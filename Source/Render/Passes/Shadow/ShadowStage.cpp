//----------------------------------------------------------------------------------------------------------------------
/// @file ShadowStage.cpp
/// @brief Implements shadow pipeline creation, uniforms and draw declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Shadow/ShadowStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Projection.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Passes/Shadow/ShadowRoots.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace lmx::render {
//======================================================================================================================
void ShadowStage::registerUniformLayoutsForCapture() {
    engine::registerUniformStruct(
        {.name = "ShadowPassUniforms",
         .sizeBytes = sizeof(ShadowPassUniforms),
         .fields = {{"lightViewProj", offsetof(ShadowPassUniforms, lightViewProj), "float4x4"}}});
    engine::registerUniformStruct(
        {"ShadowPassRoot",
         sizeof(ShadowPassRoot),
         {{"vertices", offsetof(ShadowPassRoot, vertices), "Addr"},
          {"visibleRows", offsetof(ShadowPassRoot, visibleRows), "Addr"},
          {"instances", offsetof(ShadowPassRoot, instances), "Addr"},
          {"materials", offsetof(ShadowPassRoot, materials), "Addr"},
          {"uniforms", offsetof(ShadowPassRoot, uniforms), "Addr"},
          {"linearSampler", offsetof(ShadowPassRoot, linearSampler), "uint"},
          {"padding", offsetof(ShadowPassRoot, padding), "uint"}}});
    engine::registerUniformStruct({"ShadowDrawRoot",
                                   sizeof(ShadowDrawRoot),
                                   {{"pass", offsetof(ShadowDrawRoot, pass), "Addr"},
                                    {"firstEntry", offsetof(ShadowDrawRoot, firstEntry), "uint"},
                                    {"baseColor", offsetof(ShadowDrawRoot, baseColor), "uint"},
                                    {"indirect", offsetof(ShadowDrawRoot, indirect), "uint"},
                                    {"padding0", offsetof(ShadowDrawRoot, padding0), "uint"},
                                    {"padding1", offsetof(ShadowDrawRoot, padding1), "uint"},
                                    {"padding2", offsetof(ShadowDrawRoot, padding2), "uint"}}});
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
lmx::Result<std::unique_ptr<ShadowStage>> ShadowStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<ShadowStage> self(new ShadowStage);

    {
        auto library = gpu.loadShaderLibrary("ShadowPass");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_shadowLibrary = std::move(*library);
    }
    // Empty colors matches the depth-only pass and its void fragment output.
    {
        auto pipeline = engine::adoptGpu(
            device,
            device.createPipeline({.vertex = {self->m_shadowLibrary.get(), "vertexMain"},
                                   .fragment = {self->m_shadowLibrary.get(), "fragmentMain"},
                                   .raster = {.cull = rojoRHI::CullMode::Back, .depthBias = true},
                                   .depthStencil = {.depthTest = true,
                                                    .depthWrite = true,
                                                    .depthCompare = rojoRHI::CompareOp::Greater},
                                   .depthFormat = rojoRHI::Format::D32Float,
                                   .label = "lmx.render.shadowPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_shadowPipeline = std::move(*pipeline);
    }

    auto shadowMaskLibrary = gpu.loadShaderLibrary("ShadowPassMask");
    if (!shadowMaskLibrary) {
        return std::unexpected(shadowMaskLibrary.error());
    }
    self->m_maskShadowLibrary = std::move(*shadowMaskLibrary);
    for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
        auto pipeline = engine::adoptGpu(
            device,
            device.createPipeline(
                {.vertex = {self->m_maskShadowLibrary.get(), "vertexMain"},
                 .fragment = {self->m_maskShadowLibrary.get(), "fragmentMain"},
                 .raster = {.cull = doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
                            .depthBias = true},
                 .depthStencil = {.depthTest = true,
                                  .depthWrite = true,
                                  .depthCompare = rojoRHI::CompareOp::Greater},
                 .depthFormat = rojoRHI::Format::D32Float,
                 .label = doubleSided ? "lmx.render.maskShadowPipeline.doubleSided"
                                      : "lmx.render.maskShadowPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_maskShadowPipelines[doubleSided] = std::move(*pipeline);
    }
    registerUniformLayoutsForCapture();

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
            if (inputs.draws.runs.empty()) {
                return;
            }
            const ShadowPassUniforms uniforms{lightViewProj};
            const auto uniformsAddress = frameData(commands, uniforms);
            engine::registerFrameRoot("ShadowPassUniforms", uniformsAddress);
            const ShadowPassRoot pass{.vertices = view.tables.vertices->address(),
                                      .visibleRows = inputs.draws.rows->address(),
                                      .instances = view.tables.instances->address(),
                                      .materials = view.tables.materials
                                                       ? view.tables.materials->address()
                                                       : rojoRHI::Addr{},
                                      .uniforms = uniformsAddress,
                                      .linearSampler = static_cast<uint32_t>(inputs.linearSampler)};
            const auto passAddress = frameData(commands, pass);
            engine::registerFrameRoot("ShadowPassRoot", passAddress);
            rojoRHI::Pipeline bound{};
            for (const auto& run : inputs.draws.runs) {
                const engine::DrawItem& item = view.items[run.itemIndex];
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "draw instance must name a current table row");
                if (run.instanceCount == 0 || item.mesh.indexCount == 0) {
                    continue;
                }
                const bool masked = item.alphaMode == engine::AlphaMode::Mask;
                const auto pipeline = masked ? m_maskShadowPipelines[item.doubleSided ? 1 : 0].get()
                                             : m_shadowPipeline.get();
                if (pipeline != bound) {
                    commands.setPipeline(pipeline);
                    bound = pipeline;
                }
                const auto* diffuse =
                    masked ? (item.diffuse ? item.diffuse : inputs.whiteTexture) : nullptr;
                const ShadowDrawRoot draw{
                    .pass = passAddress,
                    .firstEntry = run.firstEntry,
                    .baseColor = diffuse ? static_cast<uint32_t>(diffuse->view()) : 0u,
                    .indirect = inputs.draws.mode != SubmissionMode::Direct ? 1u : 0u};
                engine::registerFrameRoot("ShadowDrawRoot", commands.pushRoot(draw));
                // Reversed depth pushes casters away from the light; the slope spans the PCF
                // kernel.
                commands.setDepthBias(-4.0f, -32.0f, 0.0f);
#ifndef NDEBUG
                const std::array<rojoRHI::ResourceAccess, 3> vertexAccesses{
                    {{.buffer = view.tables.vertices->rhi(),
                      .size = view.tables.vertices->desc().size,
                      .stage = rojoRHI::Stage::VertexShader,
                      .access = rojoRHI::Access::ShaderRead},
                     {.buffer = inputs.draws.rows->rhi(),
                      .offset = uint64_t{run.firstEntry} * 4,
                      .size = uint64_t{run.instanceCount} * 4,
                      .stage = rojoRHI::Stage::VertexShader,
                      .access = rojoRHI::Access::ShaderRead},
                     {.buffer = view.tables.instances->rhi(),
                      .size = uint64_t{view.tables.instanceCount} * sizeof(engine::InstanceRow),
                      .stage = rojoRHI::Stage::VertexShader,
                      .access = rojoRHI::Access::ShaderRead}}};
                commands.declareAccess(vertexAccesses);
                if (masked) {
                    const std::array<rojoRHI::ResourceAccess, 4> maskAccesses{
                        {{.buffer = view.tables.materials->rhi(),
                          .size = uint64_t{view.tables.materialCount} * sizeof(engine::MaterialRow),
                          .stage = rojoRHI::Stage::VertexShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.instances->rhi(),
                          .size = uint64_t{view.tables.instanceCount} * sizeof(engine::InstanceRow),
                          .stage = rojoRHI::Stage::FragmentShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.buffer = view.tables.materials->rhi(),
                          .size = uint64_t{view.tables.materialCount} * sizeof(engine::MaterialRow),
                          .stage = rojoRHI::Stage::FragmentShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.texture = diffuse->rhi(),
                          .range = {0, diffuse->desc().mips, 0, diffuse->desc().layers},
                          .stage = rojoRHI::Stage::FragmentShader,
                          .access = rojoRHI::Access::ShaderRead}}};
                    commands.declareAccess(maskAccesses);
                }
#endif
                if (inputs.draws.mode == SubmissionMode::Direct) {
                    commands.drawIndexed(view.tables.indices->rhi(), rojoRHI::IndexType::UInt32,
                                         item.mesh.indexCount, run.instanceCount,
                                         item.mesh.firstIndex, 0, 0);
                } else {
                    commands.drawIndexedIndirect(
                        view.tables.indices->rhi(), rojoRHI::IndexType::UInt32,
                        inputs.draws.arguments->address(uint64_t{run.argumentIndex} *
                                                        sizeof(rojoRHI::DrawIndexedIndirectArgs)),
                        1);
                }
            }
        });

    return nextVersion(shadowMap);
}

} // namespace lmx::render
