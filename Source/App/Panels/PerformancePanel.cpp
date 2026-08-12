//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.cpp
/// @brief Implements the Performance panel over one coherent `PerformanceSnapshot`.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/PerformancePanel.h"

#include <imgui.h>

namespace lmx::app {

namespace {

// A fixed ceiling keeps the frame-interval plot comparable over time.
constexpr float kFrameIntervalPlotCeilingMs = 33.3f;

// Three side-by-side full-height regions so every spec section 10 value is visible at the default
// (wide, short) dock height without dragging: a fixed-width stats column, a flexible frame-interval
// plot with a floor, and the pass table filling whatever is left.
constexpr float kStatsColumnWidth = 260.0f;
constexpr float kPlotColumnMinWidth = 150.0f;
constexpr float kPlotColumnWidthFraction = 0.4f;

} // namespace

//======================================================================================================================
void drawPerformancePanel(bool& open, PerformanceModel& model) {
    if (!ImGui::Begin(kPerformancePanelWindowName, &open)) {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    float plotColumnWidth =
        (avail.x - kStatsColumnWidth - itemSpacingX * 2.0f) * kPlotColumnWidthFraction;
    if (plotColumnWidth < kPlotColumnMinWidth) {
        plotColumnWidth = kPlotColumnMinWidth;
    }
    float tableColumnWidth = avail.x - kStatsColumnWidth - plotColumnWidth - itemSpacingX * 2.0f;
    if (tableColumnWidth < 0.0f) {
        tableColumnWidth = 0.0f;
    }

    // Set only inside the stats column below, after Pause/Clear have already run -- every region
    // that follows reads through this one pointer, so nothing in this draw call can mix a
    // pre-mutation value with a post-mutation one.
    const PerformanceSnapshot* snapshot = nullptr;

    if (ImGui::BeginChild("PerformanceStatsColumn", ImVec2(kStatsColumnWidth, avail.y),
                          ImGuiChildFlags_Borders)) {
        // Pause and Clear History are model calls that can mutate `model` synchronously on click.
        // Running them before the snapshot read below -- rather than after -- means the read can
        // never observe a mix of values from before and after that mutation.
        bool paused = model.paused();
        if (ImGui::Checkbox("Pause", &paused)) {
            model.setPaused(paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear History")) {
            model.clearHistory();
        }

        // One coherent read for the whole draw call: every value in all three columns below comes
        // from this same snapshot.
        snapshot = &model.snapshot();

        ImGui::Text("Frame interval: %.2f ms wall clock (%.1f FPS)",
                    static_cast<double>(snapshot->latestFrameIntervalMs),
                    static_cast<double>(snapshot->framesPerSecond));

        if (snapshot->waitingForSamples) {
            // Clear History and a fresh run both land here until the next retired GPU frame joins
            // -- resolution, counts, and memory are only meaningful alongside the pass rows they
            // accompanied, so all of it waits together rather than showing stale zeros.
            ImGui::TextDisabled("waiting for retired GPU timings");
        } else {
            ImGui::Text("Viewport: %u x %u pt (logical)", snapshot->viewportLogicalWidth,
                        snapshot->viewportLogicalHeight);
            ImGui::Text("Scene target: %u x %u px", snapshot->sceneTargetPixelWidth,
                        snapshot->sceneTargetPixelHeight);
            ImGui::Text("Frame %llu -- %u objects, %u draws",
                        static_cast<unsigned long long>(snapshot->frameId), snapshot->objectCount,
                        snapshot->drawCount);
            ImGui::Text("Transients: requested %llu B, high-water %llu B, saved %llu B",
                        static_cast<unsigned long long>(snapshot->transientRequestedBytes),
                        static_cast<unsigned long long>(snapshot->transientHighWaterBytes),
                        static_cast<unsigned long long>(snapshot->transientAliasSavingsBytes));
            ImGui::Text("Timed pass sum: %.3f ms", snapshot->timedPassSumMilliseconds);
            if (ImGui::IsItemHovered()) {
                // Explicitly not total GPU frame time: present, driver, and untimestamped work
                // fall outside it. Kept as a hover so the stats column stays within its line
                // budget at the default dock height.
                ImGui::SetTooltip(
                    "Sum of timed passes -- not total GPU frame time: present, driver, and "
                    "untimestamped work fall outside it.");
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("PerformancePlotColumn", ImVec2(plotColumnWidth, avail.y),
                          ImGuiChildFlags_Borders)) {
        if (snapshot != nullptr) {
            ImGui::PlotLines("##frameIntervals", snapshot->frameIntervalsMs.data(),
                             static_cast<int>(snapshot->frameIntervalsMs.size()), 0,
                             "frame interval (ms)", 0.0f, kFrameIntervalPlotCeilingMs,
                             ImVec2(-1.0f, -1.0f));
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("PerformancePassTableColumn", ImVec2(tableColumnWidth, avail.y),
                          ImGuiChildFlags_Borders)) {
        if (snapshot != nullptr) {
            if (snapshot->waitingForSamples) {
                ImGui::TextDisabled("waiting for retired GPU timings");
            } else {
                ImGui::TextDisabled(
                    "GPU pass timings -- 60-frame window, schedule order, updates 4x/s");
                constexpr ImGuiTableFlags kTableFlags =
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                    ImGuiTableFlags_SizingFixedFit;
                if (ImGui::BeginTable("PerformancePassTable", 5, kTableFlags)) {
                    ImGui::TableSetupColumn("Pass");
                    ImGui::TableSetupColumn("Average");
                    ImGui::TableSetupColumn("Latest");
                    ImGui::TableSetupColumn("Min-Max");
                    ImGui::TableSetupColumn("Samples");
                    ImGui::TableHeadersRow();
                    for (const PassTimingSummary& row : snapshot->passRows) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(row.label.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f ms", row.averageGpuMilliseconds);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.3f ms", row.latestGpuMilliseconds);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.3f..%.3f ms", row.minimumGpuMilliseconds,
                                    row.maximumGpuMilliseconds);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%zu", row.sampleCount);
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace lmx::app
