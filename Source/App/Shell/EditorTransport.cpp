//----------------------------------------------------------------------------------------------------------------------
/// @file EditorTransport.cpp
/// @brief Connects top-bar playback actions to restorable scene previews and measurements.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/PlaybackToolbar.h"
#include "App/Shell/EditorShell.h"

#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/Workspace/MenuBarFit.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <algorithm>
#include <format>
#include <string>

namespace lmx::app {

namespace {

//======================================================================================================================
std::string measurementReadout(const MeasurementRun& run) {
    const auto& plan = run.plan();
    switch (run.state()) {
    case MeasurementState::Warmup: {
        const auto next = run.nextFrame();
        return std::format("Warmup {} / {}", next ? next->sequenceFrame : plan.warmupFrames,
                           plan.warmupFrames);
    }
    case MeasurementState::Draining:
        return "Finishing";
    default:
        return std::format("Measuring {} / {}", run.samples().size(), plan.measuredFrames);
    }
}

//======================================================================================================================
// Reserves the widest phase text so the centred transport does not shift as the run advances.
float measurementReadoutWidth(const MeasurementPlan& plan) {
    const std::string phases[] = {
        std::format("Warmup {} / {}", plan.warmupFrames, plan.warmupFrames),
        std::format("Measuring {} / {}", plan.measuredFrames, plan.measuredFrames), "Finishing"};
    float width = 0.0f;
    for (const auto& phase : phases)
        width = std::max(width, ImGui::CalcTextSize(phase.c_str()).x);
    return width;
}

} // namespace

//======================================================================================================================
void EditorShell::stopPlayback() {
    if (m_measurement.active())
        m_measurement.cancel();
    m_session.setMeasurementActive(false);
    endMouseLook();
    if (m_playback.stop(m_session, m_settings.followCameraTrack)) {
        requestCameraCut(m_temporalState);
        m_exposureResetPending = true;
    }
    m_measurementOwnsPlayback = false;
}

//======================================================================================================================
void EditorShell::finishMeasurementPlayback() {
    m_session.setMeasurementActive(m_measurement.active());
    if (m_measurementOwnsPlayback && !m_measurement.active())
        stopPlayback();
}

//======================================================================================================================
void EditorShell::buildPlaybackTransport() {
    const auto& scene = m_session.scene();
    const std::string readout = m_measurement.active()
                                    ? measurementReadout(m_measurement)
                                    : std::format("{:.3f} s", scene.animationTime);
    const float readoutWidth = m_measurement.active()
                                   ? measurementReadoutWidth(m_measurement.plan())
                                   : ImGui::CalcTextSize(readout.c_str()).x;
    const PlaybackToolbarContext context{.followCameraRail = m_settings.followCameraTrack,
                                         .playing = m_playback.playing(),
                                         .active = m_playback.active(),
                                         .measurementActive = m_measurement.active(),
                                         .hasCameraRail = !scene.animation.cameraTrack.empty(),
                                         .readout = readout};
    ActivityInputs inputs{.capturePending =
                              m_actions.captureResult().status == ActionStatus::Pending,
                          .controller = m_lastControllerScaleChange,
                          .now = ImGui::GetTime()};
    if (const auto* proposal = m_sessionProposals.pendingFile())
        inputs.session =
            SessionActivity{proposal->state, proposal->summary, {}, false, proposal->actor};
    if (const auto* approval = m_sessionApprovals.active())
        inputs.session = SessionActivity{
            approval->state, approval->summary, {}, approval->state == SessionState::Working};
    if (m_sessionCaptureApproval)
        inputs.session = SessionActivity{SessionState::Working, "GPU capture", {}, true};
    if (m_pendingSessionMeasurementStart)
        inputs.session = SessionActivity{SessionState::Working, "Measurement", {}, true};
    if (m_measurement.active() && !m_sessionMeasurementApproval) {
        const auto next = m_measurement.nextFrame();
        const bool warmup = m_measurement.state() == MeasurementState::Warmup;
        inputs.measure = MeasureProgress{
            m_measurement.state(),
            warmup ? (next ? next->sequenceFrame : m_measurement.plan().warmupFrames)
                   : static_cast<uint32_t>(m_measurement.samples().size()),
            warmup ? m_measurement.plan().warmupFrames : m_measurement.plan().measuredFrames};
    }
    if (m_sessionMeasurementApproval) {
        const auto& plan = m_measurement.plan();
        const auto next = m_measurement.nextFrame();
        const uint32_t done =
            next ? next->sequenceFrame
                 : plan.warmupFrames + static_cast<uint32_t>(m_measurement.samples().size());
        const uint32_t total = plan.warmupFrames + plan.measuredFrames;
        inputs.session =
            SessionActivity{SessionState::Working, "Measurement",
                            total ? std::optional(float(done) / total) : std::nullopt, true};
    }
    switch (m_documentWorkflow.step()) {
    case WorkflowStep::Idle:
        break;
    case WorkflowStep::Confirm:
        inputs.documentWork = "Awaiting document confirmation";
        break;
    case WorkflowStep::ChoosePath:
        inputs.documentWork = "Choosing scene file";
        break;
    case WorkflowStep::Ready:
        switch (*m_documentWorkflow.action()) {
        case DocumentAction::Open:
        case DocumentAction::OpenCatalog:
            inputs.documentWork = "Loading (queued)";
            break;
        case DocumentAction::Save:
        case DocumentAction::SaveAs:
            inputs.documentWork = "Saving (queued)";
            break;
        case DocumentAction::Revert:
            inputs.documentWork = "Reverting (queued)";
            break;
        case DocumentAction::Quit:
            inputs.documentWork = "Closing (queued)";
            break;
        }
        break;
    }
    const auto activity = currentActivity(inputs);
    const std::string zoomLabel = std::to_string(m_workspace.uiScalePercent) + "%##ResetUiZoom";
    const float zoomWidth = ImGui::CalcTextSize(zoomLabel.c_str(), nullptr, true).x +
                            ImGui::GetStyle().FramePadding.x * 2.0f;
    const float available = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // The menu bar's cursor already sits one item spacing past the last menu, which fitMenuBar
    // adds itself as the minimum gap.
    const MenuBarWidths widths{ImGui::GetCursorPosX() - spacing,
                               playbackToolbarButtonsWidth(context),
                               readoutWidth,
                               zoomWidth,
                               spacing,
                               activity ? editor_style::activityStripWidth(*activity) : 0,
                               activity ? editor_style::activityStripWidth(*activity, false) : 0};
    const auto fit = fitMenuBar(available, widths);
    ImGui::SetCursorPosX(fit.transportX);
    const auto action = drawPlaybackToolbar(context, fit.showReadout);
    bool activityStop = false;
    if (activity) {
        ImGui::SameLine(0.0f, spacing);
        activityStop = editor_style::activityStrip(*activity, fit.showActivityVerb);
        if (activity->source == ActivitySource::Session && ImGui::IsItemClicked())
            setPanelVisible(EditorPanel::Session, true);
    }
    if (fit.showZoom) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(available - zoomWidth);
        if (ImGui::SmallButton(zoomLabel.c_str()))
            setUiScale(kDefaultUiScalePercent);
        editorTooltip(
            "Current UI scale. Click to reset to 100% (Cmd+0). View > UI Scale has all sizes.");
    }
    if (activityStop) {
        if (activity && activity->source == ActivitySource::Session && activity->stoppable)
            stopSessionWork();
        else
            stopPlayback();
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
        if (m_sessionMeasurementApproval || m_sessionCaptureApproval ||
            m_pendingSessionMeasurementStart ||
            (m_sessionApprovals.active() &&
             m_sessionApprovals.active()->state == SessionState::Working))
            stopSessionWork();
        else
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
