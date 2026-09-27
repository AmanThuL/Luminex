//----------------------------------------------------------------------------------------------------------------------
/// @file PassStages.cpp
/// @brief Implements stage grouping and stable timing order.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Performance/PassStages.h"
#include <algorithm>
#include <numeric>

namespace lmx::app {
//======================================================================================================================
std::string_view passStage(std::string_view label) {
    constexpr std::string_view prefix = "lmx.pass.";
    if (!label.starts_with(prefix) || label.size() == prefix.size())
        return label;
    const auto family = label.substr(prefix.size());
    const auto end = family.find('.');
    return end == 0 ? label : family.substr(0, end);
}
//======================================================================================================================
std::vector<StageTimingRow> groupPassStages(std::span<const PassTimingSummary> rows) {
    std::vector<StageTimingRow> groups;
    for (size_t index = 0; index < rows.size(); ++index) {
        const auto stage = passStage(rows[index].label);
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&](const auto& row) { return row.stage == stage; });
        if (group == groups.end()) {
            groups.push_back({.stage = std::string(stage),
                              .firstSchedule = static_cast<uint32_t>(index),
                              .members = {}});
            group = std::prev(groups.end());
        }
        group->averageMs += rows[index].averageGpuMilliseconds;
        group->latestMs += rows[index].latestGpuMilliseconds;
        group->members.push_back(index);
    }
    return groups;
}
//======================================================================================================================
std::vector<size_t> sortedStageTimingIndices(std::span<const StageTimingRow> rows,
                                             StageTimingSort column, bool descending) {
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const auto value = [column](const auto& row) {
        switch (column) {
        case StageTimingSort::Schedule:
            return static_cast<double>(row.firstSchedule);
        case StageTimingSort::Average:
            return row.averageMs;
        case StageTimingSort::Latest:
            return row.latestMs;
        }
        return 0.0;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return descending ? value(rows[a]) > value(rows[b]) : value(rows[a]) < value(rows[b]);
    });
    return order;
}
} // namespace lmx::app
