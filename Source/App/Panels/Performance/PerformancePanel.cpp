//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.cpp
/// @brief Implements the Performance panel over one coherent `PerformanceSnapshot`.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Performance/PerformancePanel.h"

#include "App/Model/Performance/PassStages.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <SDL3/SDL.h>
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
bool beginPerformanceWindow(bool& open, PerformancePanelState& state) {
    static const ImGuiWindowClass windowClass = [] {
        ImGuiWindowClass created;
        created.ClassId =
            0x6C6D7870u; // 'lmxp'; independent of Render Graph and the main dockspace.
        created.DockingAllowUnclassed = false;
        created.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        created.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
        return created;
    }();
    ImGui::SetNextWindowClass(&windowClass);
    const auto* main = ImGui::GetMainViewport();
    const ImVec2 size{std::min(1100.0f, main->WorkSize.x), std::min(760.0f, main->WorkSize.y)};
    const auto condition = state.resetPlacement ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos({main->WorkPos.x + (main->WorkSize.x - size.x) * 0.5f,
                             main->WorkPos.y + (main->WorkSize.y - size.y) * 0.5f},
                            condition);
    ImGui::SetNextWindowSize(size, condition);
    state.resetPlacement = false;
    if (state.requestFocus)
        ImGui::SetNextWindowFocus();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking;
    if (state.ownsPlatformWindow)
        flags |= ImGuiWindowFlags_NoTitleBar;
    const bool visible = ImGui::Begin(kPerformancePanelWindowName, &open, flags);
    auto* viewport = ImGui::GetWindowViewport();
    state.ownsPlatformWindow = viewport != main;
    if (state.requestFocus && state.ownsPlatformWindow && viewport->PlatformHandle) {
        // Explicit Show/Play also recovers a minimized window. Ordinary updates never raise it.
        const auto windowId =
            static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle));
        if (auto* window = SDL_GetWindowFromID(windowId)) {
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
        }
        state.requestFocus = false;
    }
    return visible;
}

