//----------------------------------------------------------------------------------------------------------------------
/// @file PlaybackToolbar.cpp
/// @brief Draws the scene transport with shared icon buttons in the main menu row.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Panels/Scene/PlaybackToolbar.h"

#include "App/Panels/Shared/EditorStyle.h"

#include <string>

namespace lmx::app {

//======================================================================================================================
float playbackToolbarButtonsWidth(const PlaybackToolbarContext& context) {
    const auto toggle =
        context.playing || context.measurementActive ? EditorIcon::Pause : EditorIcon::Play;
    float width =
        editor_style::iconButtonWidth(toggle) + editor_style::iconButtonWidth(EditorIcon::Stop) +
        editor_style::iconButtonWidth(EditorIcon::Step) + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    if (context.hasCameraRail)
        width += editor_style::iconButtonWidth(EditorIcon::Rail) + ImGui::GetStyle().ItemSpacing.x;
    return width;
}

//======================================================================================================================
PlaybackToolbarAction drawPlaybackToolbar(const PlaybackToolbarContext& context, bool showReadout) {
    PlaybackToolbarAction action = PlaybackToolbarAction::None;
    const bool showPause = context.playing || context.measurementActive;
    std::string playHelp =
        context.measurementActive
            ? "Pause unavailable during measurement. Use Stop to cancel the fixed sequence."
        : showPause ? "Pause: hold scene time and retain the starting state for Stop."
                    : "Play: start or resume scene playback, including camera preview.";
    if (!showReadout) {
        playHelp += "\n";
        playHelp += context.readout;
    }
    if (editor_style::iconButton("play-pause", showPause ? EditorIcon::Pause : EditorIcon::Play,
                                 !context.measurementActive, playHelp.c_str()))
        action = showPause ? PlaybackToolbarAction::Pause : PlaybackToolbarAction::Play;
    ImGui::SameLine();
    if (editor_style::iconButton(
            "stop", EditorIcon::Stop, context.active || context.measurementActive,
            "Stop: end the run and restore its starting scene and camera state."))
        action = PlaybackToolbarAction::Stop;
    ImGui::SameLine();
    if (editor_style::iconButton(
            "step", EditorIcon::Step, !context.playing && !context.measurementActive,
            context.measurementActive ? "Step unavailable during measurement."
            : context.playing
                ? "Pause scene playback before stepping."
                : "Step: advance scene time by exactly 1/60 second, then stay paused."))
        action = PlaybackToolbarAction::Step;
    if (showReadout) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(context.readout.data(),
                               context.readout.data() + context.readout.size());
    }
    if (context.hasCameraRail) {
        ImGui::SameLine();
        if (context.followCameraRail)
            ImGui::PushStyleColor(ImGuiCol_Button, editor_style::color(ThemeRole::SelectionBg));
        const bool changed = editor_style::iconButton(
            "rail", EditorIcon::Rail, !context.measurementActive,
            context.measurementActive  ? "Camera rail settings are fixed during measurement."
            : context.followCameraRail ? "Following camera rail. Click to stop following."
                                       : "Follow the authored camera rail at scene time.");
        if (context.followCameraRail)
            ImGui::PopStyleColor();
        if (changed)
            context.followCameraRail = !context.followCameraRail;
    }
    return action;
}

} // namespace lmx::app
