//----------------------------------------------------------------------------------------------------------------------
/// @file EditorTransport.cpp
/// @brief Connects top-bar playback actions to restorable scene previews and measurements.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/PlaybackToolbar.h"
#include "App/Shell/EditorShell.h"

#include <array>
#include <format>

namespace lmx::app {

//======================================================================================================================
void EditorShell::showMeasurement() {
    setPanelVisible(EditorPanel::Performance, true);
    m_performancePanel.requestFocus = true;
    m_revealMeasurement = true;
}

//======================================================================================================================
void EditorShell::stopPlayback() {
    if (m_measurement.active())
        m_measurement.cancel();
    endMouseLook();
    if (m_playback.stop(m_session, m_settings.followCameraTrack)) {
        requestCameraCut(m_temporalState);
        m_exposureResetPending = true;
    }
    m_measurementOwnsPlayback = false;
}

//======================================================================================================================
void EditorShell::finishMeasurementPlayback() {
    if (m_measurementOwnsPlayback && !m_measurement.active())
        stopPlayback();
}

//======================================================================================================================
void EditorShell::buildPlaybackTransport(rojoRHI::Device& device,
                                         const render::Renderer& renderer) {
    finishMeasurementPlayback();
    const auto& scene = m_session.scene();
    std::string status = m_playback.playing()  ? "Playing"
                         : m_playback.active() ? "Paused"
                                               : "Stopped";
    if (m_measureOnPlay) {
        constexpr std::array names{"Ready",    "Warmup",   "Measuring",
                                   "Draining", "Complete", "Cancelled"};
        status = std::format("{} {}/{}", names[static_cast<size_t>(m_measurement.state())],
                             m_measurement.samples().size(), m_measurement.plan().measuredFrames);
    }
    const PlaybackToolbarContext context{
        .measureOnPlay = m_measureOnPlay,
        .followCameraRail = m_settings.followCameraTrack,
        .playing = m_playback.playing(),
        .active = m_playback.active(),
        .measurementActive = m_measurement.active(),
        .hasCameraRail = !scene.animation.cameraTrack.empty(),
        .canMeasure = !m_settings.dynamicResolutionEnabled,
        .timeSeconds = scene.animationTime,
        .status = status,
        .disabledReason = "Turn off dynamic resolution before starting a fixed-plan measurement."};
    switch (drawPlaybackToolbar(context)) {
    case PlaybackToolbarAction::Play:
        endMouseLook();
        if (m_measureOnPlay) {
            showMeasurement();
            startMeasurement(device, renderer);
        } else {
            m_playback.play(m_session, m_settings.followCameraTrack);
        }
        break;
    case PlaybackToolbarAction::Pause:
        m_playback.pause();
        break;
    case PlaybackToolbarAction::Stop:
        stopPlayback();
        break;
    case PlaybackToolbarAction::Step:
        endMouseLook();
        m_playback.step(m_session, m_settings.followCameraTrack);
        break;
    case PlaybackToolbarAction::None:
        break;
    case PlaybackToolbarAction::ShowMeasurement:
        showMeasurement();
        break;
    }
}

} // namespace lmx::app
