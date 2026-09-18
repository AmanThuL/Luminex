//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDiagnostics.cpp
/// @brief Serializes visibility counters and identifies invalid capture evidence.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/VisibilityDiagnostics.h"

#include <format>

namespace lmx::app {
namespace {
//======================================================================================================================
std::string countersJson(const render::VisibilityCounters& c) {
    return std::format(
        "{{\"candidates\":{},\"visible\":{},\"rejected\":{},\"bypassed\":[{},{},{},{}],"
        "\"emittedRows\":{},\"emittedCommands\":{},\"overflowedRows\":{},\"overflowedCommands\":{},"
        "\"occluded\":{},\"occlusionTested\":{},\"historyInvalid\":{},\"nearCrossing\":{},"
        "\"outsideSource\":{},\"rectTooLarge\":{}}}",
        c.candidates, c.visible, c.rejected, c.bypassed[1], c.bypassed[2], c.bypassed[3],
        c.bypassed[4], c.emittedRows, c.emittedCommands, c.overflowedRows, c.overflowedCommands,
        c.occluded, c.occlusionTested, c.historyInvalid, c.nearCrossing, c.outsideSource,
        c.rectTooLarge);
}
} // namespace
//======================================================================================================================
std::string_view classifyModeName(render::ClassifyMode mode) {
    return mode == render::ClassifyMode::Gpu ? "gpu" : "cpu";
}
//======================================================================================================================
std::string visibilityDiagnosticsJson(const render::VisibilityStatus& s) {
    const bool available = s.classifyMode == render::ClassifyMode::Cpu || s.isRetired;
    std::string out = std::format(
        "{{\"frameId\":{},\"classify\":\"{}\",\"retired\":{},\"overflow\":{},"
        "\"checkEnabled\":{},\"stateMismatches\":{},\"rowMismatches\":{},"
        "\"argumentMismatches\":{},\"counterMismatches\":{},\"scene\":{},\"shadow\":{},"
        "\"occlusionEnabled\":{},\"occlusionCheckEnabled\":{},\"occlusionHistoryValid\":{},"
        "\"occlusionInvalidReason\":\"{}\",\"occlusionSourceFrame\":{},\"pyramidBytes\":{}",
        s.frameNumber, classifyModeName(s.classifyMode), s.isRetired, s.overflow, s.checkEnabled,
        s.stateMismatches, s.rowMismatches, s.argumentMismatches, s.counterMismatches,
        available ? countersJson(s.sceneCounters) : "null",
        available ? countersJson(s.shadowCounters) : "null", s.occlusionEnabled,
        s.occlusionCheckEnabled, s.occlusionInvalidReason == render::OcclusionInvalidReason::None,
        render::occlusionInvalidReasonName(s.occlusionInvalidReason), s.occlusionSourceFrame,
        s.pyramidBytes);
    const auto& check = s.occlusionCheck;
    out += std::format(
        ",\"occlusionCheck\":{{\"enabled\":{},\"strict\":{},\"passed\":{},"
        "\"visibleInstances\":{},\"falselyRejectedInstances\":{},\"falselyRejectedPixels\":{},"
        "\"invalidReferencePixels\":{},\"unmatchedCandidates\":{},\"maximumMissingStreak\":{},"
        "\"missing\":[",
        check.enabled, check.strict, check.passed(), check.visibleInstances,
        check.falselyRejectedInstances, check.falselyRejectedPixels, check.invalidReferencePixels,
        check.unmatchedCandidates, check.maximumMissingStreak);
    for (size_t i = 0; i < check.missing.size(); ++i) {
        if (i)
            out += ',';
        const auto& missing = check.missing[i];
        out += std::format("{{\"identity\":{},\"row\":{},\"pixels\":{},\"streak\":{}}}",
                           missing.instanceIdentity, missing.instanceRow, missing.visiblePixels,
                           missing.consecutiveFrames);
    }
    return out + "]}}";
}
//======================================================================================================================
std::string visibilityFailure(const render::VisibilityStatus& s, bool includeOcclusionReference) {
    if (s.classifyMode == render::ClassifyMode::Gpu && !s.isRetired)
        return std::format("frame {}: GPU visibility result has not retired", s.frameNumber);
    if (s.overflow || s.sceneCounters.overflowedRows || s.sceneCounters.overflowedCommands ||
        s.shadowCounters.overflowedRows || s.shadowCounters.overflowedCommands)
        return std::format(
            "frame {}: visibility overflow (scene rows/commands {}/{}, shadow {}/{})",
            s.frameNumber, s.sceneCounters.overflowedRows, s.sceneCounters.overflowedCommands,
            s.shadowCounters.overflowedRows, s.shadowCounters.overflowedCommands);
    const auto reconciles = [](const render::VisibilityCounters& c) {
        const uint64_t retained =
            uint64_t{c.visible} + c.bypassed[1] + c.bypassed[2] + c.bypassed[3] + c.bypassed[4];
        return c.bypassed[0] == 0 && retained + c.rejected == c.candidates &&
               retained == uint64_t{c.emittedRows} + c.overflowedRows && c.occluded <= c.rejected &&
               uint64_t{c.occluded} + c.nearCrossing + c.outsideSource + c.rectTooLarge <=
                   c.occlusionTested;
    };
    if (!reconciles(s.sceneCounters) || !reconciles(s.shadowCounters))
        return std::format("frame {}: visibility counters do not reconcile", s.frameNumber);
    const auto& check = s.occlusionCheck;
    if (includeOcclusionReference && s.occlusionCheckEnabled && (!check.enabled || !check.passed()))
        return std::format(
            "frame {}: occlusion reference failed (strict {}, missing instances {}, pixels {}, "
            "streak {}, invalid IDs {}, unmatched {})",
            s.frameNumber, check.strict, check.falselyRejectedInstances,
            check.falselyRejectedPixels, check.maximumMissingStreak, check.invalidReferencePixels,
            check.unmatchedCandidates);
    if (!s.checkPassed())
        return std::format(
            "frame {}: visibility check mismatches states {}, rows {}, arguments {}, counters {}",
            s.frameNumber, s.stateMismatches, s.rowMismatches, s.argumentMismatches,
            s.counterMismatches);
    return {};
}
} // namespace lmx::app
