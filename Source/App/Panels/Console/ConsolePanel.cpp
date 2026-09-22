//----------------------------------------------------------------------------------------------------------------------
/// @file ConsolePanel.cpp
/// @brief Displays bounded log snapshots with filtering and controlled auto-scroll.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Console/ConsolePanel.h"

#include "App/Panels/ActionFeedback.h"
#include "App/Panels/EditorStyle.h"

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
void nextControl(float width) {
    const float contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (contentRight - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine();
    }
}

//======================================================================================================================
ImVec4 severityColor(ConsoleSeverity severity) {
    if (severity >= ConsoleSeverity::Error)
        return {1.0f, 0.48f, 0.45f, 1.0f};
    if (severity == ConsoleSeverity::Warning)
        return editor_style::kWarning;
    return severity < ConsoleSeverity::Info ? editor_style::kMuted
                                            : ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

} // namespace

//======================================================================================================================
void drawConsolePanel(bool& open, ConsoleModel& model) {
    if (ImGui::Begin(kConsolePanelWindowName, &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        model.refresh();
        if (ImGui::Button(model.frozen() ? "Resume display" : "Freeze display")) {
            model.setFrozen(!model.frozen());
        }
        editorTooltip("Freeze only the displayed messages and counters. Logging continues; Resume "
                      "shows the current retained history.");
        nextControl(ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button("Clear"))
            model.clear();
        editorTooltip("Clear retained and displayed messages plus loss counters. Freeze state and "
                      "filters are retained; later messages continue to arrive.");
        nextControl(ImGui::CalcTextSize("Copy visible").x + ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button("Copy visible")) {
            const auto text = consoleVisibleText(model.snapshot(), model.filter);
            if (SDL_SetClipboardText(text.c_str())) {
                const auto count =
                    std::ranges::count_if(model.snapshot().entries, [&](const auto& entry) {
                        return consoleEntryMatches(entry, model.filter);
                    });
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
        editorTooltip("Copy exactly the messages matching the current severity/search filters in "
                      "the displayed snapshot, with UTC timestamps and severity.");
        nextControl(ImGui::CalcTextSize("Follow newest").x + ImGui::GetFrameHeight() +
                    ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Checkbox("Follow newest", &model.autoScroll);
        editorTooltip("Follow new messages only while already scrolled to the end. Scroll up to "
                      "inspect earlier output without being pulled down.");

        constexpr const char* kSeverityNames[] = {"All severities",  "Debug and above",
                                                  "Info and above",  "Warning and above",
                                                  "Error and above", "Critical only"};
        int severity = static_cast<int>(model.filter.minimumSeverity);
        ImGui::SetNextItemWidth(
            std::min(editor_style::scaled(180.0f), ImGui::GetContentRegionAvail().x));
        if (ImGui::Combo("##severity", &severity, kSeverityNames, 6)) {
            model.filter.minimumSeverity = static_cast<ConsoleSeverity>(severity);
        }
        editorTooltip("Show this severity and more important messages. Hidden messages remain in "
                      "bounded storage.");
        nextControl(editor_style::scaled(200.0f));
        std::array<char, 256> search{};
        std::snprintf(search.data(), search.size(), "%s", model.filter.search.c_str());
        ImGui::SetNextItemWidth(
            std::max(editor_style::scaled(40.0f), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputTextWithHint("##search", "Search log messages...", search.data(),
                                     search.size())) {
            model.filter.search = search.data();
        }
        editorTooltip("Case-insensitive message substring filter. This field searches existing "
                      "logs and never executes commands.");
        const auto& snapshot = model.snapshot();
        size_t visible = 0;
        for (const auto& entry : snapshot.entries)
            visible += consoleEntryMatches(entry, model.filter) ? 1 : 0;
        ImGui::TextWrapped(
            "%s | %zu / %zu messages | %.1f KiB / 2048 KiB | Evicted %llu | Truncated %llu",
            model.frozen() ? "Frozen display; logging continues" : "Live", visible,
            snapshot.entries.size(), static_cast<double>(snapshot.payloadBytes) / 1024.0,
            static_cast<unsigned long long>(snapshot.evictedEntries),
            static_cast<unsigned long long>(snapshot.truncatedMessages));
        if (model.clipboardResult.status != ActionStatus::Ready) {
            drawActionFeedback("console-clipboard", model.clipboardResult);
        }
        if (ImGui::BeginChild("ConsoleMessages", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            const bool wasAtEnd =
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - editor_style::scaled(1.0f);
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
            if (model.autoScroll && wasAtEnd)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace lmx::app
