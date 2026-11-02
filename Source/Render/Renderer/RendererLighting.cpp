//----------------------------------------------------------------------------------------------------------------------
/// @file RendererLighting.cpp
/// @brief Coordinates light-list declaration, paced retirement and frame-keyed diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/PacedSlots.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/RendererInternal.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <limits>

namespace lmx::render {

//======================================================================================================================
lmx::Result<RendererLightingFrame>
Renderer::prepareLighting(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                          std::optional<GraphBuffer> lights, const FrameExtents& extents,
                          const CameraFrameState& cameraState) {
    const uint64_t frame = m_gpu.frameNumber();
    // beginFrame has paced this slot; read its previous contents before any new GPU write.
    if (frame >= 3) {
        auto retired = retireLightingThrough(frame - 3);
        if (!retired)
            return std::unexpected(retired.error());
    }
    const auto effective =
        view.tables.liveLightCount == 0 ? engine::LocalLightMode::Off : view.localLightMode;
    // Creation completes before anything of this frame is declared or staged.
    if (effective == engine::LocalLightMode::Clustered && !m_lightClusters) {
        auto stage = LightClusterStage::create(m_gpu);
        if (!stage)
            return std::unexpected(stage.error());
        m_lightClusters = std::move(*stage);
    }
    RendererLightingFrame result;
    LightingStatus& status = result.status;
    status = {.requested = view.localLightMode,
              .effective = effective,
              .frameNumber = frame,
              .sceneGeneration = view.temporal.sceneGeneration,
              .liveLightCount = view.tables.liveLightCount,
              .checkEnabled = view.lightCheck};
    if (effective == engine::LocalLightMode::Clustered) {
        LMX_ASSERT(lights.has_value(), "clustered lighting requires the imported scene light rows");
        result.clusters = m_lightClusters->declare(
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
            status.checkFrame = std::move(checkFrame);
        }
    }
    status.allocatedListBytes =
        m_lightClusters ? uint64_t{3} * kLightClusterIndexCapacity * sizeof(uint32_t) : 0;
    return result;
}

//======================================================================================================================
void Renderer::abandonLighting(const RendererLightingFrame& lighting) {
    if (lighting.clusters.declared)
        m_lightClusters->discardUnrecorded(lighting.clusters);
}

//======================================================================================================================
void Renderer::commitLighting(RendererLightingFrame&& lighting) {
    m_lightingStatus = lighting.status;
    m_pendingLighting.push_back(std::move(lighting.status));
}

//======================================================================================================================
lmx::Result<void> Renderer::retireLightingThrough(uint64_t frame) {
    // The cluster owner's take is destructive, so every record it retired -- including those it
    // retired before a later declaration failed -- is held here until its status joins it.
    lmx::Result<void> clustersRetired;
    if (m_lightClusters) {
        clustersRetired = m_lightClusters->retireThrough(frame);
        std::ranges::move(m_lightClusters->takeRetired(), std::back_inserter(m_retiredClusters));
    }
    while (!m_pendingLighting.empty() && m_pendingLighting.front().frameNumber <= frame) {
        LightingStatus status = m_pendingLighting.front();
        if (status.effective == engine::LocalLightMode::Clustered) {
            const auto found = std::ranges::find(m_retiredClusters, status.frameNumber,
                                                 &RetiredLightClusters::frameNumber);
            if (found == m_retiredClusters.end()) {
                // A failed cluster retirement keeps this status and every later one pending.
                if (!clustersRetired)
                    return std::unexpected(clustersRetired.error());
                return std::unexpected(
                    Error{ErrorCode::InvalidArgument,
                          std::format("lighting frame {} has no retired light clusters to join",
                                      status.frameNumber)});
            }
            status.counters = found->counters;
            status.listBytes = uint64_t{found->counters.assigned} * sizeof(uint32_t);
            if (status.checkEnabled) {
                LMX_ASSERT(status.checkFrame, "checked declaration must own its CPU mirror");
                auto evidence = std::make_shared<LightClusterCheckFrame>(*status.checkFrame);
                evidence->gpu = {.grid = std::move(found->grid),
                                 .indices = std::move(found->indices),
                                 .counters = found->counters};
                status.check = checkLightClusters(evidence->cpu, evidence->gpu.grid,
                                                  evidence->gpu.indices, evidence->gpu.counters);
                status.checkFrame = std::move(evidence);
            }
            m_retiredClusters.erase(found);
        }
        status.isRetired = true;
        if (status.frameNumber == m_lightingStatus.frameNumber)
            m_lightingStatus = status;
        m_retiredLighting.push_back(std::move(status));
        m_pendingLighting.erase(m_pendingLighting.begin());
    }
    return clustersRetired;
}

//======================================================================================================================
std::vector<LightingStatus> Renderer::takeRetiredLighting() {
    return lmx::render::takeRetired(m_retiredLighting);
}

//======================================================================================================================
lmx::Result<void> Renderer::drainLightingAfterIdle() {
    return retireLightingThrough(std::numeric_limits<uint64_t>::max());
}

} // namespace lmx::render
