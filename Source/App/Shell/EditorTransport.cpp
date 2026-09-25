//----------------------------------------------------------------------------------------------------------------------
/// @file EditorTransport.cpp
/// @brief Connects top-bar playback actions to restorable scene previews and measurements.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/PlaybackToolbar.h"
#include "App/Shell/EditorShell.h"

#include "App/Model/Workspace/MenuBarFit.h"
#include "App/Panels/Shared/EditorStyle.h"
#include <format>

namespace lmx::app {

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
void EditorShell::buildPlaybackTransport() {
    const auto& scene = m_session.scene();
    const std::string readout =
        m_measurement.active() ? std::format("Measuring {} / {}", m_measurement.samples().size(),
                                             m_measurement.plan().measuredFrames)
                               : std::format("{:.3f} s", scene.animationTime);
    const PlaybackToolbarContext context{.followCameraRail = m_settings.followCameraTrack,
                                         .playing = m_playback.playing(),
                                         .active = m_playback.active(),
                                         .measurementActive = m_measurement.active(),
                                         .hasCameraRail = !scene.animation.cameraTrack.empty(),
                                         .readout = readout};
    const std::string zoomLabel = std::to_string(m_workspace.uiScalePercent) + "%##ResetUiZoom";
    const float zoomWidth = ImGui::CalcTextSize(zoomLabel.c_str(), nullptr, true).x +
                            ImGui::GetStyle().FramePadding.x * 2.0f;
    const float available = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    const MenuBarWidths widths{ImGui::GetCursorPosX(), playbackToolbarButtonsWidth(context),
                               ImGui::CalcTextSize(readout.c_str()).x, zoomWidth,
                               ImGui::GetStyle().ItemSpacing.x};
    const auto fit = fitMenuBar(available, widths);
    ImGui::SetCursorPosX(fit.transportX);
    const auto action = drawPlaybackToolbar(context, fit.showReadout);
    if (fit.showZoom) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(available - zoomWidth);
        if (ImGui::SmallButton(zoomLabel.c_str()))
            setUiScale(kDefaultUiScalePercent);
        editorTooltip(
            "Current UI scale. Click to reset to 100% (Cmd+0). View > UI Scale has all sizes.");
    }
    switch (action) {
    case PlaybackToolbarAction::Play:
        endMouseLook();
        m_playback.play(m_session, m_settings.followCameraTrack);
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
    }
}

} // namespace lmx::app
