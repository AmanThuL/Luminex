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
        "\"emittedRows\":{},\"emittedCommands\":{},\"overflowedRows\":{},\"overflowedCommands\":{}}"
        "}",
        c.candidates, c.visible, c.rejected, c.bypassed[1], c.bypassed[2], c.bypassed[3],
        c.bypassed[4], c.emittedRows, c.emittedCommands, c.overflowedRows, c.overflowedCommands);
}
} // namespace
//======================================================================================================================
std::string_view classifyModeName(render::ClassifyMode mode) {
    return mode == render::ClassifyMode::Gpu ? "gpu" : "cpu";
}
//======================================================================================================================
std::string visibilityDiagnosticsJson(const render::VisibilityStatus& s) {
    const bool available = s.classifyMode == render::ClassifyMode::Cpu || s.isRetired;
    return std::format(
        "{{\"frameId\":{},\"classify\":\"{}\",\"retired\":{},\"overflow\":{},"
        "\"checkEnabled\":{},\"stateMismatches\":{},\"rowMismatches\":{},"
        "\"argumentMismatches\":{},\"counterMismatches\":{},\"scene\":{},\"shadow\":{}}}",
        s.frameNumber, classifyModeName(s.classifyMode), s.isRetired, s.overflow, s.checkEnabled,
        s.stateMismatches, s.rowMismatches, s.argumentMismatches, s.counterMismatches,
        available ? countersJson(s.sceneCounters) : "null",
        available ? countersJson(s.shadowCounters) : "null");
}
//======================================================================================================================
std::string visibilityFailure(const render::VisibilityStatus& s) {
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
               retained == uint64_t{c.emittedRows} + c.overflowedRows;
    };
    if (!reconciles(s.sceneCounters) || !reconciles(s.shadowCounters))
        return std::format("frame {}: visibility counters do not reconcile", s.frameNumber);
    if (!s.checkPassed())
        return std::format(
            "frame {}: visibility check mismatches states {}, rows {}, arguments {}, counters {}",
            s.frameNumber, s.stateMismatches, s.rowMismatches, s.argumentMismatches,
            s.counterMismatches);
    return {};
}
} // namespace lmx::app
