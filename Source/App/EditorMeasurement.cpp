//----------------------------------------------------------------------------------------------------------------------
/// @file EditorMeasurement.cpp
/// @brief Connects live editor playback, frame samples, and measurement exports.
//----------------------------------------------------------------------------------------------------------------------
#include "App/EditorShell.h"
#include "App/Measurement.h"
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
void EditorShell::startMeasurement(rhi::Device& device, const render::Renderer& renderer) {
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
void EditorShell::retireMeasurement(uint64_t frameId, std::span<const rhi::PassTiming> timings) {
    m_visibilityDisplay.observeTimings(frameId, timings);
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
        m_measurement.plan().temporal != temporalName(m_settings) ||
        m_measurement.plan().renderScale != m_settings.renderScale ||
        m_settings.dynamicResolutionEnabled) {
        m_measurement.cancel("Frame identity or rendering settings changed during measurement");
        return;
    }
    const auto& scene = m_session.scene();
    m_measurement.recordCpu(measurementCpuSample(
        next->sequenceFrame, waitMs, encodeMs, m_measurementVisibility, m_session.tableStats(),
        record, scene.skySphere.has_value() && scene.skyCubemap != nullptr, m_measurementTemporal));
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
