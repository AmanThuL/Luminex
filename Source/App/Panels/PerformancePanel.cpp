//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.cpp
/// @brief Implements the Performance panel over one coherent `PerformanceSnapshot`.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/PerformancePanel.h"

#include "App/Panels/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <numeric>
#include <string>

namespace lmx::app {

namespace {

constexpr float kTargetIntervalMs = 1000.0f / 60.0f;
constexpr double kBytesPerMiB = 1024.0 * 1024.0;

//======================================================================================================================
void summary(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::TextWrapped("%s", value);
}

//======================================================================================================================
void drawSummary(const PerformanceSnapshot& snapshot) {
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = width >= editor_style::scaled(1000.0f)
                            ? 4
                            : (width >= editor_style::scaled(560.0f) ? 2 : 1);
    if (!ImGui::BeginTable("MetricSummaries", columns, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    std::array<char, 160> text{};
    ImGui::TableNextColumn();
    if (snapshot.frameIntervalsMs.empty()) {
        summary("Interval / mean FPS", "Waiting for frame intervals");
    } else {
        std::snprintf(text.data(), text.size(), "%.2f ms / %.1f FPS",
                      snapshot.latestFrameIntervalMs, snapshot.framesPerSecond);
        summary("Interval / mean FPS", text.data());
    }
    ImGui::TableNextColumn();
    if (snapshot.waitingForSamples) {
        summary("Timed pass sum (average)", "Waiting for retired samples");
    } else {
        std::snprintf(text.data(), text.size(), "%.3f ms", snapshot.timedPassSumMilliseconds);
        summary("Timed pass sum (average)", text.data());
    }
    ImGui::TableNextColumn();
    if (snapshot.waitingForSamples) {
        summary("Render / output pixels", "N/A");
    } else {
        std::snprintf(text.data(), text.size(), "%u x %u / %u x %u", snapshot.renderPixelWidth,
                      snapshot.renderPixelHeight, snapshot.sceneTargetPixelWidth,
                      snapshot.sceneTargetPixelHeight);
        summary("Render / output pixels", text.data());
    }
    ImGui::TableNextColumn();
    if (snapshot.waitingForSamples) {
        summary("Objects / draws / peak MiB", "N/A");
    } else {
        std::snprintf(text.data(), text.size(), "%u / %u / %.2f", snapshot.objectCount,
                      snapshot.drawCount, snapshot.transientHighWaterBytes / kBytesPerMiB);
        summary("Objects / draws / peak MiB", text.data());
    }
    ImGui::EndTable();
}

//======================================================================================================================
void drawIntervalPlot(const PerformanceSnapshot& snapshot) {
    ImGui::TextUnformatted("Wall-clock frame interval (ms)");
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float plotHeight =
        std::max(editor_style::scaled(18.0f),
                 ImGui::GetContentRegionAvail().y - editor_style::scaled(24.0f));
    const ImVec2 plotMin(origin.x + editor_style::scaled(36.0f),
                         origin.y + editor_style::scaled(4.0f));
    const ImVec2 plotMax(
        origin.x + std::max(width - editor_style::scaled(6.0f), editor_style::scaled(60.0f)),
        origin.y + plotHeight);
    const float largest =
        snapshot.frameIntervalsMs.empty()
            ? 0.0f
            : *std::max_element(snapshot.frameIntervalsMs.begin(), snapshot.frameIntervalsMs.end());
    const float ceilingMs = std::max(33.3334f, std::ceil(largest / 10.0f) * 10.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(plotMin, plotMax, ImGui::GetColorU32(ImGuiCol_FrameBg),
                        editor_style::scaled(3.0f));
    std::array<char, 80> text{};
    for (int tick = 0; tick <= 2; ++tick) {
        const float value = ceilingMs * static_cast<float>(tick) / 2.0f;
        const float y = plotMax.y - (plotMax.y - plotMin.y) * value / ceilingMs;
        draw->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y),
                      ImGui::GetColorU32(ImGuiCol_Border), editor_style::scaled(1.0f));
        std::snprintf(text.data(), text.size(), "%.0f", value);
        draw->AddText(ImVec2(origin.x, y - ImGui::GetFontSize() * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), text.data());
    }
    const float targetY = plotMax.y - (plotMax.y - plotMin.y) * kTargetIntervalMs / ceilingMs;
    draw->AddLine(ImVec2(plotMin.x, targetY), ImVec2(plotMax.x, targetY),
                  ImGui::GetColorU32(editor_style::kWarning), editor_style::scaled(1.0f));
    draw->AddText(ImVec2(plotMin.x + editor_style::scaled(4.0f),
                         std::max(plotMin.y, targetY - ImGui::GetFontSize())),
                  ImGui::GetColorU32(editor_style::kWarning), "16.7 ms - 60 Hz");
    const float totalMs =
        std::accumulate(snapshot.frameIntervalsMs.begin(), snapshot.frameIntervalsMs.end(), 0.0f);
    float elapsedMs = 0.0f;
    ImVec2 previous{};
    for (size_t i = 0; i < snapshot.frameIntervalsMs.size(); ++i) {
        elapsedMs += snapshot.frameIntervalsMs[i];
        const ImVec2 point(
            plotMin.x + (plotMax.x - plotMin.x) * (totalMs > 0.0f ? elapsedMs / totalMs : 1.0f),
            plotMax.y - (plotMax.y - plotMin.y) * snapshot.frameIntervalsMs[i] / ceilingMs);
        if (i > 0) {
            draw->AddLine(previous, point, ImGui::GetColorU32(editor_style::kAccent),
                          editor_style::scaled(1.5f));
        }
        previous = point;
    }
    std::snprintf(text.data(), text.size(), "-%.2f s (%zu intervals)", totalMs / 1000.0f,
                  snapshot.frameIntervalsMs.size());
    draw->AddText(ImVec2(plotMin.x, plotMax.y + editor_style::scaled(4.0f)),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), text.data());
    constexpr const char* kNewest = "latest";
    draw->AddText(
        ImVec2(plotMax.x - ImGui::CalcTextSize(kNewest).x, plotMax.y + editor_style::scaled(4.0f)),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), kNewest);
    ImGui::Dummy(ImVec2(width, plotMax.y - origin.y + editor_style::scaled(22.0f)));
}

//======================================================================================================================
void numericCell(double value, const char* format = "%.3f") {
    std::array<char, 48> text{};
    std::snprintf(text.data(), text.size(), format, value);
    const float padding = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.data()).x;
    if (padding > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
    }
    ImGui::TextUnformatted(text.data());
}

