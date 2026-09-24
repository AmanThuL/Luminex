//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageDraw.cpp
/// @brief Encodes scene geometry and sky commands for a declared scene pass.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/DrawEncoding.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Scene/SceneStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Render/Passes/Scene/SceneStageInternal.h"

#include <cstdlib>

namespace lmx::render {
using namespace scene_detail;

//======================================================================================================================
void SceneStage::draw(rojoRHI::CommandList& commands, const SceneView& view,
                      const SceneStageInputs& inputs,
                      const scene_detail::PassUniforms& passUniforms,
                      const scene_detail::LocalLightParams& localLightParams,
                      GraphTexture shadowRead, GraphBuffer exposureCurrent, bool temporalEnabled,
                      const CameraFrameState& cameraState, const CameraFrameState& previousCamera,
                      glm::vec2 jitterNdc, const PassResources& resources) {
    // Resolved rather than captured: the graph hands over the shadow map only because this
    // pass declared reading it, which is what ordered it after the pass that wrote it.
    auto& shadowMapTexture = lmx::render::texture(resources, shadowRead);

    // Auto-exposure selects ScenePassAuto.slang's compiled pipeline instead of
    // ScenePass.slang's (spec 9): a separate shader file and pipeline, not a runtime
    // branch in one, is what keeps the manual pipeline's compiled output identical to
    // pre-M5 -- see ScenePassAuto.slang's header.
    rojoRHI::GraphicsPipeline* opaquePipeline = nullptr;
    if (temporalEnabled) {
        opaquePipeline = view.autoExposureEnabled
                             ? (view.wireframe ? m_sceneWireframePipelineAutoMotion.get()
                                               : m_scenePipelineAutoMotion.get())
                             : (view.wireframe ? m_sceneWireframePipelineMotion.get()
                                               : m_scenePipelineMotion.get());
    } else {
        opaquePipeline =
            view.autoExposureEnabled
                ? (view.wireframe ? m_sceneWireframePipelineAuto.get() : m_scenePipelineAuto.get())
                : (view.wireframe ? m_sceneWireframePipeline.get() : m_scenePipeline.get());
    }
    commands.bindPipeline(*opaquePipeline);
    commands.bindSampler(kLinearSamplerSlot, *inputs.linearSampler);
    commands.bindSampler(kShadowSamplerSlot, *inputs.shadowSampler);
    commands.bindSampler(kIblSamplerSlot, *inputs.iblSampler);
    commands.bindTexture(kShadowTextureSlot, shadowMapTexture);
    // The pass-wide IBL set. Each slot falls back independently, so a SceneView that
    // carries no environment still renders -- with both image-based terms at zero.
    commands.bindTexture(kIrradianceTextureSlot,
                         view.irradiance != nullptr ? *view.irradiance : *inputs.blackCubeTexture);
    commands.bindTexture(kPrefilteredEnvTextureSlot, view.prefilteredEnv != nullptr
                                                         ? *view.prefilteredEnv
                                                         : *inputs.blackCubeTexture);
    commands.bindTexture(kDfgLutTextureSlot,
                         view.dfgLut != nullptr ? *view.dfgLut : *inputs.zeroDfgTexture);
    // Only ScenePassAuto.slang/SkyAuto.slang declare this resource at all, so it is bound
    // only when their pipelines are the ones in use.
    if (view.autoExposureEnabled) {
        auto& exposureOverride = lmx::render::buffer(resources, exposureCurrent);
        commands.bindBuffer(kExposureOverrideSlot, exposureOverride);
    }
    commands.bindFrameData(kPassUniformsSlot, passUniforms);
    // Declared slots stay bound even when the selected loop never reads their data.
    rojoRHI::Buffer* lightRows = m_fallbackLightRows.get();
    if (inputs.lights.has_value()) {
        auto& imported = lmx::render::buffer(resources, *inputs.lights);
        lightRows = &imported;
    }
    commands.bindBuffer(engine::kSceneLightsSlot, *lightRows);
    rojoRHI::Buffer* grid = m_fallbackClusterGrid.get();
    rojoRHI::Buffer* indices = m_fallbackClusterIndices.get();
    if (inputs.lightGrid) {
        auto& gridResult = lmx::render::buffer(resources, *inputs.lightGrid);
        auto& indexResult = lmx::render::buffer(resources, *inputs.lightIndices);
        grid = &gridResult;
        indices = &indexResult;
    }
    commands.bindBuffer(engine::kLightClusterGridSlot, *grid);
    commands.bindBuffer(engine::kLightClusterIndexSlot, *indices);
    commands.bindFrameData(engine::kLocalLightParamsSlot, localLightParams);
    if (view.tables.vertices) {
        bindSceneTables(
            commands, view.tables,
            {kVertexBufferSlot, engine::kSceneInstancesSlot, engine::kSceneMaterialsSlot});
    }

    // R4.1 experiment harness; exp branch only.
    uint32_t opaqueRuns = 0;
    uint32_t maskedRuns = 0;

    encodeDrawRuns(
        commands, {inputs.draws, view.tables.indices, opaquePipeline},
        [&](const DrawRun& run, const auto& bindPipeline) -> const engine::DrawItem& {
            const engine::DrawItem& item = view.items[run.itemIndex];
            const bool masked = item.alphaMode == engine::AlphaMode::Mask;
            // R4.1 experiment harness; exp branch only.
            if (masked) {
                ++maskedRuns;
            } else {
                ++opaqueRuns;
            }
            const uint32_t maskIndex = (item.doubleSided ? 8u : 0u) +
                                       (view.autoExposureEnabled ? 4u : 0u) +
                                       (temporalEnabled ? 2u : 0u) + (view.wireframe ? 1u : 0u);
            auto* pipeline = masked ? m_maskScenePipelines[maskIndex].get() : opaquePipeline;
            bindPipeline(*pipeline);
            LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                       "draw instance must name a current table row");
            commands.bindTexture(kDiffuseTextureSlot,
                                 item.diffuse != nullptr ? *item.diffuse : *inputs.whiteTexture);
            commands.bindTexture(kNormalTextureSlot, item.normalMap != nullptr
                                                         ? *item.normalMap
                                                         : *inputs.flatNormalTexture);
            // The shared white fallback lets each factor pass through unchanged when a
            // material carries no map -- white is the identity for all three.
            commands.bindTexture(kMetallicRoughnessTextureSlot, item.metallicRoughness != nullptr
                                                                    ? *item.metallicRoughness
                                                                    : *inputs.whiteTexture);
            commands.bindTexture(kOcclusionTextureSlot, item.occlusion != nullptr
                                                            ? *item.occlusion
                                                            : *inputs.whiteTexture);
            commands.bindTexture(kEmissiveTextureSlot, item.emissiveMap != nullptr
                                                           ? *item.emissiveMap
                                                           : *inputs.whiteTexture);

            return item;
        });

