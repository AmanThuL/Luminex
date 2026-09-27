//----------------------------------------------------------------------------------------------------------------------
/// @file ConsolePanel.cpp
/// @brief Displays bounded log snapshots with filtering and automatic freeze-on-scroll.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Console/ConsolePanel.h"

#include "App/Panels/Shared/ActionFeedback.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <string>

namespace lmx::app {
namespace {

//======================================================================================================================
ImVec4 severityColor(log::Level severity) {
    if (severity >= log::Level::Error)
        return {1.0f, 0.48f, 0.45f, 1.0f};
    if (severity == log::Level::Warning)
        return editor_style::kWarning;
    return severity < log::Level::Info ? editor_style::kMuted
                                       : ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

//======================================================================================================================
size_t visibleCount(const ConsoleModel& model) {
    return static_cast<size_t>(
        std::ranges::count_if(model.snapshot().entries, [&](const auto& entry) {
            return consoleEntryMatches(entry, model.filter);
        }));
}

//======================================================================================================================
void copyVisible(ConsoleModel& model) {
    const auto text = consoleVisibleText(model.snapshot(), model.filter);
    if (SDL_SetClipboardText(text.c_str())) {
        const auto count = visibleCount(model);
        model.clipboardResult = {
            ActionStatus::Succeeded,
            std::format("Copied {} matching message{}.", count, count == 1 ? "" : "s"),
            {}};
    } else {
        model.clipboardResult = {
            ActionStatus::Failed,
            std::format("Copy visible failed: {}. Check clipboard access and retry.",
                        SDL_GetError()),
            {}};
    }
}

//======================================================================================================================
void drawSearch(ConsoleModel& model, float width) {
    const bool showClear = !model.filter.search.empty();
    const float clearWidth = showClear ? editor_style::iconButtonWidth(EditorIcon::Close) : 0.0f;
    const auto position = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        position, {position.x + width, position.y + ImGui::GetFrameHeight()},
        ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, ImGui::GetStyle().ItemSpacing.y});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.0f, 0.0f, 0.0f, 0.0f});
    std::array<char, 256> search{};
    std::snprintf(search.data(), search.size(), "%s", model.filter.search.c_str());
    ImGui::SetNextItemWidth(std::max(1.0f, width - clearWidth));
    if (ImGui::InputTextWithHint("##search", "Search messages...", search.data(), search.size()))
        model.filter.search = search.data();
    ImGui::PopStyleColor();
    const auto& snapshot = model.snapshot();
    const auto tip =
        std::format("Case-insensitive message search; never executes commands.\n"
                    "{} / {} matching messages; {:.1f} / 2048 KiB; {} evicted; {} truncated.\n"
                    "Scroll up to hold this view; return to the bottom or click the new-message "
                    "chip to resume.",
                    visibleCount(model), snapshot.entries.size(), snapshot.payloadBytes / 1024.0,
                    snapshot.evictedEntries, snapshot.truncatedMessages);
    editorTooltip(tip.c_str());
    if (showClear) {
        ImGui::SameLine();
        if (editor_style::iconButton("ClearSearch", EditorIcon::Close, true,
                                     "Clear message search."))
            model.filter.search.clear();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

//======================================================================================================================
bool drawHeader(ConsoleModel& model) {
    editor_style::beginHeaderRow();
    const auto counts = consoleSeverityCounts(model.snapshot());
    constexpr const char* kShortNames[] = {"T", "D", "I", "W", "E", "C"};
    std::array<std::string, 6> labels;
    const auto& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    float actionWidth = editor_style::iconButtonWidth(EditorIcon::More) + spacing;
    const std::string arrivals = std::format("↓ {} new", model.newSinceFreeze());
    if (model.frozen())
        actionWidth += ImGui::CalcTextSize(arrivals.c_str()).x + style.FramePadding.x * 2 + spacing;
    for (size_t level = 0; level < labels.size(); ++level) {
        labels[level] = std::format("{} {}", kShortNames[level], counts[level]);
        actionWidth +=
            ImGui::CalcTextSize(labels[level].c_str()).x + style.FramePadding.x * 2 + spacing;
    }
    drawSearch(model, std::max(editor_style::scaled(80.0f),
                               ImGui::GetContentRegionAvail().x - actionWidth));
    for (size_t level = 0; level < labels.size(); ++level) {
        ImGui::SameLine();
        const auto severity = static_cast<log::Level>(level);
        // The chips set a minimum, so every level the filter shows reads as selected.
        const bool selected = severity >= model.filter.minimumSeverity;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushID(static_cast<int>(level));
        if (ImGui::Button((labels[level] + "###severity").c_str()))
            model.filter.minimumSeverity = severity;
        ImGui::PopID();
        if (selected)
            ImGui::PopStyleColor();
        const auto tip = std::format("{}: {} retained messages at this level. Show {} and above. "
                                     "Search still applies; hidden messages remain retained.",
                                     consoleSeverityName(severity), counts[level],
                                     consoleSeverityName(severity));
        editorTooltip(tip.c_str());
    }
    ImGui::SameLine();
    if (editor_style::overflowMenu("ConsoleActions")) {
        if (ImGui::MenuItem("Clear"))
            model.clear();
        editorTooltip("Clear displayed and retained messages and loss counters. A frozen view "
                      "stays frozen; its new-message count starts at Clear.");
        if (ImGui::MenuItem("Copy visible"))
            copyVisible(model);
        editorTooltip("Copy the displayed matching messages with UTC time and severity. A frozen "
                      "view copies its held messages while ingestion continues.");
        ImGui::EndPopup();
    }
    bool resumed = false;
    if (model.frozen()) {
        ImGui::SameLine();
        if (ImGui::Button((arrivals + "###resumeConsole").c_str())) {
            model.resumeAtEnd();
            resumed = true;
        }
        editorTooltip("This view is frozen; logging continues. Resume the current retained history "
                      "and follow its newest message.");
    }
    editor_style::endHeaderRow();
    return resumed;
}

} // namespace