//======================================================================================================================
void summary(const char* label, const char* value) {
    {
        const editor_style::ScopedType type(TypeRole::Caption);
        ImGui::TextDisabled("%s", label);
    }
    const editor_style::ScopedType type(TypeRole::MonoBody);
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
                  editor_style::colorU32(ThemeRole::PlotLimit), editor_style::scaled(1.0f));
    draw->AddText(ImVec2(plotMin.x + editor_style::scaled(4.0f),
                         std::max(plotMin.y, targetY - ImGui::GetFontSize())),
                  editor_style::colorU32(ThemeRole::PlotLimit), "16.7 ms - 60 Hz");
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
            draw->AddLine(previous, point, editor_style::colorU32(ThemeRole::PlotLine),
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
    const editor_style::ScopedType type(TypeRole::MonoBody);
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
                                       bool individualRows = false) {
    const editor_style::ScopedType type(TypeRole::MonoBody);
    static std::string selectedLabel;
    static size_t selectedOccurrence = 0;
    if (snapshot.waitingForSamples)
        return nullptr;
    const PassTimingSummary* selected = nullptr;
    size_t occurrence = 0;
    for (const auto& row : snapshot.passRows) {
        if (row.label == selectedLabel && occurrence++ == selectedOccurrence) {
            selected = &row;
            break;
        }
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
                                      ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable(individualRows ? "IndividualPassCosts" : "PassCosts",
                           individualRows ? 7 : 4, flags, ImVec2(0.0f, height)))
        return selected;
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortAscending,
                            editor_style::scaled(36.0f),
                            static_cast<ImGuiID>(PassTimingSort::Schedule));
    ImGui::TableSetupColumn(
        "Stage / pass", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.0f);
    ImGui::TableSetupColumn("Average (ms)",
                            ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortDescending,
                            ImGui::CalcTextSize("Average (ms)").x + ImGui::GetFrameHeight(),
                            static_cast<ImGuiID>(PassTimingSort::Average));
    ImGui::TableSetupColumn("Latest (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                            ImGui::CalcTextSize("Latest (ms)").x + ImGui::GetFrameHeight(),
                            static_cast<ImGuiID>(PassTimingSort::Latest));
    if (individualRows) {
        ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                                ImGui::CalcTextSize("Min (ms)").x + ImGui::GetFrameHeight(),
                                static_cast<ImGuiID>(PassTimingSort::Minimum));
        ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_PreferSortDescending,
                                ImGui::CalcTextSize("Min (ms)").x + ImGui::GetFrameHeight(),
                                static_cast<ImGuiID>(PassTimingSort::Maximum));
        ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_PreferSortDescending,
                                ImGui::CalcTextSize("Samples").x + ImGui::GetFrameHeight(),
                                static_cast<ImGuiID>(PassTimingSort::Samples));
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    auto sort = PassTimingSort::Average;
    bool descending = true;
    if (const auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        sort = static_cast<PassTimingSort>(specs->Specs[0].ColumnUserID);
        descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
    }
    const auto selectPass = [&](size_t index) {
        const auto& row = snapshot.passRows[index];
        selected = &row;
        selectedLabel = row.label;
        selectedOccurrence = static_cast<size_t>(
            std::count_if(snapshot.passRows.begin(),
                          snapshot.passRows.begin() + static_cast<std::ptrdiff_t>(index),
                          [&](const auto& candidate) { return candidate.label == row.label; }));
    };
    const auto passHelp = [](const PassTimingSummary& row) {
        const std::string help =
            std::format("{}\nRange {:.3f}-{:.3f} ms | {} / {} samples\nSelect for exact details "
                        "and Copy pass ID.",
                        row.label, row.minimumGpuMilliseconds, row.maximumGpuMilliseconds,
                        row.sampleCount, PassTimingHistory::kSampleCapacity);
        editorTooltip(help.c_str());
    };
    if (individualRows) {
        const auto passSort = sort;
        auto indices = sortedPassTimingIndices(snapshot.passRows, passSort, descending);
        if (passSort == PassTimingSort::Schedule && descending)
            std::reverse(indices.begin(), indices.end());
        for (const auto index : indices) {
            const auto& row = snapshot.passRows[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            numericCell(static_cast<double>(index + 1), "%.0f");
            ImGui::TableSetColumnIndex(1);
            const char* label =
                row.label.starts_with("lmx.pass.") ? row.label.c_str() + 9 : row.label.c_str();
            if (ImGui::Selectable(label, selected == &row, ImGuiSelectableFlags_SpanAllColumns))
                selectPass(index);
            passHelp(row);
            ImGui::TableSetColumnIndex(2);
            numericCell(row.averageGpuMilliseconds);
            ImGui::TableSetColumnIndex(3);
            numericCell(row.latestGpuMilliseconds);
            ImGui::TableSetColumnIndex(4);
            numericCell(row.minimumGpuMilliseconds);
            ImGui::TableSetColumnIndex(5);
            numericCell(row.maximumGpuMilliseconds);
            ImGui::TableSetColumnIndex(6);
            numericCell(static_cast<double>(row.sampleCount), "%.0f");
            ImGui::PopID();
        }
        ImGui::EndTable();
        return selected;
    }
    const auto groups = groupPassStages(snapshot.passRows);
    const auto stageSort = sort == PassTimingSort::Schedule ? StageTimingSort::Schedule
                           : sort == PassTimingSort::Latest ? StageTimingSort::Latest
                                                            : StageTimingSort::Average;
    for (const size_t groupIndex : sortedStageTimingIndices(groups, stageSort, descending)) {
        const auto& group = groups[groupIndex];
        ImGui::PushID(group.stage.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        numericCell(group.firstSchedule + 1, "%.0f");
        ImGui::TableSetColumnIndex(1);
        const bool hasMembers = group.members.size() > 1;
        bool expanded = false;
        if (hasMembers) {
            expanded = ImGui::TreeNodeEx("##stage", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)",
                                         group.stage.c_str(), group.members.size());
            editorTooltip("Summed Average and Latest costs. Expand for individual pass timings; # "
                          "is the first pass in the schedule.");
        } else {
            const auto index = group.members.front();
            if (ImGui::Selectable(group.stage.c_str(), selected == &snapshot.passRows[index],
                                  ImGuiSelectableFlags_SpanAllColumns))
                selectPass(index);
            passHelp(snapshot.passRows[index]);
        }
        ImGui::TableSetColumnIndex(2);
        numericCell(group.averageMs);
        ImGui::TableSetColumnIndex(3);
        numericCell(group.latestMs);
        if (expanded) {
            std::vector<PassTimingSummary> memberRows;
            for (const auto index : group.members)
                memberRows.push_back(snapshot.passRows[index]);
            const auto memberSort = sort == PassTimingSort::Latest    ? PassTimingSort::Latest
                                    : sort == PassTimingSort::Average ? PassTimingSort::Average
                                                                      : PassTimingSort::Schedule;
            auto members = sortedPassTimingIndices(memberRows, memberSort, descending);
            if (sort == PassTimingSort::Schedule && descending)
                std::reverse(members.begin(), members.end());
            for (const auto member : members) {
                const auto index = group.members[member];
                const auto& row = snapshot.passRows[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                numericCell(static_cast<double>(index + 1), "%.0f");
                ImGui::TableSetColumnIndex(1);
                const char* label =
                    row.label.starts_with("lmx.pass.") ? row.label.c_str() + 9 : row.label.c_str();
                if (ImGui::Selectable(label, selected == &row, ImGuiSelectableFlags_SpanAllColumns))
                    selectPass(index);
                passHelp(row);
                ImGui::TableSetColumnIndex(2);
                numericCell(row.averageGpuMilliseconds);
                ImGui::TableSetColumnIndex(3);
                numericCell(row.latestGpuMilliseconds);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    return selected;
}

//======================================================================================================================
void drawFreshness(const PerformanceModel& model) {
    if (model.paused())
        editor_style::message(model.snapshot().waitingForSamples ? "Frozen — empty" : "Frozen");
    else if (model.snapshot().waitingForSamples)
        editor_style::message("Waiting for retired GPU samples");
    else if (model.stale())
        editor_style::message("Stale — no new retired GPU sample for 1 s", true);
}

} // namespace

//======================================================================================================================
void drawPerformancePanel(bool& open, PerformanceModel& model, PerformancePanelState& state,
                          MeasurementPanelContext* measurement) {
    if (!beginPerformanceWindow(open, state)) {
        ImGui::End();
        return;
    }
    if (!ImGui::BeginTabBar("PerformanceTabs")) {
        ImGui::End();
        return;
    }
    const bool showLive = editor_style::beginTabItem(
        "Live", state.requestLiveTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
    state.requestLiveTab = false;
    if (showLive) {
        static bool individualRows = false;
        editor_style::beginHeaderRow();
        if (editor_style::iconButton(
                "FreezeMetrics", model.paused() ? EditorIcon::Unlock : EditorIcon::Lock, true,
                model.paused() ? "Resume metrics; wait for fresh samples. Scene playback continues."
                               : "Freeze the metrics snapshot, including both Performance "
                                 "surfaces. Scene playback continues."))
            model.setPaused(!model.paused());
        ImGui::SameLine();
        if (editor_style::overflowMenu("MetricsMore")) {
            if (ImGui::MenuItem("Clear history"))
                model.clearHistory();
            editorTooltip("Discard timing and interval history. A frozen snapshot stays empty "
                          "until resumed.");
            ImGui::Separator();
            ImGui::MenuItem("Individual pass rows", nullptr, &individualRows);
            editorTooltip("Show every pass separately, including sortable Min, Max and Samples. "
                          "Turn off to group stages by their summed Average and Latest costs.");
            ImGui::EndPopup();
        }
        editor_style::endHeaderRow();
        const PerformanceSnapshot& snapshot = model.snapshot();
        drawFreshness(model);
        drawSummary(snapshot);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool wide = available.x >= editor_style::scaled(760.0f);
        const float detailsRowHeight = ImGui::GetFrameHeightWithSpacing();
        const float height =
            std::max(editor_style::scaled(48.0f),
                     available.y * 0.65f - detailsRowHeight - ImGui::GetStyle().ItemSpacing.y);
        const float tableWidth = wide ? available.x * 0.65f : available.x;
        const PassTimingSummary* selected = nullptr;
        if (ImGui::BeginChild("Costs", ImVec2(tableWidth, height))) {
            selected = drawPassTable(snapshot, height, individualRows);
        }
        ImGui::EndChild();
        if (wide) {
            ImGui::SameLine();
            if (ImGui::BeginChild("Intervals", ImVec2(0.0f, height))) {
                drawIntervalPlot(snapshot);
            }
            ImGui::EndChild();
        } else if (editor_style::collapsingHeader("Frame interval history")) {
            if (ImGui::BeginChild("Intervals", ImVec2(0.0f, editor_style::scaled(130.0f)))) {
                drawIntervalPlot(snapshot);
            }
            ImGui::EndChild();
        }
        const char* detailsLabel = selected != nullptr
                                       ? "Selected pass & metric details###MetricDetails"
                                       : "Metric definitions & exact memory###MetricDetails";
        if (editor_style::collapsingHeader(detailsLabel)) {
            ImGui::TextWrapped(
                "Frame %llu | published %.2f s | %zu / %zu samples | %.0f updates/s",
                static_cast<unsigned long long>(snapshot.frameId), snapshot.publishedAtSeconds,
                snapshot.passRows.empty() ? size_t{0} : snapshot.passRows.front().sampleCount,
                PassTimingHistory::kSampleCapacity,
                1.0f / PerformanceModel::kRepublishIntervalSeconds);
            if (selected != nullptr) {
                ImGui::TextWrapped("%s", selected->label.c_str());
                ImGui::TextWrapped("Range %.3f-%.3f ms | %zu / %zu samples",
                                   selected->minimumGpuMilliseconds,
                                   selected->maximumGpuMilliseconds, selected->sampleCount,
                                   PassTimingHistory::kSampleCapacity);
                if (ImGui::SmallButton("Copy pass ID")) {
                    ImGui::SetClipboardText(selected->label.c_str());
                }
                editorTooltip(
                    "Copy the full renderer pass label for logs, captures or the Render Graph.");
            }
            if (!snapshot.waitingForSamples) {
                ImGui::Text("CPU classify / prepare: %.3f / %.3f ms · declared frame %llu",
                            snapshot.renderingTimings.classifyMilliseconds,
                            snapshot.renderingTimings.prepareMilliseconds,
                            static_cast<unsigned long long>(snapshot.frameId));
                editorTooltip("CPU classification and list/argument preparation recorded when this "
                              "retained frame was declared.");
            }
            const auto timingRow = [](const char* label,
                                      const std::optional<PerformanceTimingReading>& reading) {
                if (reading)
                    ImGui::Text("%s: %.3f ms · frame %llu", label, reading->milliseconds,
                                static_cast<unsigned long long>(reading->frameId));
                else
                    ImGui::Text("%s: N/A", label);
            };
            timingRow("Latest compatible GPU pass sum", snapshot.renderingTimings.compatibleGpu);
            editorTooltip(
                "Newest compatible retired sum at snapshot publication; distinct from the "
                "rolling average above. Presentation, driver and untimed work are excluded.");
            timingRow("Last observed controller input", snapshot.renderingTimings.controllerInput);
            editorTooltip(
                "Last timing offered while dynamic resolution was active, not necessarily applied. "
                "The "
                "controller may skip a sample during settling or for an obsolete scale; this value "
                "remains when inactive. Freeze holds every reading.");
            ImGui::TextWrapped(
                "Timed pass sum averages the retained GPU pass samples; present, driver "
                "and untimed work are outside this sum. Latest is the newest retired frame. "
                "FPS uses the mean of the wall-clock interval history. Frame labels identify the "
                "newest retirement; seconds identify snapshot publication since startup. "
                "Freeze affects both Performance surfaces. Clear and Resume wait for new samples.");
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
        ImGui::EndTabItem();
    }
    if (measurement && editor_style::beginTabItem("Measure")) {
        drawMeasurementSection(*measurement);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::End();
}

//======================================================================================================================
bool drawPerformanceSummary(bool& open, const PerformanceModel& model) {
    bool details = false;
    ImGui::SetNextWindowSize(ImVec2(editor_style::scaled(480.0f), editor_style::scaled(220.0f)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kPerformanceSummaryWindowName, &open)) {
        const auto& snapshot = model.snapshot();
        editor_style::beginHeaderRow();
        details = editor_style::iconButton("PerformanceDetails", EditorIcon::Details, true,
                                           "Open Live details in the detached Performance window.");
        ImGui::SameLine();
        if (snapshot.frameIntervalsMs.empty())
            ImGui::TextUnformatted("Frame —");
        else
            ImGui::Text("Frame %.2f ms", snapshot.latestFrameIntervalMs);
        editorTooltip("Latest wall-clock frame interval; includes waiting and presentation.");
        editor_style::nextInRow(ImGui::CalcTextSize("GPU 000.000 ms").x);
        if (snapshot.waitingForSamples)
            ImGui::TextUnformatted("GPU —");
        else
            ImGui::Text("GPU %.3f ms", snapshot.timedPassSumMilliseconds);
        editorTooltip(
            "Rolling average sum of timed GPU passes, excluding present, driver and untimed work.");
        editor_style::endHeaderRow();
        drawFreshness(model);
        if (!snapshot.frameIntervalsMs.empty()) {
            ImGui::PlotLines("##FrameIntervals", snapshot.frameIntervalsMs.data(),
                             static_cast<int>(snapshot.frameIntervalsMs.size()), 0, nullptr, 0.0f,
                             FLT_MAX, ImVec2(-FLT_MIN, editor_style::scaled(34.0f)));
            editorTooltip("Wall-clock frame interval history (ms), oldest on the left; frozen with "
                          "the readings.");
        }
        const auto groups = groupPassStages(snapshot.passRows);
        const auto order = sortedStageTimingIndices(groups, StageTimingSort::Average);
        if (ImGui::BeginTable("CostliestStages", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Average ms", ImGuiTableColumnFlags_WidthFixed,
                                    editor_style::scaled(90.0f));
            for (size_t i = 0; i < std::min(size_t{3}, order.size()); ++i) {
                const auto& stage = groups[order[i]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(stage.stage.c_str());
                ImGui::TableNextColumn();
                numericCell(stage.averageMs);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    return details;
}

} // namespace lmx::app
