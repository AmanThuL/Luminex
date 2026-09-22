//----------------------------------------------------------------------------------------------------------------------
/// @file Measurement.cpp
/// @brief Implements deterministic offscreen measurement with explicit retirement pacing.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Headless/Measurement.h"

#include "App/Headless/OcclusionValidation.h"
#include "App/Model/LightingDiagnostics.h"
#include "App/Model/SceneDefaults.h"
#include "App/Model/SceneSession.h"
#include "App/Model/VisibilityDiagnostics.h"
#include "Core/Diagnostics/Log.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Render/Graph/FrameDeclaration.h"
#include "Render/Renderer/Renderer.h"

#include <algorithm>
#include <chrono>
#include <crt_externs.h>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mach-o/dyld.h>
#include <sys/utsname.h>

namespace lmx::app {
namespace {
using Clock = std::chrono::steady_clock;
//======================================================================================================================
double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

//======================================================================================================================
std::string hashFile(const std::filesystem::path& path) {
    const auto bytes = readWholeFile(path);
    return bytes ? lmx::sha256Hex(*bytes) : "unavailable";
}

//======================================================================================================================
bool writeReport(const std::filesystem::path& path, const MeasurementRun& run) {
    std::ofstream stream(path, std::ios::trunc);
    stream << run.json();
    stream.close();
    return static_cast<bool>(stream);
}
} // namespace
//======================================================================================================================
MeasurementProvenance collectMeasurementProvenance(const rojoRHI::Device& device) {
    MeasurementProvenance result;
    result.device = device.deviceName();
    utsname info{};
    if (uname(&info) == 0)
        result.os =
            std::format("{} {} {} {}", info.sysname, info.release, info.version, info.machine);
#ifdef NDEBUG
    result.buildMode = "release";
#else
    result.buildMode = "debug";
#endif
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> executable(size);
    if (_NSGetExecutablePath(executable.data(), &size) == 0)
        result.executableHash = hashFile(executable.data());
    std::error_code error;
    const std::filesystem::path shaderRoot = "Shaders";
    for (const auto& file : std::filesystem::recursive_directory_iterator(shaderRoot, error)) {
        if (file.is_regular_file() &&
            (file.path().extension() == ".metal" || file.path().extension() == ".metallib")) {
            result.shaderHashes.emplace_back(file.path().generic_string(), hashFile(file.path()));
        }
    }
    std::ranges::sort(result.shaderHashes);
    for (char** entry = *_NSGetEnviron(); *entry != nullptr; ++entry) {
        const std::string_view text(*entry);
        const size_t equals = text.find('=');
        const auto key = text.substr(0, equals);
        if (key.starts_with("MTL_") || key.starts_with("METAL_") || key.starts_with("DYLD_") ||
            key.starts_with("LMX_")) {
            result.environment.emplace_back(
                key, equals == std::string_view::npos ? "" : text.substr(equals + 1));
        }
    }
    for (std::string_view key : {"MTL_DEBUG_LAYER", "MTL_CAPTURE_ENABLED", "MTL_SHADER_VALIDATION",
                                 "LMX_CAPTURE_AT_FRAME"}) {
        if (std::ranges::none_of(result.environment,
                                 [key](const auto& entry) { return entry.first == key; })) {
            result.environment.emplace_back(key, "");
        }
    }
    std::ranges::sort(result.environment);
    return result;
}

//======================================================================================================================
uint64_t measurementTableBytes(const engine::SceneTableStats& stats) {
    return 3 * (uint64_t{stats.instanceCapacity} * sizeof(engine::InstanceRow) +
                uint64_t{stats.meshCapacity} * sizeof(engine::MeshRow) +
                uint64_t{stats.materialCapacity} * sizeof(engine::MaterialRow) +
                uint64_t{stats.lightCapacity} * sizeof(engine::LightRow));
}

//======================================================================================================================
MeasurementCpuSample measurementCpuSample(uint32_t sequenceFrame, double waitMs, double encodeMs,
                                          const render::VisibilityStatus& visibility,
                                          const engine::SceneTableStats& tables,
                                          const render::CompiledFrameRecord& record, bool hasSky,
                                          const render::TemporalStatus& temporal,
                                          const render::LightingStatus& lighting) {
    MeasurementCpuSample sample{
        .frameId = record.frameId,
        .sequenceFrame = sequenceFrame,
        .classifyMs = visibility.classifyMs,
        .prepareMs = visibility.prepareMs,
        .encodeMs = encodeMs,
        .slotWaitMs = waitMs,
        .renderWidth = temporal.extents.renderWidth,
        .renderHeight = temporal.extents.renderHeight,
        .outputWidth = temporal.extents.outputWidth,
        .outputHeight = temporal.extents.outputHeight,
        .effectiveScale = temporal.renderScale,
        .effectiveReconstruction = static_cast<uint32_t>(temporal.reconstruction),
        .vendorFallback = static_cast<uint32_t>(temporal.vendorFallback),
        .candidates = static_cast<uint32_t>(visibility.scene.candidates.size()),
        .visible = static_cast<uint32_t>(visibility.scene.visibleItems.size()),
        .rejected = visibility.scene.rejected,
        .sceneCommands = visibility.submission.sceneCommands + (hasSky ? 1u : 0u),
        .shadowCommands = visibility.submission.shadowCommands,
        .tableBytes = measurementTableBytes(tables),
        .reservedListBytes = visibility.submission.listBytes,
        .listBytes = visibility.submission.listBytes,
        .argumentBytes = visibility.submission.argumentBytes,
        .allocatedListBytes = visibility.submission.allocatedListBytes,
        .allocatedArgumentBytes = visibility.submission.allocatedArgumentBytes,
        .candidateBytes = visibility.submission.candidateBytes,
        .runBytes = visibility.submission.runBytes,
        .chunkBytes = visibility.submission.chunkBytes,
        .stateBytes = visibility.submission.stateBytes,
        .counterBytes = visibility.submission.counterBytes,
        .classifyMode = visibility.classifyMode,
        .sceneCounters = visibility.sceneCounters,
        .shadowCounters = visibility.shadowCounters,
        .transientBytes = record.debug.memory.highWater,
        .lighting = lighting};
    for (uint32_t index : record.debug.schedule.passes)
        sample.expectedPasses.push_back(record.debug.passes[index].label);
    return sample;
}

//======================================================================================================================
int runMeasurement(const AppOptions& options) {
    if (std::filesystem::exists(options.measurementPath)) {
        LMX_LOG_ERROR("measurement output already exists: {}", options.measurementPath.string());
        return 1;
    }
    auto device = rojoRHI::createDevice({.enableValidation = false});
    if (!device) {
        LMX_LOG_ERROR("measurement device: {}", device.error().message);
        return 1;
    }
    MeasurementPlan plan;
    plan.warmupFrames = options.warmup;
    plan.measuredFrames = options.frames;
    plan.scene = scenes::sceneIdString(options.initialScene);
    const auto scaleStep =
        readOcclusionScaleStep(options.unscored, options.temporal != TemporalMode::Off);
    if (!scaleStep) {
        LMX_LOG_ERROR("{}", scaleStep.error());
        return 1;
    }
    plan.labInstances = options.labInstances;
    plan.labOccluders = options.labOccluders;
    plan.visibilityEnabled = options.visibilityEnabled;
    plan.classify = classifyModeName(options.classifyMode);
    plan.classifyCheck = options.classifyCheck;
    plan.occlusionEnabled = options.occlusionEnabled;
    plan.occlusionCheck = options.occlusionCheck;
    plan.hzbDebugLevel = options.hzbDebugLevel;
    plan.submission = options.submission == render::SubmissionMode::Direct    ? "direct"
                      : options.submission == render::SubmissionMode::Batched ? "batched"
                                                                              : "indirect";
    plan.temporal = options.temporal == TemporalMode::Off      ? "off"
                    : options.temporal == TemporalMode::Raw    ? "raw"
                    : options.temporal == TemporalMode::Vendor ? "metalfx"
                                                               : "taa";
    plan.renderScale = options.renderScale;
    plan.cameraTrack = options.measurementTrack;
    plan.unscored = options.unscored;
    plan.localLightMode = localLightModeName(options.localLightMode);
    plan.localLightRig = options.localLightRig;
    plan.labLights = options.labLights;
    plan.labLightPile = options.labLightPile;
    plan.lightCheck = options.lightCheck;
    plan.lightDebugView = lightDebugViewName(options.lightDebugView);
    MeasurementRun run;
    if (!run.start(plan, collectMeasurementProvenance(**device))) {
        writeReport(options.measurementPath, run);
        LMX_LOG_ERROR("{}", run.failure());
        return 1;
    }
    scenes::SceneLibrary library(**device, options.labInstances, options.labOccluders,
                                 options.labLights, options.labLightPile);
    auto loaded = library.get(options.initialScene);
    if (!loaded) {
        run.cancel(loaded.error().message);
        writeReport(options.measurementPath, run);
        return 1;
    }
    SceneSession session;
    session.activate(**loaded, SceneActivationMotion::PreserveLoadedMotion);
    if (session.localLightRigAvailable()) {
        if (auto rig = session.setLocalLightRig(options.localLightRig); !rig) {
            run.cancel(rig.error().message);
            writeReport(options.measurementPath, run);
            return 1;
        }
    }
    render::TransientPool pool(**device);
    auto renderer = render::Renderer::create(**device, plan.width, plan.height);
    if (!renderer) {
        run.cancel(renderer.error().message);
        writeReport(options.measurementPath, run);
        return 1;
    }
    std::fill_n((*renderer)->clearColor, 3, kSceneClearGray);
    (*renderer)->clearColor[3] = 1;
    while (run.nextFrame()) {
        const auto frame = *run.nextFrame();
        session.prepareSequenceFrame(frame.sequenceFrame);
        if (!plan.cameraTrack)
            session.camera() = engine::cameraFromScene((*loaded)->initialCamera);
        const auto waitStart = Clock::now();
        auto& commands = (*device)->beginFrame();
        const auto encodeStart = Clock::now();
        if (!run.retire((*device)->passTimingsFrame(), (*device)->passTimings())) {
            (*device)->endFrame(nullptr);
            break;
        }
        if (auto prepared = session.prepareFrame((*device)->frameNumber()); !prepared) {
            run.cancel(prepared.error().message);
            (*device)->endFrame(nullptr);
            break;
        }
        std::vector<engine::DrawItem> items;
        auto view = session.view(items, render::ShadowFilter::PCF, false);
        view.localLightMode = options.localLightMode;
        view.lightCheck = options.lightCheck;
        view.lightDebugView = options.lightDebugView;
        view.visibilityEnabled = options.visibilityEnabled;
        view.submission = options.submission;
        view.classifyMode = options.classifyMode;
        view.classifyCheck = options.classifyCheck;
        view.occlusionEnabled = options.occlusionEnabled;
        view.occlusionCheck = options.occlusionCheck;
        view.hzbDebugLevel = options.hzbDebugLevel;
        view.temporal.enabled = options.temporal != TemporalMode::Off;
        view.temporal.jitterEnabled = view.temporal.enabled;
        view.temporal.reconstruction = temporalReconstructionMode(options.temporal);
        view.temporal.debugView = options.temporalView;
        view.temporal.renderScale = *scaleStep && frame.sequenceFrame >= (**scaleStep).frame
                                        ? (**scaleStep).scale
                                        : options.renderScale;
        render::FrameDeclaration declaration(pool, **renderer, commands, session.camera(), view,
                                             true);
        declaration.graph().exportTexture(declaration.displayColor());
        auto record = declaration.execute();
        session.commitFrame();
        (*device)->endFrame(nullptr);
        const auto encodeEnd = Clock::now();
        run.recordCpu(
            measurementCpuSample(frame.sequenceFrame, elapsedMs(waitStart, encodeStart),
                                 elapsedMs(encodeStart, encodeEnd), (*renderer)->visibilityStatus(),
                                 session.tableStats(), record, view.skySphere.has_value(),
                                 (*renderer)->temporalStatus(), (*renderer)->lightingStatus()));
        if (options.temporal == TemporalMode::Vendor &&
            (*renderer)->temporalStatus().vendorFallback != render::VendorFallback::None) {
            run.cancel("Requested vendor reconstruction fell back during measurement");
        }
        // The RHI publishes only its newest retired frame. Explicit serialization is required to
        // preserve every frame's timestamps; report this pacing, and exclude it from CPU encoding.
        (*device)->waitIdle();
        (*renderer)->drainVisibilityAfterIdle();
        (*renderer)->drainLightingAfterIdle();
        for (const auto& status : (*renderer)->takeRetiredLighting())
            if (run.active())
                run.retireLighting(status);
        for (const auto& status : (*renderer)->takeRetiredVisibility())
            if (run.active())
                run.retireVisibility(status);
    }
    if (run.state() == MeasurementState::Draining) {
        (*device)->beginFrame();
        run.retire((*device)->passTimingsFrame(), (*device)->passTimings());
        (*device)->endFrame(nullptr);
        run.finishDrain();
    }
    (*device)->waitIdle();
    if (!writeReport(options.measurementPath, run)) {
        LMX_LOG_ERROR("could not write measurement report: {}", options.measurementPath.string());
        return 1;
    }
    if (run.state() != MeasurementState::Complete) {
        LMX_LOG_ERROR("measurement incomplete: {}", run.failure());
        return 1;
    }
    LMX_LOG_INFO("measurement complete: {} samples, {}", run.samples().size(),
                 options.measurementPath.string());
    return 0;
}
} // namespace lmx::app
