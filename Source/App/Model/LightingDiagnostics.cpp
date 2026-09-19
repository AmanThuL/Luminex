//----------------------------------------------------------------------------------------------------------------------
/// @file LightingDiagnostics.cpp
/// @brief Serializes lighting diagnostics without discarding declaration identity.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/LightingDiagnostics.h"
#include <format>
namespace lmx::app {
//======================================================================================================================
std::string_view localLightModeName(render::LocalLightMode mode) {
    switch (mode) {
    case render::LocalLightMode::Off:
        return "off";
    case render::LocalLightMode::Direct:
        return "direct";
    case render::LocalLightMode::Clustered:
        return "clustered";
    }
    return "unknown";
}
//======================================================================================================================
std::string_view lightDebugViewName(render::LightDebugView view) {
    switch (view) {
    case render::LightDebugView::Off:
        return "off";
    case render::LightDebugView::Count:
        return "count";
    case render::LightDebugView::Overflow:
        return "overflow";
    case render::LightDebugView::Missed:
        return "missed";
    }
    return "unknown";
}
//======================================================================================================================
std::string lightingDiagnosticsJson(const render::LightingStatus& s) {
    const auto& c = s.counters;
    const auto counters =
        s.isRetired
            ? std::format(
                  R"({{"candidates":{},"assigned":{},"droppedPerCluster":{},"droppedGlobal":{},"truncatedFroxels":{},"maxCount":{}}})",
                  c.candidates, c.assigned, c.droppedPerCluster, c.droppedGlobal,
                  c.truncatedFroxels, c.maxCount)
            : "null";
    return std::format(
        R"({{"frameId":{},"sceneGeneration":{},"requestedMode":"{}","effectiveMode":"{}","liveLightCount":{},"retired":{},"counters":{},"listBytes":{},"allocatedListBytes":{},"checkEnabled":{},"gridMismatches":{},"indexMismatches":{},"counterMismatches":{}}})",
        s.frameNumber, s.sceneGeneration, localLightModeName(s.requested),
        localLightModeName(s.effective), s.liveLightCount, s.isRetired, counters, s.listBytes,
        s.allocatedListBytes, s.checkEnabled, s.check.gridMismatches, s.check.indexMismatches,
        s.check.counterMismatches);
}
//======================================================================================================================
std::string lightingFailure(const render::LightingStatus& s) {
    if (!s.isRetired)
        return std::format("frame {}: lighting result has not retired", s.frameNumber);
    const auto& c = s.counters;
    if (uint64_t{c.assigned} + c.droppedPerCluster + c.droppedGlobal != c.candidates)
        return std::format("frame {}: lighting counters do not reconcile", s.frameNumber);
    if (!s.checkPassed())
        return std::format("frame {}: light check mismatches grid {}, indices {}, counters {}",
                           s.frameNumber, s.check.gridMismatches, s.check.indexMismatches,
                           s.check.counterMismatches);
    return {};
}
} // namespace lmx::app