    // R4.1 experiment harness; exp branch only.
    static const bool kExperimentPipelineLog =
        std::getenv("LMX_EXPERIMENT_PIPELINE_LOG") != nullptr;
    if (kExperimentPipelineLog) {
        LMX_LOG_INFO("r4.1-coverage auto={} motion={} opaqueRuns={} maskedRuns={}",
                     view.autoExposureEnabled ? 1 : 0, temporalEnabled ? 1 : 0, opaqueRuns,
                     maskedRuns);
    }

    // Draw the solid sky last so opaque geometry rejects covered fragments at the depth
    // clear.
    if (view.skySphere.has_value() && view.skyCubemap != nullptr) {
        // Unjittered, unlike the scene draws' mvp: the sky's vertex entry point applies
        // jitterNdc itself so its motion pair stays unjittered. Off the temporal path the
        // jitter is zero and this is the same matrix the scene rasterised with.
        const SkyUniforms sky{.viewProj = cameraState.viewProjection,
                              .eyePos = passUniforms.eyePos,
                              .eyePadding = 0.0f,
                              .preExposure = passUniforms.preExposure,
                              .jitterNdcX = jitterNdc.x,
                              .jitterNdcY = jitterNdc.y,
                              .jitterPadding = 0.0f,
                              .previousViewProj = previousCamera.viewProjection,
                              .previousEyePos = previousCamera.position,
                              .previousEyePosPadding = 0.0f};
        // Same pipeline switch as the scene draws above, for the same reason (spec 9).
        if (temporalEnabled) {
            commands.bindPipeline(view.autoExposureEnabled ? *m_skyPipelineAutoMotion
                                                           : *m_skyPipelineMotion);
        } else {
            commands.bindPipeline(view.autoExposureEnabled ? *m_skyPipelineAuto : *m_skyPipeline);
        }
        // Shaders/Passes/Scene/Sky.slang and SkyAuto.slang are the only readers of this
        // slot, so it is bound here rather than with the pass's shared set.
        commands.bindTexture(kSkyTextureSlot, *view.skyCubemap);
        commands.bindFrameData(kPassUniformsSlot, sky);
        commands.drawIndexed(*view.tables.indices, view.skySphere->indexCount,
                             view.skySphere->firstIndex);
    }
}

} // namespace lmx::render