//======================================================================================================================
void drawConsolePanel(bool& open, ConsoleModel& model) {
    if (ImGui::Begin(kConsolePanelWindowName, &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        const bool resumed = drawHeader(model);
        const auto& snapshot = model.snapshot();
        if (snapshot.evictedEntries != 0 || snapshot.truncatedMessages != 0) {
            ImGui::TextWrapped("Evicted %llu | Truncated %llu",
                               static_cast<unsigned long long>(snapshot.evictedEntries),
                               static_cast<unsigned long long>(snapshot.truncatedMessages));
        }
        if (model.clipboardResult.status != ActionStatus::Ready)
            drawActionFeedback("console-clipboard", model.clipboardResult);
        if (ImGui::BeginChild("ConsoleMessages", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            const float scrollY = ImGui::GetScrollY();
            const bool atEnd = scrollY >= ImGui::GetScrollMaxY() - editor_style::scaled(1.0f);
            auto* storage = ImGui::GetStateStorage();
            const ImGuiID scrollKey = ImGui::GetID("previousScrollY");
            const float previousScrollY = storage->GetFloat(scrollKey, scrollY);
            // Inspect the old content before refreshing: eviction must not move the reader's lines.
            // Shrinking content after Clear/filtering is not a request to resume a frozen view,
            // but a downward wheel at a clamped end is, since the reader cannot scroll further.
            const bool wheelDown = ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel < 0.0f;
            if (!resumed) {
                if (model.frozen() && atEnd && (scrollY > previousScrollY || wheelDown))
                    model.setScrolledToEnd(true);
                else if (!model.frozen() && !atEnd && scrollY < previousScrollY)
                    model.setScrolledToEnd(false);
            }
            storage->SetFloat(scrollKey, scrollY);
            model.refresh();
            const auto& snapshot = model.snapshot();
            const size_t visible = visibleCount(model);
            if (snapshot.entries.empty()) {
                editor_style::message(model.frozen()
                                          ? "Frozen display is empty. Resume to see new messages."
                                          : "No retained messages.");
            } else if (visible == 0) {
                editor_style::message("No messages match these filters.");
            } else if (ImGui::BeginTable("ConsoleEntries", 3,
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                             ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Time (UTC)", ImGuiTableColumnFlags_WidthFixed,
                                        editor_style::scaled(108.0f));
                ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed,
                                        editor_style::scaled(64.0f));
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const auto& entry : snapshot.entries) {
                    if (!consoleEntryMatches(entry, model.filter))
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(consoleTimestamp(entry.timestampMilliseconds).c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Text, severityColor(entry.severity));
                    ImGui::TextUnformatted(
                        std::string(consoleSeverityName(entry.severity)).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", entry.message.c_str());
                    if (entry.truncated)
                        ImGui::TextUnformatted("[message truncated at 16 KiB]");
                    ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
            if (!model.frozen())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace lmx::app
