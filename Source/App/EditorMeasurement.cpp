//----------------------------------------------------------------------------------------------------------------------
/// @file EditorMeasurement.cpp
/// @brief Connects live editor playback, frame samples, and measurement exports.
//----------------------------------------------------------------------------------------------------------------------
#include "App/EditorShell.h"
#include "App/Measurement.h"
#include "App/Model/LightingDiagnostics.h"
#include "App/Model/VisibilityDiagnostics.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace lmx::app {
namespace {
//======================================================================================================================
std::string submissionName(render::SubmissionMode mode) {
    switch (mode) {
    case render::SubmissionMode::Direct:
        return "direct";
    case render::SubmissionMode::Indirect:
        return "indirect";
    case render::SubmissionMode::Batched:
        return "batched";
    }
    return "unknown";
}
//======================================================================================================================
std::string temporalName(const EditorRenderSettings& settings) {
    if (!settings.temporalEnabled)
        return "off";
    switch (settings.reconstruction) {
    case render::ReconstructionMode::Raw:
        return "raw";
    case render::ReconstructionMode::NativeTaa:
        return "taa";
    case render::ReconstructionMode::VendorTemporal:
        return "metalfx";
    }
    return "unknown";
}
} // namespace
//======================================================================================================================
void EditorShell::startMeasurement(rojoRHI::Device& device, const render::Renderer& renderer) {
    if (m_settings.dynamicResolutionEnabled) {
        m_measurementFeedback =
            "Turn off dynamic resolution before starting a fixed-plan measurement.";
        return;
    }
    MeasurementPlan plan;
    plan.warmupFrames = m_measurementWarmup;
    plan.measuredFrames = m_measurementFrames;
    plan.width = renderer.width();
    plan.height = renderer.height();
    plan.labInstances = m_labInstances;
    plan.labOccluders = m_labOccluders;
    plan.scene = scene::sceneIdString(m_activeSceneId);
    plan.temporal = temporalName(m_settings);
    plan.submission = submissionName(m_settings.submission);
    plan.classify = classifyModeName(m_settings.classifyMode);
    plan.classifyCheck = m_settings.classifyCheck;
    plan.occlusionEnabled = m_settings.occlusionEnabled;
    plan.occlusionCheck = m_settings.occlusionCheck;
    plan.hzbDebugLevel = m_settings.hzbDebugLevel;
    plan.visibilityEnabled = m_settings.visibilityEnabled;
    plan.renderScale = m_settings.renderScale;
    plan.interactive = true;
    plan.unscored = true;
    plan.localLightMode = localLightModeName(m_settings.localLightMode);
    plan.localLightRig = m_session.localLightRigEnabled();
    plan.labLights =
        m_session.lightLabPileAvailable() ? m_session.scene().lightLabGridCount : m_labLights;
    plan.labLightPile =
        m_session.lightLabPileAvailable() ? m_session.lightLabPileCount() : m_labLightPile;
    plan.lightCheck = m_settings.lightCheck;
    plan.lightDebugView = lightDebugViewName(m_settings.lightDebugView);
    if (!m_measurement.start(std::move(plan), collectMeasurementProvenance(device))) {
        m_measurementFeedback = m_measurement.failure();
        return;
    }
    m_playback.play(m_session, m_settings.followCameraTrack);
    m_measurementOwnsPlayback = true;
    m_session.camera() = scene::cameraFromScene(m_session.scene().initialCamera);
    m_session.rewindAnimation();
    requestCameraCut(m_temporalState);
    m_exposureResetPending = true;
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    m_measurementExportPath = (std::filesystem::temp_directory_path() /
                               std::format("luminex-interactive-measurement-{}.json", stamp))
                                  .string();
    m_measurementFeedback.clear();
}
//======================================================================================================================
void EditorShell::retireMeasurement(uint64_t frameId, std::span<const rojoRHI::PassTiming> timings) {
    m_visibilityDisplay.observeTimings(frameId, timings);
    m_lightingDisplay.observeTimings(frameId, timings);
    if (m_measurement.active())
        m_measurement.retire(frameId, timings);
    finishMeasurementPlayback();
}
//======================================================================================================================
void EditorShell::recordMeasurementFrame(uint64_t frameId, double waitMs, double encodeMs,
                                         const render::CompiledFrameRecord& record) {
    const auto next = m_measurement.nextFrame();
    if (!next)
        return;
    if (frameId != record.frameId || frameId != m_measurementVisibility.frameNumber ||
        m_measurement.plan().submission != submissionName(m_settings.submission) ||
        m_measurement.plan().classify != classifyModeName(m_settings.classifyMode) ||
        m_measurement.plan().classifyCheck != m_settings.classifyCheck ||
        m_measurement.plan().occlusionEnabled != m_settings.occlusionEnabled ||
        m_measurement.plan().occlusionCheck != m_settings.occlusionCheck ||
        m_measurement.plan().hzbDebugLevel != m_settings.hzbDebugLevel ||
        m_measurement.plan().localLightMode != localLightModeName(m_settings.localLightMode) ||
        m_measurement.plan().localLightRig != m_session.localLightRigEnabled() ||
        m_measurement.plan().labLights != (m_session.lightLabPileAvailable()
                                               ? m_session.scene().lightLabGridCount
                                               : m_labLights) ||
        m_measurement.plan().labLightPile !=
            (m_session.lightLabPileAvailable() ? m_session.lightLabPileCount() : m_labLightPile) ||
        m_measurement.plan().lightCheck != m_settings.lightCheck ||
        m_measurement.plan().lightDebugView != lightDebugViewName(m_settings.lightDebugView) ||
        m_measurement.plan().temporal != temporalName(m_settings) ||
        m_measurement.plan().renderScale != m_settings.renderScale ||
        m_settings.dynamicResolutionEnabled) {
        m_measurement.cancel("Frame identity or rendering settings changed during measurement");
        return;
    }
    const auto& scene = m_session.scene();
    m_measurement.recordCpu(measurementCpuSample(
        next->sequenceFrame, waitMs, encodeMs, m_measurementVisibility, m_session.tableStats(),
        record, scene.skySphere.has_value() && scene.skyCubemap != nullptr, m_measurementTemporal,
        m_measurementLighting));
}
//======================================================================================================================
void EditorShell::retireMeasurementLighting(const render::LightingStatus& status) {
    if (m_measurement.active())
        m_measurement.retireLighting(status);
    finishMeasurementPlayback();
}
//======================================================================================================================
bool EditorShell::measurementNeedsRetirementWait() const {
    return m_measurement.active();
}
//======================================================================================================================
void EditorShell::exportMeasurement() {
    std::error_code error;
    const std::filesystem::path path(m_measurementExportPath);
    if (path.empty() || std::filesystem::exists(path, error)) {
        m_measurementFeedback = "Choose a new JSON path; existing files are preserved.";
        return;
    }
    std::ofstream output(path);
    output << m_measurement.json();
    output.close();
    m_measurementFeedback =
        output ? "Exported " + path.string() : "Could not write " + path.string();
}
} // namespace lmx::app
