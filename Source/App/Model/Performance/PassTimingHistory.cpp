//----------------------------------------------------------------------------------------------------------------------
/// @file PassTimingHistory.cpp
/// @brief Implements rolling GPU pass-timing summaries for the editor Stats panel.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Performance/PassTimingHistory.h"

#include <algorithm>
#include <numeric>

namespace lmx::app {

//======================================================================================================================
std::vector<size_t> sortedPassTimingIndices(std::span<const PassTimingSummary> rows,
                                            PassTimingSort column, bool descending) {
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (column == PassTimingSort::Schedule) {
        return order;
    }
    const auto value = [column](const PassTimingSummary& row) {
        switch (column) {
        case PassTimingSort::Average:
            return row.averageGpuMilliseconds;
        case PassTimingSort::Latest:
            return row.latestGpuMilliseconds;
        case PassTimingSort::Minimum:
            return row.minimumGpuMilliseconds;
        case PassTimingSort::Maximum:
            return row.maximumGpuMilliseconds;
        case PassTimingSort::Samples:
            return static_cast<double>(row.sampleCount);
        case PassTimingSort::Schedule:
            return 0.0;
        }
        return 0.0;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        return descending ? value(rows[left]) > value(rows[right])
                          : value(rows[left]) < value(rows[right]);
    });
    return order;
}

//======================================================================================================================
bool PassTimingHistory::addFrame(uint64_t frameId, std::span<const rojoRHI::PassTiming> timings) {
    if (frameId == 0 || frameId <= m_lastFrameId) {
        return false;
    }
    m_lastFrameId = frameId;

    const bool scheduleChanged =
        timings.size() != m_series.size() ||
        !std::equal(timings.begin(), timings.end(), m_series.begin(),
                    [](const rojoRHI::PassTiming& timing, const Series& series) {
                        return timing.label == series.label;
                    });
    if (scheduleChanged) {
        m_series.clear();
        m_series.reserve(timings.size());
        for (const rojoRHI::PassTiming& timing : timings) {
            m_series.push_back({.label = timing.label});
        }
    }

    for (size_t index = 0; index < timings.size(); ++index) {
        m_series[index].samples.push(timings[index].gpuMilliseconds);
    }
    return scheduleChanged;
}

//======================================================================================================================
std::vector<PassTimingSummary> PassTimingHistory::summaries() const {
    std::vector<PassTimingSummary> result;
    result.reserve(m_series.size());
    for (const Series& series : m_series) {
        if (series.samples.size() == 0) {
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
