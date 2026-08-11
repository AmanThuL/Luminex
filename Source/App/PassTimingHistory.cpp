//----------------------------------------------------------------------------------------------------------------------
/// @file PassTimingHistory.cpp
/// @brief Implements rolling GPU pass-timing summaries for the editor Stats panel.
//----------------------------------------------------------------------------------------------------------------------

#include "App/PassTimingHistory.h"

#include <algorithm>
#include <numeric>

namespace lmx::app {

//======================================================================================================================
bool PassTimingHistory::addFrame(uint64_t frameId, std::span<const rhi::PassTiming> timings) {
    if (frameId == 0 || frameId <= m_lastFrameId) {
        return false;
    }
    m_lastFrameId = frameId;

    const bool scheduleChanged =
        timings.size() != m_series.size() ||
        !std::equal(timings.begin(), timings.end(), m_series.begin(),
                    [](const rhi::PassTiming& timing, const Series& series) {
                        return timing.label == series.label;
                    });
    if (scheduleChanged) {
        m_series.clear();
        m_series.reserve(timings.size());
        for (const rhi::PassTiming& timing : timings) {
            m_series.push_back({.label = timing.label, .samples = {}});
        }
    }

    for (size_t index = 0; index < timings.size(); ++index) {
        std::vector<double>& samples = m_series[index].samples;
        if (samples.size() == kSampleCapacity) {
            samples.erase(samples.begin());
        }
        samples.push_back(timings[index].gpuMilliseconds);
    }
    return scheduleChanged;
}

//======================================================================================================================
std::vector<PassTimingSummary> PassTimingHistory::summaries() const {
    std::vector<PassTimingSummary> result;
    result.reserve(m_series.size());
    for (const Series& series : m_series) {
        if (series.samples.empty()) {
            continue;
        }
        const auto [minimum, maximum] =
            std::minmax_element(series.samples.begin(), series.samples.end());
        const double total = std::accumulate(series.samples.begin(), series.samples.end(), 0.0);
        result.push_back({.label = series.label,
                          .averageGpuMilliseconds = total / series.samples.size(),
                          .latestGpuMilliseconds = series.samples.back(),
                          .minimumGpuMilliseconds = *minimum,
                          .maximumGpuMilliseconds = *maximum,
                          .sampleCount = series.samples.size()});
    }
    return result;
}

} // namespace lmx::app
