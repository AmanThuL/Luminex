//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStatus.cpp
/// @brief Records declared frame status and exposes renderer-owned targets.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Diagnostics/Assert.h"
#include "Render/Passes/Exposure/ExposureStage.h"
#include "Render/Renderer/RendererInternal.h"
#include <algorithm>

namespace lmx::render {
//======================================================================================================================
void Renderer::recordFrameStatus(const RendererFrameState& state) {
    // The frame just declared becomes the previous one. A frame the caller abandoned before
    // declaring never reaches here, so it never becomes anyone's predecessor.
    ++m_declaredFrames;
    m_temporalStatus.lastReset = state.resetReason;
    if (state.resetReason != HistoryResetReason::None) {
        m_temporalStatus.lastResetFrame = m_declaredFrames;
    }
    m_temporalStatus.jitterIndex = m_temporalFrame % kJitterSequenceLength;
    m_temporalStatus.historyValid = state.historyValid;
    m_temporalStatus.historyBytes = m_temporalResolve->colorBytes();
    m_temporalStatus.depthHistoryBytes = m_temporalResolve->depthBytes();
    m_temporalStatus.reconstruction = state.reconstruction;
    m_temporalStatus.vendorFallback = state.selection.fallback;
    m_temporalStatus.vendorName = m_gpu.upscalerSupport().name;
    m_temporalStatus.vendorReset = state.vendorTemporal && m_temporalResolve->vendorReset();
    m_temporalStatus.vendorScalerGeneration = m_temporalResolve->vendorScalerGeneration();
    // The age counts declared temporal frames since the last non-None reason, whatever the mode:
    // both modes leave a real frame in the colour slot, so the count survives a mode switch. A
    // frame with temporal off starts it over, because the history the next temporal frame finds is
    // not the one this count would have described.
    if (!state.temporalEnabled) {
        m_temporalResolve->recordDisabledFrame();
        m_temporalStatus.historyAge = 0;
    } else if (state.resetReason != HistoryResetReason::None) {
        m_temporalStatus.historyAge = 1;
    } else {
        m_temporalStatus.historyAge = std::min<uint32_t>(m_temporalStatus.historyAge + 1, 65535);
    }
    m_temporalStatus.warmupComplete = m_temporalStatus.historyAge >= kTemporalWarmupFrames;
    m_temporalStatus.extents = state.extents;
    m_temporalStatus.renderScale = state.renderScale;
    m_temporalStatus.upscaled = state.upscaled;
    // A render-extent change under a reset reason says nothing: the history is being thrown away
    // anyway, and a frame with temporal off has no history to have survived anything -- it also
    // rasterises at the output extent whatever the scale field says, so a scale change straddling
    // it would otherwise be recorded twice. Under None with temporal on it is the whole point --
    // the history survived a change of the extent the scene rasterised at -- so that is the only
    // frame the count records.
    if (state.temporalEnabled && state.resetReason == HistoryResetReason::None &&
        m_previousSignature &&
        (m_previousSignature->extents.renderWidth != state.extents.renderWidth ||
         m_previousSignature->extents.renderHeight != state.extents.renderHeight)) {
        m_temporalStatus.lastRenderExtentChangeFrame = m_declaredFrames;
    }
    m_previousSignature = state.signature;
    m_previousCamera = state.cameraState;
    if (state.temporalEnabled) {
        m_temporalResolve->recordFrame(state.reconstruction);
        ++m_temporalFrame;
    }
}

//======================================================================================================================
rojoRHI::Buffer& Renderer::exposureBuffer() {
    return m_exposureStage->buffer();
}

//======================================================================================================================
const engine::GpuTexture& Renderer::colorTarget() {
    LMX_ASSERT(m_color, "Renderer::colorTarget: no color target -- create() failed");
    return m_color;
}

//======================================================================================================================
rojoRHI::Texture& Renderer::hdrColorTarget() {
    LMX_ASSERT(m_hdrColor, "Renderer::hdrColorTarget: no scene color target -- create() failed");
    return m_hdrColor.rhi();
}

//======================================================================================================================
rojoRHI::Texture& Renderer::depthTarget() {
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::depthTarget: no depth target -- create() failed");
    return m_temporalResolve->depthSlot(m_currentSlot);
}

//======================================================================================================================
rojoRHI::Texture* Renderer::historyTarget() {
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::historyTarget: no history -- create() failed");
    return &m_temporalResolve->colorSlot(m_currentSlot);
}

} // namespace lmx::render
