//----------------------------------------------------------------------------------------------------------------------
/// @file PassTimingHistory.h
/// @brief Declares rolling GPU pass-timing summaries for the editor Stats panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

/// Stable display statistics for one pass position in a repeated compiled schedule.
struct PassTimingSummary {
    std::string label;                   ///< Diagnostic label at this schedule position.
    double averageGpuMilliseconds = 0.0; ///< Arithmetic mean of the retained samples.
    double latestGpuMilliseconds = 0.0;  ///< Newest retained sample.
    double minimumGpuMilliseconds = 0.0; ///< Smallest retained sample.
    double maximumGpuMilliseconds = 0.0; ///< Largest retained sample.
    size_t sampleCount = 0;              ///< Samples contributing to the statistics.
};

/// Keeps a bounded timing history without confusing different compiled schedules.
///
/// Pass labels are diagnostics and need not be unique, so a series is identified by its position
/// in the RHI's schedule-ordered timing publication. If the ordered label sequence changes, every
/// series resets together rather than averaging unlike graph shapes. Repeated publication of the
/// same frame number is ignored.
class PassTimingHistory {
public:
    /// Samples retained per pass before the oldest value rolls out.
    static constexpr size_t kSampleCapacity = 60;

    /// Adds one retired frame. Returns true when a changed schedule reset all prior samples.
    bool addFrame(uint64_t frameId, std::span<const rhi::PassTiming> timings);

    /// Computes display rows in schedule order from the currently retained samples.
    std::vector<PassTimingSummary> summaries() const;

private:
    struct Series {
        std::string label;
        std::vector<double> samples;
    };

    uint64_t m_lastFrameId = 0;
    std::vector<Series> m_series;
};

} // namespace lmx::app