//======================================================================================================================
const PassTimingSummary* drawPassTable(const PerformanceSnapshot& snapshot, float height,
                                       bool scheduleOrder) {
    static std::string selectedLabel;
    static size_t selectedOccurrence = 0;
    if (snapshot.waitingForSamples) {
        editor_style::message("Waiting for a new retired GPU sample. Scene playback continues.");
        return nullptr;
    }
    const bool compact = ImGui::GetContentRegionAvail().x < editor_style::scaled(650.0f);
    const PassTimingSummary* selected = nullptr;
    size_t occurrence = 0;
    for (const auto& row : snapshot.passRows) {
        if (row.label == selectedLabel && occurrence++ == selectedOccurrence) {
            selected = &row;
            break;
        }
    }
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                       ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
                                       ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("PassCosts", compact ? 3 : 6, kFlags, ImVec2(0.0f, height))) {
        return selected;
    }
    ImGui::TableSetupColumn(
        "Pass", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.0f);
    ImGui::TableSetupColumn("Average (ms)",
                            ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortDescending,
                            editor_style::scaled(100.0f), 1);
    ImGui::TableSetupColumn("Latest (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                            editor_style::scaled(90.0f), 2);
    if (!compact) {
        ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                                editor_style::scaled(70.0f), 3);
        ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                                editor_style::scaled(70.0f), 4);
        ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_PreferSortDescending,
                                editor_style::scaled(60.0f), 5);
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    PassTimingSort sort = PassTimingSort::Average;
    bool descending = true;
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs != nullptr && specs->SpecsCount > 0) {
        const auto& spec = specs->Specs[0];
        sort = static_cast<PassTimingSort>(spec.ColumnUserID);
        descending = spec.SortDirection == ImGuiSortDirection_Descending;
    }
    const auto indices = sortedPassTimingIndices(
        snapshot.passRows, scheduleOrder ? PassTimingSort::Schedule : sort, descending);
    for (const size_t index : indices) {
        const auto& row = snapshot.passRows[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const char* label =
            row.label.starts_with("lmx.pass.") ? row.label.c_str() + 9 : row.label.c_str();
        if (ImGui::Selectable(label, selected == &row, ImGuiSelectableFlags_SpanAllColumns)) {
            selected = &row;
            selectedLabel = row.label;
            selectedOccurrence = static_cast<size_t>(
                std::count_if(snapshot.passRows.begin(),
                              snapshot.passRows.begin() + static_cast<std::ptrdiff_t>(index),
                              [&](const auto& candidate) { return candidate.label == row.label; }));
        }
        const std::string help = std::format(
            "{}\nRange {:.3f}-{:.3f} ms | {} / {} samples\nSelect for full details below.",
            row.label, row.minimumGpuMilliseconds, row.maximumGpuMilliseconds, row.sampleCount,
            PassTimingHistory::kSampleCapacity);
        editorTooltip(help.c_str());
        ImGui::TableSetColumnIndex(1);
        numericCell(row.averageGpuMilliseconds);
        ImGui::TableSetColumnIndex(2);
        numericCell(row.latestGpuMilliseconds);
        if (!compact) {
            ImGui::TableSetColumnIndex(3);
            numericCell(row.minimumGpuMilliseconds);
            ImGui::TableSetColumnIndex(4);
            numericCell(row.maximumGpuMilliseconds);
            ImGui::TableSetColumnIndex(5);
            numericCell(static_cast<double>(row.sampleCount), "%.0f");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    return selected;
}

} // namespace

//======================================================================================================================
void drawPerformancePanel(bool& open, PerformanceModel& model) {
    if (!ImGui::Begin(kPerformancePanelWindowName, &open)) {
        ImGui::End();
        return;
    }
    static bool scheduleOrder = false;
    const float contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(model.paused() ? "Resume metrics" : "Freeze metrics")) {
        model.setPaused(!model.paused());
    }
    editorTooltip("Freeze or resume this metrics snapshot, including the table and plot. Scene "
                  "playback continues; resuming waits for fresh samples.");
    const auto continueToolbar = [contentRight](float nextWidth) {
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <=
            contentRight) {
            ImGui::SameLine();
        }
    };
    continueToolbar(ImGui::CalcTextSize("Clear history").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Clear history")) {
        model.clearHistory();
    }
    editorTooltip("Discard retained timing and frame interval history. A frozen snapshot stays "
                  "empty until you resume metrics.");
    continueToolbar(ImGui::CalcTextSize("Schedule order").x + ImGui::GetFrameHeight() +
                    ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Checkbox("Schedule order", &scheduleOrder);
    editorTooltip("Show declared pass schedule order. Turn off to use the numeric column sort; "
                  "click a column header to change it.");
    const PerformanceSnapshot& snapshot = model.snapshot();
    ImGui::TextWrapped(
        "%s | frame %llu | %.2f s | %zu / %zu samples | %.0f updates/s",
        model.paused() ? (snapshot.waitingForSamples ? "Frozen - empty" : "Frozen") : "Live",
        static_cast<unsigned long long>(snapshot.frameId), snapshot.publishedAtSeconds,
        snapshot.passRows.empty() ? size_t{0} : snapshot.passRows.front().sampleCount,
        PassTimingHistory::kSampleCapacity, 1.0f / PerformanceModel::kRepublishIntervalSeconds);
    drawSummary(snapshot);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const bool wide = available.x >= editor_style::scaled(760.0f);
    const float detailsRowHeight = ImGui::GetFrameHeightWithSpacing();
    const float height = std::max(editor_style::scaled(48.0f),
                                  available.y - detailsRowHeight - ImGui::GetStyle().ItemSpacing.y);
    const float tableWidth = wide ? available.x * 0.65f : available.x;
    const PassTimingSummary* selected = nullptr;
    if (ImGui::BeginChild("Costs", ImVec2(tableWidth, height))) {
        selected = drawPassTable(snapshot, height, scheduleOrder);
    }
    ImGui::EndChild();
    if (wide) {
        ImGui::SameLine();
        if (ImGui::BeginChild("Intervals", ImVec2(0.0f, height))) {
            drawIntervalPlot(snapshot);
        }
        ImGui::EndChild();
    } else if (ImGui::CollapsingHeader("Frame interval history")) {
        if (ImGui::BeginChild("Intervals", ImVec2(0.0f, editor_style::scaled(130.0f)))) {
            drawIntervalPlot(snapshot);
        }
        ImGui::EndChild();
    }
    const char* detailsLabel = selected != nullptr
                                   ? "Selected pass & metric details###MetricDetails"
                                   : "Metric definitions & exact memory###MetricDetails";
    if (ImGui::CollapsingHeader(detailsLabel)) {
        if (selected != nullptr) {
            ImGui::TextWrapped("%s", selected->label.c_str());
            ImGui::TextWrapped("Range %.3f-%.3f ms | %zu / %zu samples",
                               selected->minimumGpuMilliseconds, selected->maximumGpuMilliseconds,
                               selected->sampleCount, PassTimingHistory::kSampleCapacity);
            if (ImGui::SmallButton("Copy pass ID")) {
                ImGui::SetClipboardText(selected->label.c_str());
            }
            editorTooltip(
                "Copy the full renderer pass label for logs, captures or the Render Graph.");
        }
        ImGui::TextWrapped(
            "Timed pass sum averages the retained GPU pass samples; present, driver "
            "and untimed work are outside this sum. Latest is the newest retired frame. "
            "FPS uses the mean of the wall-clock interval history. Frame labels identify the "
            "newest retirement; seconds identify snapshot publication since startup. "
            "Freeze affects this panel only. Clear and Resume wait for new samples.");
        if (!snapshot.waitingForSamples) {
            ImGui::TextWrapped(
                "Viewport: %u x %u pt. Transients: requested %llu B; high-water %llu B; "
                "alias savings %llu B.",
                snapshot.viewportLogicalWidth, snapshot.viewportLogicalHeight,
                static_cast<unsigned long long>(snapshot.transientRequestedBytes),
                static_cast<unsigned long long>(snapshot.transientHighWaterBytes),
                static_cast<unsigned long long>(snapshot.transientAliasSavingsBytes));
        }
    }
    ImGui::End();
}

} // namespace lmx::app
