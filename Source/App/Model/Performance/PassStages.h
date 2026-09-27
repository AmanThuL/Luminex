//----------------------------------------------------------------------------------------------------------------------
/// @file PassStages.h
/// @brief Groups GPU pass costs into stable renderer stage rows.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Performance/PassTimingHistory.h"
#include <string_view>

namespace lmx::app {
/// Returns the first family after lmx.pass.; foreign and empty-family labels remain unchanged.
std::string_view passStage(std::string_view label);
/// One stage's additive rolling costs, with source indices preserving exact member identities.
struct StageTimingRow {
    std::string stage;           ///< Renderer family or complete foreign label.
    double averageMs = 0;        ///< Sum of the members' average GPU milliseconds.
    double latestMs = 0;         ///< Sum of the members' latest GPU milliseconds.
    uint32_t firstSchedule = 0;  ///< First member's zero-based source schedule position.
    std::vector<size_t> members; ///< Indices into the borrowed source rows, in schedule order.
};
/// Groups all rows once, retaining first-appearance schedule order and duplicate-label members.
std::vector<StageTimingRow> groupPassStages(std::span<const PassTimingSummary> rows);
/// Additive columns available for ordering grouped stage rows.
enum class StageTimingSort {
    Schedule, ///< First member's position in the original pass schedule.
    Average,  ///< Summed average GPU duration.
    Latest,   ///< Summed latest GPU duration.
};
/// Returns indices in the chosen direction; ties retain input order without mutating the rows.
std::vector<size_t> sortedStageTimingIndices(std::span<const StageTimingRow> rows,
                                             StageTimingSort column, bool descending = true);
} // namespace lmx::app
