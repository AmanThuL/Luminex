//----------------------------------------------------------------------------------------------------------------------
/// @file LightingDisplay.cpp
/// @brief Formats and publishes coherent local-light counters and matched GPU timings.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Rendering/Lighting/LightingDisplay.h"

#include <algorithm>
#include <format>

namespace lmx::app {
namespace {

//======================================================================================================================
bool sameContext(const render::LightingStatus& a, const render::LightingStatus& b) {
    return a.sceneGeneration == b.sceneGeneration && a.requested == b.requested &&
           a.effective == b.effective && a.checkEnabled == b.checkEnabled;
}

} // namespace

//======================================================================================================================
std::string_view localLightModeLabel(engine::LocalLightMode mode) {
    switch (mode) {
    case engine::LocalLightMode::Off:
        return "Off";
    case engine::LocalLightMode::Direct:
        return "Direct";
    case engine::LocalLightMode::Clustered:
        return "Clustered";
    }
    return "Unavailable";
}

//======================================================================================================================
std::vector<LightingField> lightingFields(const render::LightingStatus& status,
                                          std::span<const rojoRHI::PassTiming> timings) {
    std::vector<LightingField> fields{
        {"Frame", std::format("{} ({})", status.frameNumber,
                              status.isRetired ? "retired" : "awaiting retirement")},
        {"Requested / effective", std::format("{} / {}", localLightModeLabel(status.requested),
                                              localLightModeLabel(status.effective))},
        {"Enabled lights", std::to_string(status.liveLightCount)},
        {"Grid / slices", "16 x 9 x 24; 0.3 to 100 m, open last slice"},
        {"Capacities", "4096 lights; 128 per froxel; 65536 indices"}};
    if (!status.isRetired)
        return fields;
    fields.push_back({"Light check", !status.checkEnabled   ? "Disabled"
                                     : status.checkPassed() ? "Passed"
                                                            : "FAILED"});
    if (status.checkEnabled)
        fields.push_back(
            {"Grid / index / counter mismatches",
             std::format("{} / {} / {}", status.check.gridMismatches, status.check.indexMismatches,
                         status.check.counterMismatches)});
    const auto& c = status.counters;
    fields.push_back({"Assigned / candidates", std::format("{} / {}", c.assigned, c.candidates)});
    fields.push_back({"Dropped per froxel / global",
                      std::format("{} / {}", c.droppedPerCluster, c.droppedGlobal)});
    fields.push_back(
        {"Truncated froxels / max count", std::format("{} / {}", c.truncatedFroxels, c.maxCount)});
    fields.push_back({"List bytes (used / allocated)",
                      std::format("{} / {}", status.listBytes, status.allocatedListBytes)});
    bool hasTiming = false;
    for (const auto& timing : timings) {
        if (timing.label.starts_with("lmx.pass.light.")) {
            fields.push_back({timing.label, std::format("{:.3f} ms", timing.gpuMilliseconds)});
            hasTiming = true;
        }
    }
    if (!hasTiming)
        fields.push_back({"Lighting GPU time", status.effective == engine::LocalLightMode::Clustered
                                                   ? "Awaiting matching frame"
                                                   : "No light-list passes"});
    return fields;
}

//======================================================================================================================
void LightingDisplay::observe(const render::LightingStatus& status) {
    if (!m_hasContext || !sameContext(status, m_context)) {
        clear();
        m_latest = status;
    } else if (!m_latest.isRetired) {
        m_latest = status;
    }
    m_context = status;
    m_hasContext = true;
    if (status.isRetired)
        retire(status);
}

//======================================================================================================================
void LightingDisplay::retire(const render::LightingStatus& status) {
    if (!m_hasContext || !status.isRetired || !sameContext(status, m_context) ||
        (m_latest.isRetired && status.frameNumber <= m_latest.frameNumber))
        return;
    m_latest = status;
}

//======================================================================================================================
void LightingDisplay::observeTimings(uint64_t frame, std::span<const rojoRHI::PassTiming> timings) {
    if (timings.empty() || (!m_timings.empty() && frame <= m_timings.back().frame))
        return;
    m_timings.push_back({frame, {timings.begin(), timings.end()}});
    if (m_timings.size() > 8)
        m_timings.pop_front();
}

//======================================================================================================================
void LightingDisplay::publishReadings(double nowSeconds) {
    if (!m_hasContext)
        return;
    const bool becameReady = !m_readings.isRetired && m_latest.isRetired;
    if (m_hasReadings && !becameReady && nowSeconds < m_nextReadingsSeconds)
        return;
    m_readings = m_latest;
    const auto found = std::ranges::find(m_timings, m_readings.frameNumber, &Timings::frame);
    m_readingsTimings =
        found == m_timings.end() ? std::vector<rojoRHI::PassTiming>{} : found->passes;
    m_nextReadingsSeconds = nowSeconds + 0.25;
    m_hasReadings = true;
}

//======================================================================================================================
void LightingDisplay::clear() {
    m_context = {};
    m_latest = {};
    m_readings = {};
    m_timings.clear();
    m_readingsTimings.clear();
    m_nextReadingsSeconds = 0;
    m_hasContext = false;
    m_hasReadings = false;
}

} // namespace lmx::app
