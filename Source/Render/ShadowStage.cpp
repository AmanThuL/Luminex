//----------------------------------------------------------------------------------------------------------------------
/// @file ShadowStage.cpp
/// @brief Implements shadow pipeline creation, uniforms and draw declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/ShadowStage.h"

#include "Core/Assert.h"
#include "RHI/CaptureSchema.h"
#include "Render/AlphaMaskParams.h"
#include "Render/Mesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <utility>

namespace lmx::render {
namespace {

// Mirrors Shaders/ShadowPass.slang's ObjectUniforms.
struct ShadowObjectUniforms {
    glm::mat4 mvp;
};
static_assert(sizeof(ShadowObjectUniforms) == 64, "must match ShadowPass.slang's ObjectUniforms");

struct ShadowMaskObjectUniforms {
    glm::mat4 mvp;
    glm::mat4 uvTransform;
    float albedoAlpha;
    float padding[3]{};
};
static_assert(sizeof(ShadowMaskObjectUniforms) == 144);
static_assert(offsetof(ShadowMaskObjectUniforms, albedoAlpha) == 128);

// The 25-texel PCF radius needs slope bias across the whole kernel, not one texel. GPU
// measurements reached the unshadowed reference at 32; 64 provided no further improvement.
//
// Negative because depth is reversed: the bias has to push a caster's stored depth *away* from
// the light so the surface stops shadowing itself, and away from the light is now the smaller
// number. The magnitudes carry over unchanged -- the light's projection is orthographic, so its
// depth is linear in light-space distance and reversing it negates the slope without changing
// its size, which leaves the same 32 covering the same kernel. Tests/GpuRendererTests.cpp's
// sloped-bias case is the instrument that pins the sign.
constexpr rhi::DepthBias kShadowDepthBias{.constant = -4.0f, .slopeScale = -32.0f};

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kLinearSamplerSlot = 0;

} // namespace

//======================================================================================================================
void ShadowStage::registerUniformLayoutsForCapture() {
    using rhi::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    schema.registerUniformStruct(
        {.name = "ShadowObjectUniforms",
         .slot = kObjectUniformsSlot,
         .sizeBytes = sizeof(ShadowObjectUniforms),
         .fields = {{"mvp", offsetof(ShadowObjectUniforms, mvp), "float4x4"}}});

    schema.registerUniformStruct(
        {.name = "ShadowMaskObjectUniforms",
         .slot = kObjectUniformsSlot,
         .sizeBytes = sizeof(ShadowMaskObjectUniforms),
         .fields = {{"mvp", offsetof(ShadowMaskObjectUniforms, mvp), "float4x4"},
                    {"uvTransform", offsetof(ShadowMaskObjectUniforms, uvTransform), "float4x4"},
                    {"albedoAlpha", offsetof(ShadowMaskObjectUniforms, albedoAlpha), "float"}}});
    schema.registerUniformStruct({.name = "AlphaMaskParams",
                                  .slot = kAlphaMaskParamsSlot,
                                  .sizeBytes = sizeof(AlphaMaskParams),
                                  .fields = {{"cutoff", 0, "float"}}});
}

//======================================================================================================================
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir) {
    const glm::vec3 center{boundingSphere};
    const float radius = boundingSphere.w;
    LMX_ASSERT(radius > 0.0f, "fitShadowOrtho: the bounding sphere's radius must be positive");
    LMX_ASSERT(glm::length(lightDir) > 0.0f,
               "fitShadowOrtho: the light direction must not be the zero vector");

    const glm::vec3 direction = glm::normalize(lightDir);
    // Offset from the sphere center so translated scenes retain the same fitted light volume.
    const glm::vec3 eye = center - 2.0f * radius * direction;

    // Avoid lookAt's degenerate cross product when light direction is parallel to world up.
    constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 up =
        std::abs(glm::dot(direction, kWorldUp)) > 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : kWorldUp;
    const glm::mat4 lightView = glm::lookAtRH(eye, center, up);

    const glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
    // Right-handed view space looks down -z, so positive near/far distances use -centerLS.z: the
    // frustum runs from r to 3r about a centre 2r out.
    //
    // Reversed to match the camera (Camera.cpp): the near plane maps to 1, the far plane to 0, so
    // "nearer to the light" is the numerically larger depth throughout -- the shadow map, the
    // GreaterEqual comparison sampler that reads it, and the Greater depth test that fills it all
    // agree on one direction. The reversal is expressed by handing orthoRH_ZO its far distance as
    // near and vice versa, which is exactly a z negate-and-offset applied to the standard form and
    // leaves the xy fit untouched.
    const float nearDistance = -centerLS.z - radius;
    const float farDistance = -centerLS.z + radius;
    const glm::mat4 lightProj =
        glm::orthoRH_ZO(centerLS.x - radius, centerLS.x + radius, centerLS.y - radius,
                        centerLS.y + radius, farDistance, nearDistance);

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
rhi::Result<std::unique_ptr<ShadowStage>> ShadowStage::create(rhi::Device& device) {
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
                                           .colorFormat = rhi::Format::Unknown,
                                           .depthFormat = rhi::Format::D32Float,
                                           .depthTestEnable = true,
                                           .depthWriteEnable = true,
                                           // Store the light-facing surface, not the back face.
                                           .cullMode = rhi::CullMode::Back,
                                           // fitShadowOrtho is reversed too, so the surface
                                           // nearest the light is the largest depth and the map
                                           // keeps what compares Greater against its 0 clear.
                                           .depthCompare = rhi::DepthCompare::Greater,
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
             .colorFormat = rhi::Format::Unknown,
             .depthFormat = rhi::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .cullMode = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back,
             .depthCompare = rhi::DepthCompare::Greater,
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
GraphTexture ShadowStage::declare(RenderGraph& graph, rhi::CommandList& commands,
                                  const SceneView& view, const ShadowStageInputs& inputs) {
    const GraphTexture shadowMap = inputs.shadowMap;
    PassDesc shadowDesc;
    // 0 is the reversed far plane: nothing in the light's frustum is farther, so every caster's
    // Greater test passes against a cleared texel.
    shadowDesc.depth = DepthAttachment{
        .handle = shadowMap, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    graph.addPass(
        "lmx.pass.shadow", std::move(shadowDesc),
        [this, &commands, view, inputs,
         lightViewProj = inputs.lightViewProj](const PassResources&) {
            rhi::GraphicsPipeline* bound = nullptr;
            for (const DrawItem& item : view.items) {
                LMX_ASSERT(item.mesh != nullptr, "DrawItem.mesh must not be null");
                const bool masked = item.material.alphaMode == AlphaMode::Mask;
                auto* pipeline =
                    masked ? m_maskShadowPipelines[item.material.doubleSided ? 1 : 0].get()
                           : m_shadowPipeline.get();
                if (pipeline != bound) {
                    commands.bindPipeline(*pipeline);
                    bound = pipeline;
                }
                commands.bindBuffer(kVertexBufferSlot, *item.mesh->vertexBuffer);
                if (masked) {
                    const ShadowMaskObjectUniforms uniforms{.mvp = lightViewProj * item.model,
                                                            .uvTransform =
                                                                item.material.uvTransform,
                                                            .albedoAlpha = item.material.albedo.a};
                    commands.bindFrameData(kObjectUniformsSlot, uniforms);
                    commands.bindFrameData(kAlphaMaskParamsSlot,
                                           AlphaMaskParams{item.material.alphaCutoff});
                    commands.bindTexture(kDiffuseTextureSlot, item.material.diffuse
                                                                  ? *item.material.diffuse
                                                                  : *inputs.whiteTexture);
                    commands.bindSampler(kLinearSamplerSlot, *inputs.linearSampler);
                } else {
                    const ShadowObjectUniforms uniforms{.mvp = lightViewProj * item.model};
                    commands.bindFrameData(kObjectUniformsSlot, uniforms);
                }
                commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
            }
        });

    return nextVersion(shadowMap);
}

} // namespace lmx::render
