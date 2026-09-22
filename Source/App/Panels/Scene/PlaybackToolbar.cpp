//----------------------------------------------------------------------------------------------------------------------
/// @file PlaybackToolbar.cpp
/// @brief Draws fixed-height vector playback controls above the editor docking area.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/PlaybackToolbar.h"

#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>

namespace lmx::app {
namespace {

enum class TransportIcon { Play, Pause, Stop, Step, Options };

//======================================================================================================================
void tooltip(std::string_view text) {
    if (!text.empty() && (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                               ImGuiHoveredFlags_AllowWhenDisabled) ||
                          (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()))) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

//======================================================================================================================
void drawIcon(TransportIcon icon, ImVec2 center, float radius, ImU32 color) {
    auto* draw = ImGui::GetWindowDrawList();
    const float thickness = editor_style::scaled(1.6f);
    switch (icon) {
    case TransportIcon::Play:
        draw->AddTriangleFilled({center.x - radius * 0.65f, center.y - radius},
                                {center.x - radius * 0.65f, center.y + radius},
                                {center.x + radius, center.y}, color);
        break;
    case TransportIcon::Pause:
        for (const float offset : {-0.75f, 0.25f}) {
            draw->AddRectFilled({center.x + radius * offset, center.y - radius},
                                {center.x + radius * (offset + 0.5f), center.y + radius}, color);
        }
        break;
    case TransportIcon::Stop:
        draw->AddRectFilled({center.x - radius * 0.8f, center.y - radius * 0.8f},
                            {center.x + radius * 0.8f, center.y + radius * 0.8f}, color);
        break;
    case TransportIcon::Step:
        draw->AddTriangleFilled({center.x - radius, center.y - radius},
                                {center.x - radius, center.y + radius},
                                {center.x + radius * 0.4f, center.y}, color);
        draw->AddRectFilled({center.x + radius * 0.6f, center.y - radius},
                            {center.x + radius, center.y + radius}, color);
        break;
    case TransportIcon::Options:
        draw->AddLine({center.x - radius * 0.7f, center.y - radius * 0.25f},
                      {center.x, center.y + radius * 0.4f}, color, thickness);
        draw->AddLine({center.x, center.y + radius * 0.4f},
                      {center.x + radius * 0.7f, center.y - radius * 0.25f}, color, thickness);
        break;
    }
}

//======================================================================================================================
bool iconButton(const char* id, TransportIcon icon, bool enabled, bool selected,
                std::string_view help) {
    ImGui::BeginDisabled(!enabled);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
    }
    const float size = editor_style::scaled(32.0f);
    const bool pressed = ImGui::Button(id, {size, size});
    const auto minimum = ImGui::GetItemRectMin();
    drawIcon(icon, {minimum.x + size * 0.5f, minimum.y + size * 0.5f}, editor_style::scaled(6.5f),
             ImGui::GetColorU32(ImGuiCol_Text));
    if (selected) {
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    tooltip(help);
    return pressed;
}

//======================================================================================================================
void fixedText(std::string_view text, float width, bool muted = false) {
    const auto minimum = ImGui::GetCursorScreenPos();
    const float height = editor_style::scaled(32.0f);
    ImGui::Dummy({width, height});
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(minimum, {minimum.x + width, minimum.y + height}, true);
    draw->AddText({minimum.x, minimum.y + (height - ImGui::GetFontSize()) * 0.5f},
                  ImGui::GetColorU32(muted ? ImGuiCol_TextDisabled : ImGuiCol_Text), text.data(),
                  text.data() + text.size());
    draw->PopClipRect();
    tooltip(text);
}

} // namespace

//======================================================================================================================
PlaybackToolbarAction drawPlaybackToolbar(const PlaybackToolbarContext& context) {
    PlaybackToolbarAction action = PlaybackToolbarAction::None;
    const float height = editor_style::scaled(44.0f);
    const float buttonHeight = editor_style::scaled(32.0f);
    const float spacing = editor_style::scaled(6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(spacing, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(editor_style::scaled(8.0f),
                               std::max(0.0f, (buttonHeight - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0.0f));
    const auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                       ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##PlaybackToolbar", ImGui::GetMainViewport(), ImGuiDir_Up,
                                    height, flags)) {
        const float available = ImGui::GetContentRegionAvail().x;
        const float modeWidth = editor_style::scaled(104.0f);
        const float timeWidth = editor_style::scaled(96.0f);
        const float fixedWidth = modeWidth + timeWidth + buttonHeight * 4.0f + spacing * 6.0f;
        const float statusWidth =
            std::clamp(available - fixedWidth, 0.0f, editor_style::scaled(160.0f));
        const float groupWidth = fixedWidth + statusWidth;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (available - groupWidth) * 0.5f));
        ImGui::BeginDisabled(context.active || context.measurementActive);
        ImGui::SetNextItemWidth(modeWidth);
        if (ImGui::BeginCombo("##RunMode", context.measureOnPlay ? "Measure" : "Scene")) {
            if (ImGui::Selectable("Scene", !context.measureOnPlay)) {
                context.measureOnPlay = false;
            }
            if (ImGui::Selectable("Measure", context.measureOnPlay)) {
                context.measureOnPlay = true;
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        tooltip(context.active || context.measurementActive
                    ? "Run mode is locked until Stop."
                    : "Scene plays the scene. Measure starts the plan configured in Performance.");
        ImGui::SameLine();
        const bool showPause = context.playing || context.measurementActive;
        const bool toggleEnabled =
            !context.measurementActive && (!context.measureOnPlay || context.canMeasure);
        const std::string_view playHelp =
            context.measureOnPlay
                ? (!context.canMeasure && !context.disabledReason.empty()
                       ? context.disabledReason
                       : "Play: start the measurement plan configured in Performance.")
                : "Play: start or resume scene playback, including camera preview in a static "
                  "scene.";
        const std::string_view toggleHelp =
            context.measurementActive
                ? "Pause unavailable in Measure mode: measurements use a fixed, uninterrupted "
                  "sequence. Use Stop to cancel an active measurement."
            : showPause ? "Pause: hold scene time and retain the starting state for Stop."
                        : playHelp;
        if (iconButton("##PlayPause", showPause ? TransportIcon::Pause : TransportIcon::Play,
                       toggleEnabled, showPause, toggleHelp)) {
            action = showPause ? PlaybackToolbarAction::Pause : PlaybackToolbarAction::Play;
        }
        ImGui::SameLine();
        if (iconButton("##Stop", TransportIcon::Stop, context.active || context.measurementActive,
                       false,
                       "Stop: end the run and restore its starting scene and camera state.")) {
            action = PlaybackToolbarAction::Stop;
        }
        ImGui::SameLine();
        if (iconButton(
                "##Step", TransportIcon::Step,
                !context.playing && !context.measurementActive && !context.measureOnPlay, false,
                context.measureOnPlay
                    ? "Step unavailable in Measure mode. Select Scene while stopped."
                : context.playing
                    ? "Pause scene playback before stepping."
                    : "Step: advance scene time by exactly 1/60 second, then stay paused.")) {
            action = PlaybackToolbarAction::Step;
        }
        ImGui::SameLine();
        fixedText(std::format("{:.3f} s", context.timeSeconds), timeWidth);
        ImGui::SameLine();
        const std::string_view status = context.status.empty()
                                            ? (context.measurementActive ? "Measuring"
                                               : context.playing         ? "Playing"
                                               : context.active          ? "Paused"
                                                                         : "Stopped")
                                            : context.status;
        fixedText(status, statusWidth, true);
        ImGui::SameLine();
        if (iconButton("##PlaybackOptions", TransportIcon::Options, true, false,
                       "Playback options")) {
            ImGui::OpenPopup("playback-options");
        }
        if (ImGui::BeginPopup("playback-options")) {
            if (ImGui::MenuItem("Show measurement"))
                action = PlaybackToolbarAction::ShowMeasurement;
            tooltip("Open Performance to configure or inspect measurement. Closing it does not "
                    "stop a run.");
            ImGui::Separator();
            ImGui::BeginDisabled(!context.hasCameraRail || context.measurementActive);
            ImGui::Checkbox("Follow camera rail", &context.followCameraRail);
            ImGui::EndDisabled();
            tooltip(context.measurementActive ? "Camera rail settings are fixed during measurement."
                    : context.hasCameraRail   ? "Follow the authored camera path at scene time."
                                              : "This scene has no authored camera rail.");
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    return action;
}

} // namespace lmx::app
