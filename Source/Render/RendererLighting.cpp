//----------------------------------------------------------------------------------------------------------------------
/// @file RendererLighting.cpp
/// @brief Coordinates light-list declaration, paced retirement and frame-keyed diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer.h"

#include "Core/Assert.h"

#include <algorithm>
#include <limits>

namespace lmx::render {

//======================================================================================================================
LightClusterOutputs Renderer::prepareLighting(RenderGraph& graph, rojoRHI::CommandList& commands,
                                              const SceneView& view,
                                              std::optional<GraphBuffer> lights,
                                              const FrameExtents& extents,
                                              const CameraFrameState& cameraState) {
    const uint64_t frame = m_device.frameNumber();
    // beginFrame has paced this slot; read its previous contents before any new GPU write.
    if (frame >= 3)
        retireLightingThrough(frame - 3);
    m_lightingStatus = {.requested = view.localLightMode,
                        .effective = view.tables.liveLightCount == 0 ? LocalLightMode::Off
                                                                     : view.localLightMode,
                        .frameNumber = frame,
                        .sceneGeneration = view.temporal.sceneGeneration,
                        .liveLightCount = view.tables.liveLightCount,
                        .checkEnabled = view.lightCheck};
    LightClusterOutputs output;
    if (m_lightingStatus.effective == LocalLightMode::Clustered) {
        LMX_ASSERT(lights.has_value(), "clustered lighting requires the imported scene light rows");
        if (!m_lightClusters) {
            auto stage = LightClusterStage::create(m_device);
            LMX_ASSERT(stage.has_value(), stage.error().message);
            m_lightClusters = std::move(*stage);
        }
        output = m_lightClusters->declare(
            graph, commands,
            {.clustered = true,
             .liveLightCount = view.tables.liveLightCount,
             .rowCount = view.tables.lightRowCount,
             .lights = *lights,
             .view = cameraState.view,
             .inverseJitteredProjection = glm::inverse(cameraState.projectionJittered),
             .sliceDepth = clusterSliceDepths(cameraState.nearZ),
             .activeWidth = extents.renderWidth,
             .activeHeight = extents.renderHeight,
             .frameNumber = frame,
             .shaderReadsOutputs = true,
             .captureLists = view.lightCheck});
        if (view.lightCheck) {
            auto checkFrame = std::make_shared<LightClusterCheckFrame>();
            checkFrame->frameNumber = frame;
            checkFrame->params = {.view = cameraState.view,
                                  .inverseJitteredProjection =
                                      glm::inverse(cameraState.projectionJittered),
                                  .rowCount = view.tables.lightRowCount,
                                  .activeWidth = extents.renderWidth,
                                  .activeHeight = extents.renderHeight,
                                  .sliceDepth = clusterSliceDepths(cameraState.nearZ)};
            // Consume borrowed rows now, before the scene can mutate or prepare a later slot.
            checkFrame->cpu = buildLightClusters(view.tables.lightRows, checkFrame->params);
            m_lightingStatus.checkFrame = std::move(checkFrame);
        }
    }
    m_lightingStatus.allocatedListBytes =
        m_lightClusters ? uint64_t{3} * kLightClusterIndexCapacity * sizeof(uint32_t) : 0;
    m_pendingLighting.push_back(m_lightingStatus);
    return output;
}

//======================================================================================================================
void Renderer::retireLightingThrough(uint64_t frame) {
    std::vector<RetiredLightClusters> clusters;
    if (m_lightClusters) {
        m_lightClusters->retireThrough(frame);
        clusters = m_lightClusters->takeRetired();
    }
    for (auto status : m_pendingLighting) {
        if (status.frameNumber > frame)
            continue;
        if (status.effective == LocalLightMode::Clustered) {
            const auto result =
                std::ranges::find(clusters, status.frameNumber, &RetiredLightClusters::frameNumber);
            LMX_ASSERT(result != clusters.end(), "retired clusters must join their declared frame");
            status.counters = result->counters;
            status.listBytes = uint64_t{result->counters.assigned} * sizeof(uint32_t);
            if (status.checkEnabled) {
                LMX_ASSERT(status.checkFrame, "checked declaration must own its CPU mirror");
                auto evidence = std::make_shared<LightClusterCheckFrame>(*status.checkFrame);
                evidence->gpu = {.grid = std::move(result->grid),
                                 .indices = std::move(result->indices),
                                 .counters = result->counters};
                status.check = checkLightClusters(evidence->cpu, evidence->gpu.grid,
                                                  evidence->gpu.indices, evidence->gpu.counters);
                status.checkFrame = std::move(evidence);
            }
        }
        status.isRetired = true;
        if (status.frameNumber == m_lightingStatus.frameNumber)
            m_lightingStatus = status;
        m_retiredLighting.push_back(status);
    }
    std::erase_if(m_pendingLighting,
                  [frame](const LightingStatus& status) { return status.frameNumber <= frame; });
}

//======================================================================================================================
std::vector<LightingStatus> Renderer::takeRetiredLighting() {
    auto result = std::move(m_retiredLighting);
    m_retiredLighting.clear();
    return result;
}

//======================================================================================================================
void Renderer::drainLightingAfterIdle() {
    retireLightingThrough(std::numeric_limits<uint64_t>::max());
}

} // namespace lmx::render
