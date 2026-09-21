//----------------------------------------------------------------------------------------------------------------------
/// @file ShadowStage.cpp
/// @brief Implements shadow pipeline creation, uniforms and draw declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Shadow/ShadowStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Projection.h"
#include <rojoRHI/CaptureSchema.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <utility>

namespace lmx::render {
namespace {

struct ShadowPassUniforms {
    glm::mat4 lightViewProj;
};
static_assert(sizeof(ShadowPassUniforms) == 64);

// The 25-texel PCF radius needs slope bias across the whole kernel, not one texel. GPU
// measurements reached the unshadowed reference at 32; 64 provided no further improvement.
//
// Negative because depth is reversed: the bias has to push a caster's stored depth *away* from
// the light so the surface stops shadowing itself, and away from the light is now the smaller
// number. The magnitudes carry over unchanged -- the light's projection is orthographic, so its
// depth is linear in light-space distance and reversing it negates the slope without changing
// its size, which leaves the same 32 covering the same kernel. Tests/GpuRendererTests.cpp's
// sloped-bias case is the instrument that pins the sign.
constexpr rojoRHI::DepthBias kShadowDepthBias{.constant = -4.0f, .slopeScale = -32.0f};

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kPassUniformsSlot = 2;
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kLinearSamplerSlot = 0;

} // namespace

//======================================================================================================================
void ShadowStage::registerUniformLayoutsForCapture() {
    using rojoRHI::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    schema.registerUniformStruct(
        {.name = "ShadowPassUniforms",
         .slot = kPassUniformsSlot,
         .sizeBytes = sizeof(ShadowPassUniforms),
         .fields = {{"lightViewProj", offsetof(ShadowPassUniforms, lightViewProj), "float4x4"}}});
}

//======================================================================================================================
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir) {
    // Reversed to match the camera (Camera.cpp): the near plane maps to 1, the far plane to 0, so
    // "nearer to the light" is the numerically larger depth throughout -- the shadow map, the
    // GreaterEqual comparison sampler that reads it, and the Greater depth test that fills it all
    // agree on one direction.
    const OrthoFit fit =
        fitOrthoToSphere(Sphere{glm::vec3(boundingSphere), boundingSphere.w}, lightDir);
    const glm::mat4& lightView = fit.view;
    const glm::mat4& lightProj = fit.projection;

    // Map NDC xy to texture coordinates and flip y; Metal depth already uses [0, 1].
    glm::mat4 ndcToTexcoord{1.0f};
    ndcToTexcoord[0][0] = 0.5f;
    ndcToTexcoord[1][1] = -0.5f;
    ndcToTexcoord[3][0] = 0.5f;
    ndcToTexcoord[3][1] = 0.5f;

    const glm::mat4 viewProj = lightProj * lightView;
    return {.viewProj = viewProj, .shadowTransform = ndcToTexcoord * viewProj};
}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<ShadowStage>> ShadowStage::create(rojoRHI::Device& device) {
    std::unique_ptr<ShadowStage> self(new ShadowStage);

    if (auto library = device.loadShaderLibrary("Shaders/ShadowPass"); library) {
        self->m_shadowLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    // Unknown color format matches the depth-only pass and its void fragment output.
    if (auto pipeline =
            device.createGraphicsPipeline({.library = self->m_shadowLibrary.get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = rojoRHI::Format::Unknown,
                                           .depthFormat = rojoRHI::Format::D32Float,
                                           .depthTestEnable = true,
                                           .depthWriteEnable = true,
                                           // Store the light-facing surface, not the back face.
                                           .cullMode = rojoRHI::CullMode::Back,
                                           // fitShadowOrtho is reversed too, so the surface
                                           // nearest the light is the largest depth and the map
                                           // keeps what compares Greater against its 0 clear.
                                           .depthCompare = rojoRHI::DepthCompare::Greater,
                                           .depthBias = kShadowDepthBias,
                                           .label = "lmx.render.shadowPipeline"});
        pipeline) {
        self->m_shadowPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    auto shadowMaskLibrary = device.loadShaderLibrary("Shaders/ShadowPassMask");
    if (!shadowMaskLibrary) {
        return std::unexpected(shadowMaskLibrary.error());
    }
    self->m_maskShadowLibrary = std::move(*shadowMaskLibrary);
    for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
        auto pipeline = device.createGraphicsPipeline(
            {.library = self->m_maskShadowLibrary.get(),
             .vertexEntry = "vertexMain",
             .fragmentEntry = "fragmentMain",
             .colorFormat = rojoRHI::Format::Unknown,
             .depthFormat = rojoRHI::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .cullMode = doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
             .depthCompare = rojoRHI::DepthCompare::Greater,
             .depthBias = kShadowDepthBias,
             .label = doubleSided ? "lmx.render.maskShadowPipeline.doubleSided"
                                  : "lmx.render.maskShadowPipeline"});
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_maskShadowPipelines[doubleSided] = std::move(*pipeline);
    }

    return self;
}

//======================================================================================================================
GraphTexture ShadowStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                  const SceneView& view, const ShadowStageInputs& inputs) {
    const GraphTexture shadowMap = inputs.shadowMap;
    PassDesc shadowDesc;
    shadowDesc.bufferReads.assign(inputs.sceneBuffers.begin(), inputs.sceneBuffers.end());
    shadowDesc.bufferReads.push_back(inputs.drawRows);
    shadowDesc.indirectBufferReads.push_back(inputs.drawArguments);
    // 0 is the reversed far plane: nothing in the light's frustum is farther, so every caster's
    // Greater test passes against a cleared texel.
    shadowDesc.depth = DepthAttachment{
        .handle = shadowMap, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    graph.addPass(
        "lmx.pass.shadow", std::move(shadowDesc),
        [this, &commands, view, inputs,
         lightViewProj = inputs.lightViewProj](const PassResources&) {
            if (view.tables.vertices) {
                commands.bindBuffer(kVertexBufferSlot, *view.tables.vertices);
                commands.bindBuffer(engine::kSceneInstancesSlot, *view.tables.instances);
                commands.bindBuffer(engine::kSceneMaterialsSlot, *view.tables.materials);
            }
            commands.bindFrameData(kPassUniformsSlot, ShadowPassUniforms{lightViewProj});
            rojoRHI::GraphicsPipeline* bound = nullptr;
            commands.bindBuffer(engine::kVisibleRowsSlot, *inputs.draws.rows);
            if (inputs.draws.mode != SubmissionMode::Direct)
                commands.bindFrameData(engine::kDrawUniformsSlot, engine::DrawUniforms{0});
            for (const auto& run : inputs.draws.runs) {
                const engine::DrawItem& item = view.items[run.itemIndex];
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "draw instance must name a current table row");
                const bool masked = item.alphaMode == engine::AlphaMode::Mask;
                auto* pipeline = masked ? m_maskShadowPipelines[item.doubleSided ? 1 : 0].get()
                                        : m_shadowPipeline.get();
                if (pipeline != bound) {
                    commands.bindPipeline(*pipeline);
                    bound = pipeline;
                }

                if (masked) {
                    commands.bindTexture(kDiffuseTextureSlot,
                                         item.diffuse ? *item.diffuse : *inputs.whiteTexture);
                    commands.bindSampler(kLinearSamplerSlot, *inputs.linearSampler);
                }
                if (inputs.draws.mode == SubmissionMode::Direct) {
                    commands.bindFrameData(engine::kDrawUniformsSlot,
                                           engine::DrawUniforms{run.firstEntry});
                    commands.drawIndexed(*view.tables.indices, item.mesh.indexCount,
                                         item.mesh.firstIndex);
                } else {
                    commands.drawIndexedIndirect(*view.tables.indices, *inputs.draws.arguments,
                                                 uint64_t{run.argumentIndex} *
                                                     sizeof(rojoRHI::DrawIndexedIndirectArgs));
                }
            }
        });

    return nextVersion(shadowMap);
}

} // namespace lmx::render
