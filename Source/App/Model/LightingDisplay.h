//----------------------------------------------------------------------------------------------------------------------
/// @file LightingDisplay.h
/// @brief Declares coherent frame-scoped lighting readings and exact-frame timing joins.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "RHI/Device.h"
#include "Render/LightingStatus.h"

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// One formatted field in the Lighting Inspector.
struct LightingField {
    std::string label; ///< User-facing name.
    std::string value; ///< Value from one retained frame.
};

/// Stable display name for the local-light execution path.
std::string_view localLightModeLabel(render::LocalLightMode mode);

/// Formats a single coherent publication and only the timings already joined to that frame.
std::vector<LightingField> lightingFields(const render::LightingStatus& status,
                                          std::span<const rhi::PassTiming> timings = {});

/// Retains latest diagnostics for immediate warnings while publishing counters/timings at 4 Hz.
class LightingDisplay {
public:
    /// Observes declaration context; changing scene or mode clears stale retired readings.
    void observe(const render::LightingStatus& status);
    /// Retains a completed frame only if its scene/mode matches current declaration context.
    void retire(const render::LightingStatus& status);
    /// Stores bounded exact-frame timing snapshots; never joins timings from another frame.
    void observeTimings(uint64_t frame, std::span<const rhi::PassTiming> timings);
    /// Publishes at most every 250 ms, except first data, retirement readiness and context changes.
    void publishReadings(double nowSeconds);
    /// Latest coherent result, independent of the throttled Inspector counters.
    const render::LightingStatus& status() const { return m_latest; }
    /// Owned 250 ms reading snapshot; pending declarations have isRetired false.
    const render::LightingStatus& readingsStatus() const { return m_readings; }
    /// Owned timings matching readingsStatus exactly; empty until matching timings exist.
    std::span<const rhi::PassTiming> readingsTimings() const { return m_readingsTimings; }
    /// Drops scene context, retained readings and bounded timing history.
    void clear();

private:
    struct Timings {
        uint64_t frame;
        std::vector<rhi::PassTiming> passes;
    };
    render::LightingStatus m_context;
    render::LightingStatus m_latest;
    render::LightingStatus m_readings;
    std::deque<Timings> m_timings;
    std::vector<rhi::PassTiming> m_readingsTimings;
    double m_nextReadingsSeconds = 0.0;
    bool m_hasContext = false;
    bool m_hasReadings = false;
};

} // namespace lmx::app
