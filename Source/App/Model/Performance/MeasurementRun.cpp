//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementRun.cpp
/// @brief Implements strict measurement joins and lossless report serialization.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Performance/MeasurementRun.h"

#include "App/Model/LightingDiagnostics.h"
#include "App/Model/VisibilityDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>

namespace lmx::app {
namespace {

//======================================================================================================================
bool validTime(double value) {
    return std::isfinite(value) && value >= 0;
}
} // namespace

//======================================================================================================================
bool measurementEnvironmentInstrumented(const MeasurementProvenance& provenance) {
    return std::ranges::any_of(provenance.environment, [](const auto& entry) {
        const auto& [key, value] = entry;
        const bool instrumentation = key.starts_with("MTL_") || key.starts_with("METAL_") ||
                                     key.starts_with("DYLD_") || key == "LMX_CAPTURE_AT_FRAME" ||
                                     key == "LMX_LIGHT_CHECK_DUMP";
        return instrumentation && !value.empty() && value != "0";
    });
}

//======================================================================================================================
bool MeasurementRun::start(MeasurementPlan plan, MeasurementProvenance provenance) {
    if (active())
        return false;
    m_state = MeasurementState::Idle;
    m_plan = std::move(plan);
    m_provenance = std::move(provenance);
    m_samples.clear();
    m_lightingDeclarations.clear();
    m_failure.clear();
    m_referenceFailure.clear();
    m_submitted = 0;
    m_firstFrameId = 0;
    m_lastFrameId = 0;
    if (m_plan.measuredFrames == 0 || m_plan.width == 0 || m_plan.height == 0 ||
        uint64_t{m_plan.warmupFrames} + m_plan.measuredFrames >
            std::numeric_limits<uint32_t>::max() ||
        !std::isfinite(m_plan.renderScale) || m_plan.renderScale < 0.5f ||
        m_plan.renderScale > 1.0f) {
        cancel("Invalid measurement frame plan");
        return false;
    }
    if (!m_plan.interactive && !m_plan.unscored &&
        measurementEnvironmentInstrumented(m_provenance)) {
        cancel("Scored measurement refuses validation/capture instrumentation; use --unscored");
        return false;
    }
    if (m_plan.classify != "cpu" && m_plan.classify != "gpu") {
        cancel("Unknown measurement classifier");
        return false;
    }
    if ((m_plan.classify == "gpu" && m_plan.submission == "direct") ||
        (m_plan.classifyCheck && m_plan.classify != "gpu") ||
        (m_plan.classifyCheck && !m_plan.interactive && !m_plan.unscored)) {
        cancel("Invalid classifier/submission or scored check-mode plan");
        return false;
    }
    if ((m_plan.occlusionEnabled && (m_plan.classify != "gpu" || !m_plan.visibilityEnabled)) ||
        (m_plan.occlusionCheck && !m_plan.occlusionEnabled) ||
        (m_plan.hzbDebugLevel >= 0 && !m_plan.occlusionEnabled) ||
        ((m_plan.occlusionCheck || m_plan.hzbDebugLevel >= 0) && !m_plan.interactive &&
         !m_plan.unscored)) {
        cancel("Invalid occlusion settings or scored diagnostic-mode plan");
        return false;
    }
    const bool validLightMode = m_plan.localLightMode == "off" ||
                                m_plan.localLightMode == "direct" ||
                                m_plan.localLightMode == "clustered";
    const bool validLightView =
        m_plan.lightDebugView == "off" || m_plan.lightDebugView == "count" ||
        m_plan.lightDebugView == "overflow" || m_plan.lightDebugView == "missed";
    const bool diagnosticLighting = m_plan.lightCheck || m_plan.lightDebugView != "off";
    if (!validLightMode || !validLightView ||
        (diagnosticLighting && m_plan.localLightMode != "clustered") ||
        (diagnosticLighting && !m_plan.interactive && !m_plan.unscored) ||
        (m_plan.localLightRig && m_plan.scene != "sponza") ||
        (m_plan.scene == "light-lab" &&
         (m_plan.labLights == 0 ||
          uint64_t{m_plan.labLights} + m_plan.labLightPile > engine::kMaxLocalLights))) {
        cancel("Invalid local-light plan or scored lighting diagnostics");
        return false;
    }
    const auto validHash = [](std::string_view hash) {
        return hash.size() == 64 && std::ranges::all_of(hash, [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    };
    if (!m_plan.interactive && !m_plan.unscored &&
        (m_provenance.device.empty() || m_provenance.os.empty() || m_provenance.buildMode.empty() ||
         !validHash(m_provenance.executableHash) || m_provenance.shaderHashes.empty() ||
         std::ranges::any_of(m_provenance.shaderHashes, [&](const auto& entry) {
             return entry.first.empty() || !validHash(entry.second);
         }))) {
        cancel("Scored measurement requires complete executable and shader provenance");
        return false;
    }
    m_state = m_plan.warmupFrames ? MeasurementState::Warmup : MeasurementState::Measuring;
    return true;
}

//======================================================================================================================
bool MeasurementRun::active() const {
    return m_state == MeasurementState::Warmup || m_state == MeasurementState::Measuring ||
           m_state == MeasurementState::Draining;
}

//======================================================================================================================
std::optional<MeasurementFramePlan> MeasurementRun::nextFrame() const {
    if (m_state != MeasurementState::Warmup && m_state != MeasurementState::Measuring)
        return {};
    MeasurementFramePlan result{.sequenceFrame = m_submitted};
    if (m_submitted >= m_plan.warmupFrames)
        result.ordinal = m_submitted - m_plan.warmupFrames;
    return result;
}

//======================================================================================================================
bool MeasurementRun::recordCpu(MeasurementCpuSample sample) {
    const auto next = nextFrame();
    if (!next || sample.sequenceFrame != next->sequenceFrame || sample.frameId <= m_lastFrameId ||
        (m_lastFrameId != 0 && sample.frameId != m_lastFrameId + 1) ||
        !validTime(sample.classifyMs) || !validTime(sample.prepareMs) ||
        !validTime(sample.encodeMs) || !validTime(sample.slotWaitMs) ||
        classifyModeName(sample.classifyMode) != m_plan.classify ||
        (sample.classifyMode == render::ClassifyMode::Cpu &&
         sample.visible + uint64_t{sample.rejected} != sample.candidates)) {
        cancel("Missing, unordered, or invalid CPU measurement frame");
        return false;
    }
    const auto& lighting = sample.lighting;
    const auto effective =
        lighting.liveLightCount ? lighting.requested : engine::LocalLightMode::Off;
    if (lighting.frameNumber != sample.frameId || lighting.isRetired ||
        localLightModeName(lighting.requested) != m_plan.localLightMode ||
        lighting.effective != effective || lighting.checkEnabled != m_plan.lightCheck ||
        lighting.liveLightCount > engine::kMaxLocalLights ||
        (!m_lightingDeclarations.empty() &&
         (m_lightingDeclarations.front().sceneGeneration != lighting.sceneGeneration ||
          m_lightingDeclarations.front().liveLightCount != lighting.liveLightCount)) ||
        (m_plan.scene == "light-lab" && !m_plan.interactive &&
         lighting.liveLightCount != m_plan.labLights + m_plan.labLightPile) ||
        (m_plan.localLightRig && (lighting.liveLightCount == 0 || lighting.liveLightCount > 32))) {
        cancel("Lighting declaration differs from the submitted frame or frozen plan");
        return false;
    }
    if (m_firstFrameId == 0)
        m_firstFrameId = sample.frameId;
    m_lastFrameId = sample.frameId;
    if (sample.vendorFallback != 0) {
        cancel("Requested reconstruction fell back during measurement");
        return false;
    }
    m_lightingDeclarations.push_back(lighting);
    m_lightingDeclarations.back().checkFrame.reset();
    if (next->ordinal) {
        sample.lighting.checkFrame.reset();
        m_samples.push_back({.cpu = sample});
        if (sample.classifyMode == render::ClassifyMode::Cpu) {
            render::VisibilityStatus status;
            status.frameNumber = sample.frameId;
            status.sceneCounters = sample.sceneCounters;
            status.shadowCounters = sample.shadowCounters;
            m_samples.back().visibility = std::move(status);
        }
    }
    ++m_submitted;
    m_state =
        m_submitted < m_plan.warmupFrames ? MeasurementState::Warmup : MeasurementState::Measuring;
    if (m_submitted == m_plan.warmupFrames + m_plan.measuredFrames)
        m_state = MeasurementState::Draining;
    completeIfReady();
    return true;
}

//======================================================================================================================
bool MeasurementRun::retire(uint64_t frameId, std::span<const rojoRHI::PassTiming> passes) {
    if (!active())
        return false;
    if (m_lastFrameId != 0 && frameId > m_lastFrameId) {
        cancel("GPU timing publication has no matching submitted measurement frame");
        return false;
    }
    const auto found = std::ranges::find_if(
        m_samples, [frameId](const auto& sample) { return sample.cpu.frameId == frameId; });
    if (found == m_samples.end())
        return true;
    if (passes.empty() || std::ranges::any_of(passes, [](const auto& pass) {
            return pass.label.empty() || !validTime(pass.gpuMilliseconds);
        })) {
        cancel("Measured frame has missing or invalid GPU pass timings");
        return false;
    }
    if (!found->cpu.expectedPasses.empty() &&
        (passes.size() != found->cpu.expectedPasses.size() ||
         !std::equal(
             passes.begin(), passes.end(), found->cpu.expectedPasses.begin(),
             [](const auto& pass, const auto& expected) { return pass.label == expected; }))) {
        cancel("Retired GPU pass inventory differs from the declared measurement frame");
        return false;
    }
    if (found->retired) {
        if (found->passes.size() != passes.size() ||
            !std::equal(passes.begin(), passes.end(), found->passes.begin(),
                        [](const auto& a, const auto& b) {
                            return a.label == b.label && a.gpuMilliseconds == b.gpuMilliseconds;
                        })) {
            cancel("Conflicting duplicate GPU timing publication");
            return false;
        }
        return true;
    }
    found->passes.assign(passes.begin(), passes.end());
    found->retired = true;
    completeIfReady();
    return true;
}

//======================================================================================================================
bool MeasurementRun::retireVisibility(const render::VisibilityStatus& status) {
    if (!active())
        return false;
    if (m_firstFrameId == 0 || status.frameNumber < m_firstFrameId)
        return true;
    if (status.frameNumber > m_lastFrameId) {
        cancel("GPU visibility publication has no matching submitted measurement frame");
        return false;
    }
    const auto found = std::ranges::find_if(
        m_samples, [&](const auto& sample) { return sample.cpu.frameId == status.frameNumber; });
    if (found == m_samples.end()) {
        if (status.occlusionCheckEnabled && !status.occlusionCheck.passed() &&
            m_referenceFailure.empty())
            m_referenceFailure = visibilityFailure(status);
        if (const auto failure = visibilityFailure(status, false); !failure.empty()) {
            cancel(failure);
            return false;
        }
        return true;
    }
    if (status.classifyMode != found->cpu.classifyMode || !status.isRetired ||
        status.checkEnabled != m_plan.classifyCheck ||
        status.occlusionEnabled != m_plan.occlusionEnabled ||
        status.occlusionCheckEnabled != m_plan.occlusionCheck) {
        cancel("GPU visibility publication differs from the declared classifier");
        return false;
    }
    if (status.occlusionCheckEnabled &&
        (!status.occlusionCheck.enabled || !status.occlusionCheck.passed()) &&
        m_referenceFailure.empty())
        m_referenceFailure = visibilityFailure(status);
    if (const auto failure = visibilityFailure(status, false); !failure.empty()) {
        found->visibility = status;
        cancel(failure);
        return false;
    }
    if (status.sceneCounters.candidates != found->cpu.candidates) {
        cancel("GPU visibility candidate count differs from declaration");
        return false;
    }
    if (found->visibility &&
        visibilityDiagnosticsJson(*found->visibility) != visibilityDiagnosticsJson(status)) {
        cancel("Conflicting duplicate GPU visibility publication");
        return false;
    }
    found->visibility = status;
    found->cpu.listBytes = status.submission.listBytes;
    // Reports need counters, not potentially million-entry diagnostic arrays.
    found->visibility->scene = {};
    found->visibility->shadow = {};
    completeIfReady();
    return true;
}

//======================================================================================================================
bool MeasurementRun::retireLighting(const render::LightingStatus& status) {
    if (!active())
        return false;
    if (m_firstFrameId == 0 || status.frameNumber < m_firstFrameId)
        return true;
    const auto declared = std::ranges::find_if(m_lightingDeclarations, [&](const auto& value) {
        return value.frameNumber == status.frameNumber;
    });
    if (declared == m_lightingDeclarations.end()) {
        cancel("Lighting publication has no matching submitted measurement frame");
        return false;
    }
    const auto found = std::ranges::find_if(
        m_samples, [&](const auto& sample) { return sample.cpu.frameId == status.frameNumber; });
    if (found != m_samples.end() && found->lighting &&
        lightingDiagnosticsJson(*found->lighting) != lightingDiagnosticsJson(status)) {
        cancel("Conflicting duplicate lighting publication");
        return false;
    }
    if (found != m_samples.end()) {
        found->lighting = status;
        found->lighting->checkFrame.reset();
    }
    const auto& c = status.counters;
    const bool clusterWork = status.effective == engine::LocalLightMode::Clustered;
    if (!status.isRetired || status.sceneGeneration != declared->sceneGeneration ||
        status.requested != declared->requested || status.effective != declared->effective ||
        status.liveLightCount != declared->liveLightCount ||
        status.checkEnabled != declared->checkEnabled ||
        uint64_t{c.assigned} + c.droppedPerCluster + c.droppedGlobal != c.candidates ||
        c.assigned > render::kLightClusterIndexCapacity ||
        c.maxCount > render::kMaxLightsPerCluster || c.truncatedFroxels > render::kClusterCount ||
        c.candidates > uint64_t{status.liveLightCount} * render::kClusterCount ||
        status.listBytes != uint64_t{c.assigned} * sizeof(uint32_t) ||
        status.allocatedListBytes < status.listBytes ||
        (!clusterWork && (c.candidates || c.maxCount || c.truncatedFroxels || status.listBytes)) ||
        (!status.checkEnabled && !status.check.passed())) {
        cancel("Lighting retirement differs from declaration or has invalid counters/list bytes");
        return false;
    }
    if (!status.checkPassed() && m_referenceFailure.empty())
        m_referenceFailure = lightingFailure(status);
    completeIfReady();
    return true;
}

//======================================================================================================================
void MeasurementRun::completeIfReady() {
    if (m_state == MeasurementState::Draining && m_samples.size() == m_plan.measuredFrames &&
        std::ranges::all_of(m_samples, [](const auto& sample) {
            return sample.retired &&
                   (sample.cpu.classifyMode == render::ClassifyMode::Cpu ||
                    sample.visibility.has_value()) &&
                   sample.lighting.has_value();
        })) {
        if (m_referenceFailure.empty())
            m_state = MeasurementState::Complete;
        else
            cancel(m_referenceFailure);
    }
}

//======================================================================================================================
bool MeasurementRun::finishDrain() {
    if (m_state == MeasurementState::Complete)
        return true;
    if (active())
        cancel("GPU retirement finished with missing measurement frames");
    return false;
}

//======================================================================================================================
void MeasurementRun::cancel(std::string reason) {
    if (m_state == MeasurementState::Cancelled)
        return;
    m_failure = std::move(reason);
    m_state = MeasurementState::Cancelled;
}

} // namespace lmx::app
