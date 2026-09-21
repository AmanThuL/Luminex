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
void Renderer::recordFrameStatus(const RendererFrameState& state, bool lightDebugEnabled) {
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
    m_temporalStatus.vendorName = m_device.capabilities().temporalScaler.name;
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
    // What this frame's last access to each persistent target was, for the next frame's imports to
    // state. The motion and reactive records survive frames with temporal off, which touch neither
    // target: overwriting them there would let a later re-enabling frame claim the last access was
    // its own attachment write, and its fragment-stage barrier would not drain the dispatch-stage
    // read the last temporal frame actually ended with.
    if (state.temporalEnabled) {
        // The resolve reads motion on every NativeTaa frame, and the debug view reads it whenever
        // one is shown.
        m_previousMotionUse =
            state.nativeTaa || state.vendorTemporal || state.debugView != TemporalDebugView::Off
                ? rojoRHI::TextureUse::ShaderRead
                : rojoRHI::TextureUse::RenderTarget;
        // Only the resolve reads the reactive attachment, so a Raw frame ends with its own write.
        m_previousReactiveUse = state.nativeTaa || state.vendorTemporal
                                    ? rojoRHI::TextureUse::ShaderRead
                                    : rojoRHI::TextureUse::RenderTarget;
        m_temporalResolve->recordFrame(state.slot, state.reconstruction, state.debugView,
                                       state.historyValid, state.upscaled);
    }
    if (lightDebugEnabled)
        m_temporalResolve->recordDepthRead(state.temporalEnabled ? state.slot : 0);
    // The scene colour is written and read by every frame. Under Raw at the output extent the
    // commit copy is the last thing to touch it; an upscaled Raw frame samples it in the spatial
    // pass instead of copying it, and under NativeTaa and with temporal off, bloom, the histogram
    // and the display transform all read it and none copies out of it.
    m_previousSceneColorUse = state.vendorTemporal ? rojoRHI::TextureUse::ExternalRead
                              : state.temporalEnabled && !state.nativeTaa && !state.upscaled
                                  ? rojoRHI::TextureUse::CopySource
                                  : rojoRHI::TextureUse::ShaderRead;
    if (state.temporalEnabled) {
        ++m_temporalFrame;
    }
}

//======================================================================================================================
rojoRHI::Buffer& Renderer::exposureBuffer() {
    return m_exposureStage->buffer();
}

//======================================================================================================================
rojoRHI::Texture& Renderer::colorTarget() {
    LMX_ASSERT(m_color != nullptr, "Renderer::colorTarget: no color target -- create() failed");
    return *m_color;
}

//======================================================================================================================
rojoRHI::Texture& Renderer::hdrColorTarget() {
    LMX_ASSERT(m_hdrColor != nullptr,
               "Renderer::hdrColorTarget: no scene color target -- create() failed");
    return *m_hdrColor;
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
