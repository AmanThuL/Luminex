//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementRun.cpp
/// @brief Implements strict measurement joins and lossless report serialization.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/MeasurementRun.h"

#include "Core/Json.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>

namespace lmx::app {
namespace {

//======================================================================================================================
std::string quote(std::string_view value) {
    return "\"" + jsonEscape(value) + "\"";
}

//======================================================================================================================
std::string pairsJson(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out = "{";
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (i)
            out += ',';
        out += quote(pairs[i].first) + ':' + quote(pairs[i].second);
    }
    return out + '}';
}

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
                                     key.starts_with("DYLD_") || key == "LMX_CAPTURE_AT_FRAME";
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
    m_failure.clear();
    m_submitted = 0;
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
        sample.visible + uint64_t{sample.rejected} != sample.candidates) {
        cancel("Missing, unordered, or invalid CPU measurement frame");
        return false;
    }
    m_lastFrameId = sample.frameId;
    if (next->ordinal)
        m_samples.push_back({.cpu = sample});
    ++m_submitted;
    m_state =
        m_submitted < m_plan.warmupFrames ? MeasurementState::Warmup : MeasurementState::Measuring;
    if (m_submitted == m_plan.warmupFrames + m_plan.measuredFrames)
        m_state = MeasurementState::Draining;
    completeIfReady();
    return true;
}

//======================================================================================================================
bool MeasurementRun::retire(uint64_t frameId, std::span<const rhi::PassTiming> passes) {
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
void MeasurementRun::completeIfReady() {
    if (m_state == MeasurementState::Draining && m_samples.size() == m_plan.measuredFrames &&
        std::ranges::all_of(m_samples, &MeasurementSample::retired))
        m_state = MeasurementState::Complete;
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

//======================================================================================================================
std::string MeasurementRun::json() const {
    const bool complete = m_state == MeasurementState::Complete;
    std::string out = std::format(
        "{{\"schemaVersion\":1,\"complete\":{},\"scored\":{},\"interactive\":{},\"failure\":{},",
        complete, complete && !m_plan.interactive && !m_plan.unscored, m_plan.interactive,
        quote(m_failure));
    out += "\"pacing\":\"serialized-retirement\",\"timingScope\":{\"encodeMs\":\"after beginFrame "
           "through endFrame commit; excludes slot wait and post-submit retirement "
           "wait\",\"slotWaitMs\":\"beginFrame including timing publication\",\"gpuSumMs\":\"sum "
           "of timed passes; excludes presentation, driver and untimed work; not throughput\"},";
    out += std::format(
        "\"plan\":{{\"warmupFrames\":{},\"measuredFrames\":{},\"width\":{},\"height\":{},"
        "\"labInstances\":{},\"scene\":{},\"temporal\":{},\"submission\":{},\"visibilityEnabled\":{"
        "},\"cameraTrack\":{},\"renderScale\":{},\"stepSeconds\":0.016666666666666666}},",
        m_plan.warmupFrames, m_plan.measuredFrames, m_plan.width, m_plan.height,
        m_plan.labInstances, quote(m_plan.scene), quote(m_plan.temporal), quote(m_plan.submission),
        m_plan.visibilityEnabled, m_plan.cameraTrack, m_plan.renderScale);
    out += "\"provenance\":{\"device\":" + quote(m_provenance.device) +
           ",\"os\":" + quote(m_provenance.os) + ",\"buildMode\":" + quote(m_provenance.buildMode) +
           ",\"executableHash\":" + quote(m_provenance.executableHash) +
           ",\"shaderHashes\":" + pairsJson(m_provenance.shaderHashes) +
           ",\"environment\":" + pairsJson(m_provenance.environment) + "},\"samples\":[";
    for (size_t i = 0; i < m_samples.size(); ++i) {
        if (i)
            out += ',';
        const auto& sample = m_samples[i];
        const auto& c = sample.cpu;
        out += std::format(
            "{{\"ordinal\":{},\"frameId\":{},\"sequenceFrame\":{},\"classifyMs\":{},\"prepareMs\":{"
            "},\"encodeMs\":{},\"slotWaitMs\":{},\"candidates\":{},\"visible\":{},\"rejected\":{},"
            "\"sceneCommands\":{},\"shadowCommands\":{},\"tableBytes\":{},\"listBytes\":{},"
            "\"argumentBytes\":{},\"transientBytes\":{},\"retired\":{},\"gpuSumMs\":",
            i, c.frameId, c.sequenceFrame, c.classifyMs, c.prepareMs, c.encodeMs, c.slotWaitMs,
            c.candidates, c.visible, c.rejected, c.sceneCommands, c.shadowCommands, c.tableBytes,
            c.listBytes, c.argumentBytes, c.transientBytes, sample.retired);
        // Actual reconstruction extents may differ from requested scale under vendor clamps.
        double sum = 0;
        for (const auto& pass : sample.passes)
            sum += pass.gpuMilliseconds;
        out += sample.retired ? std::format("{}", sum) : "null";
        out += std::format(
            ",\"renderWidth\":{},\"renderHeight\":{},\"outputWidth\":{},\"outputHeight\":{},"
            "\"effectiveScale\":{},\"effectiveReconstruction\":{},\"vendorFallback\":{}",
            c.renderWidth, c.renderHeight, c.outputWidth, c.outputHeight, c.effectiveScale,
            c.effectiveReconstruction, c.vendorFallback);
        out += ",\"passes\":[";
        for (size_t j = 0; j < sample.passes.size(); ++j) {
            if (j)
                out += ',';
            out += std::format("{{\"label\":{},\"gpuMs\":{}}}", quote(sample.passes[j].label),
                               sample.passes[j].gpuMilliseconds);
        }
        out += "]}";
    }
    return out + "]}\n";
}
} // namespace lmx::app
