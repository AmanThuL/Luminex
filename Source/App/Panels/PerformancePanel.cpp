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

} // namespace

//======================================================================================================================
void drawPerformancePanel(bool& open, PerformanceModel& model) {
    if (!ImGui::Begin(kPerformancePanelWindowName, &open)) {
        ImGui::End();
        return;
    }

    // One coherent read for the whole draw call: every value below comes from this same snapshot,
    // so nothing here can mix a frozen row with a live resolution or memory value.
    const PerformanceSnapshot& snapshot = model.snapshot();

    ImGui::Text("Frame interval: %.2f ms wall clock (%.1f FPS)",
                static_cast<double>(snapshot.latestFrameIntervalMs),
                static_cast<double>(snapshot.framesPerSecond));
    ImGui::PlotLines("##frameIntervals", snapshot.frameIntervalsMs.data(),
                     static_cast<int>(snapshot.frameIntervalsMs.size()), 0, "frame interval (ms)",
                     0.0f, kFrameIntervalPlotCeilingMs, ImVec2(0.0f, 60.0f));

    bool paused = model.paused();
    if (ImGui::Checkbox("Pause", &paused)) {
        model.setPaused(paused);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear History")) {
        model.clearHistory();
    }

    ImGui::Separator();

    if (snapshot.waitingForSamples) {
        // Clear History and a fresh run both land here until the next retired GPU frame joins --
        // the frame ID, counts, resolution, and memory below are only meaningful alongside the pass
        // rows they accompanied, so all of it waits together rather than showing stale zeros.
        ImGui::TextDisabled("waiting for retired GPU timings");
        ImGui::End();
        return;
    }

    ImGui::Text("Viewport: %u x %u pt (logical)", snapshot.viewportLogicalWidth,
                snapshot.viewportLogicalHeight);
    ImGui::Text("Scene target: %u x %u px", snapshot.sceneTargetPixelWidth,
                snapshot.sceneTargetPixelHeight);
    ImGui::Text("Frame %llu -- %u objects, %u draws",
                static_cast<unsigned long long>(snapshot.frameId), snapshot.objectCount,
                snapshot.drawCount);

    ImGui::TextDisabled("GPU pass timings -- 60-frame window, schedule order, updates 4x/s");
    constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("PerformancePassTable", 5, kTableFlags)) {
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Average");
        ImGui::TableSetupColumn("Latest");
        ImGui::TableSetupColumn("Min-Max");
        ImGui::TableSetupColumn("Samples");
        ImGui::TableHeadersRow();
        for (const PassTimingSummary& row : snapshot.passRows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f ms", row.averageGpuMilliseconds);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f ms", row.latestGpuMilliseconds);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f..%.3f ms", row.minimumGpuMilliseconds, row.maximumGpuMilliseconds);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu", row.sampleCount);
        }
        ImGui::EndTable();
    }
    // Explicitly not total GPU frame time: present, driver, and untimestamped work fall outside it.
    ImGui::Text("Timed pass sum: %.3f ms (sum of timed passes -- not total GPU frame time)",
                snapshot.timedPassSumMilliseconds);

    ImGui::Separator();
    ImGui::Text("Transients: requested %llu B, high-water %llu B, saved %llu B",
                static_cast<unsigned long long>(snapshot.transientRequestedBytes),
                static_cast<unsigned long long>(snapshot.transientHighWaterBytes),
                static_cast<unsigned long long>(snapshot.transientAliasSavingsBytes));

    ImGui::End();
}

} // namespace lmx::app
