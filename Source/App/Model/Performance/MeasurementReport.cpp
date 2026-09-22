//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementReport.cpp
/// @brief Serializes schema-4 measurement scopes and exact-frame retired diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/LightingDiagnostics.h"
#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/VisibilityDiagnostics.h"
#include "Core/IO/Json.h"

#include <format>

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

} // namespace

//======================================================================================================================
std::string MeasurementRun::json() const {
    const bool complete = m_state == MeasurementState::Complete;
    std::string out = std::format(
        "{{\"schemaVersion\":4,\"complete\":{},\"scored\":{},\"interactive\":{},\"failure\":{},",
        complete, complete && !m_plan.interactive && !m_plan.unscored, m_plan.interactive,
        quote(m_failure));
    out +=
        "\"pacing\":\"serialized-retirement\",\"timingScope\":{\"encodeMs\":\"after beginFrame "
        "through endFrame commit; excludes slot wait and post-submit retirement "
        "wait\",\"slotWaitMs\":\"beginFrame including timing publication\",\"gpuSumMs\":\"sum "
        "of timed passes; excludes presentation, driver and untimed work; not throughput\","
        "\"visibilityGpuMs\":\"sum of matched lmx.pass.visibility.* timings; GPU preparation\","
        "\"hzbGpuMs\":\"sum of lmx.pass.hzb.* reduction/publication timings; excludes debug\","
        "\"lightingGpuMs\":\"sum of exact-frame lmx.pass.light.* timings; separate from scene\"},"
        "\"memoryScope\":{\"listBytes\":\"valid emitted row payload; GPU joined on retirement\","
        "\"reservedListBytes\":\"declaration row reservation; includes rejected GPU slots\","
        "\"allocatedListBytes\":\"active list storage across three slots\"},";
    out += std::format(
        "\"plan\":{{\"warmupFrames\":{},\"measuredFrames\":{},\"width\":{},\"height\":{},"
        "\"labInstances\":{},\"scene\":{},\"temporal\":{},\"submission\":{},\"visibilityEnabled\":{"
        "},\"cameraTrack\":{},\"renderScale\":{},\"stepSeconds\":0.016666666666666666,"
        "\"classify\":{},\"classifyCheck\":{},\"occlusionEnabled\":{},\"occlusionCheck\":{},"
        "\"hzbDebugLevel\":{},\"labOccluders\":{},"
        "\"localLightMode\":{},\"localLightRig\":{},\"labLights\":{},\"labLightPile\":{},"
        "\"lightCheck\":{},\"lightDebugView\":{}}},",
        m_plan.warmupFrames, m_plan.measuredFrames, m_plan.width, m_plan.height,
        m_plan.labInstances, quote(m_plan.scene), quote(m_plan.temporal), quote(m_plan.submission),
        m_plan.visibilityEnabled, m_plan.cameraTrack, m_plan.renderScale, quote(m_plan.classify),
        m_plan.classifyCheck, m_plan.occlusionEnabled, m_plan.occlusionCheck, m_plan.hzbDebugLevel,
        m_plan.labOccluders, quote(m_plan.localLightMode), m_plan.localLightRig, m_plan.labLights,
        m_plan.labLightPile, m_plan.lightCheck, quote(m_plan.lightDebugView));
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
        const uint32_t visible = c.classifyMode == render::ClassifyMode::Gpu && sample.visibility
                                     ? sample.visibility->sceneCounters.emittedRows
                                     : c.visible;
        const uint32_t rejected = c.classifyMode == render::ClassifyMode::Gpu && sample.visibility
                                      ? sample.visibility->sceneCounters.rejected
                                      : c.rejected;
        out += std::format(
            "{{\"ordinal\":{},\"frameId\":{},\"sequenceFrame\":{},\"classifyMs\":{},\"prepareMs\":{"
            "},\"encodeMs\":{},\"slotWaitMs\":{},\"candidates\":{},\"visible\":{},\"rejected\":{},"
            "\"sceneCommands\":{},\"shadowCommands\":{},\"tableBytes\":{},\"listBytes\":{},"
            "\"reservedListBytes\":{},"
            "\"argumentBytes\":{},\"transientBytes\":{},\"retired\":{},\"gpuSumMs\":",
            i, c.frameId, c.sequenceFrame, c.classifyMs, c.prepareMs, c.encodeMs, c.slotWaitMs,
            c.candidates, visible, rejected, c.sceneCommands, c.shadowCommands, c.tableBytes,
            c.listBytes, c.reservedListBytes, c.argumentBytes, c.transientBytes, sample.retired);
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
        double lightingGpuMs = 0;
        for (const auto& pass : sample.passes)
            if (pass.label.starts_with("lmx.pass.light."))
                lightingGpuMs += pass.gpuMilliseconds;
        double visibilityGpuMs = 0;
        double hzbGpuMs = 0;
        for (const auto& pass : sample.passes)
            if (pass.label.starts_with("lmx.pass.visibility."))
                visibilityGpuMs += pass.gpuMilliseconds;
        for (const auto& pass : sample.passes)
            if (pass.label.starts_with("lmx.pass.hzb.") && pass.label != "lmx.pass.hzb.debug")
                hzbGpuMs += pass.gpuMilliseconds;
        double sceneGpuMs = 0;
        double shadowGpuMs = 0;
        for (const auto& pass : sample.passes) {
            if (pass.label.starts_with("lmx.pass.scene"))
                sceneGpuMs += pass.gpuMilliseconds;
            if (pass.label.starts_with("lmx.pass.shadow"))
                shadowGpuMs += pass.gpuMilliseconds;
        }
        out += std::format(",\"hzbGpuMs\":{},\"sceneGpuMs\":{},\"shadowGpuMs\":{}", hzbGpuMs,
                           sceneGpuMs, shadowGpuMs);
        out +=
            std::format(",\"effectiveSubmission\":{},\"effectiveClassify\":{},\"visibilityGpuMs\":{"
                        "},\"allocatedListBytes\":{},"
                        "\"allocatedArgumentBytes\":{},\"candidateBytes\":{},\"runBytes\":{},"
                        "\"chunkBytes\":{},\"stateBytes\":{},\"counterBytes\":{},\"visibility\":{}",
                        quote(m_plan.submission), quote(classifyModeName(c.classifyMode)),
                        visibilityGpuMs, c.allocatedListBytes, c.allocatedArgumentBytes,
                        c.candidateBytes, c.runBytes, c.chunkBytes, c.stateBytes, c.counterBytes,
                        sample.visibility ? visibilityDiagnosticsJson(*sample.visibility) : "null");
        out += std::format(
            ",\"localLightMode\":{},\"liveLightCount\":{},\"lightingGpuMs\":{},\"lighting\":{}",
            quote(localLightModeName(c.lighting.requested)), c.lighting.liveLightCount,
            lightingGpuMs, sample.lighting ? lightingDiagnosticsJson(*sample.lighting) : "null");
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
