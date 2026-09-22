//----------------------------------------------------------------------------------------------------------------------
/// @file ActionFeedback.cpp
/// @brief Handles output-path actions and displays their recoverable failures.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Shared/ActionFeedback.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace lmx::app {
namespace {

//======================================================================================================================
std::string revealOutput(std::string_view output) {
    if (output.empty()) {
        return "No output path is available. Run the action again.";
    }
    std::error_code error;
    const auto path = std::filesystem::absolute(output, error);
    if (error) {
        return "Cannot resolve the output path: " + error.message();
    }
    if (!std::filesystem::exists(path, error)) {
        return error ? "Cannot access the output: " + error.message()
                     : "The output no longer exists. Run the action again to "
                       "recreate it.";
    }
    const std::string absolute = path.string();
    // Absolute paths are passed as one argv entry, never interpreted by a shell.
    // open without -W completes after the Finder request rather than waiting for
    // the Finder window to close.
    const char* arguments[] = {"/usr/bin/open", "-R", absolute.c_str(), nullptr};
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    if (!process) {
        return std::format("Cannot ask Finder to reveal the output: {}", SDL_GetError());
    }
    int exitCode = 0;
    const bool finished = SDL_WaitProcess(process, true, &exitCode);
    std::string failure;
    if (!finished) {
        failure = std::format("Cannot complete the Finder request: {}", SDL_GetError());
    } else if (exitCode != 0) {
        failure = std::format("Finder could not reveal the output (open exited {}). "
                              "Check access to the path and retry.",
                              exitCode);
    }
    SDL_DestroyProcess(process);
    return failure;
}

} // namespace

//======================================================================================================================
void drawActionFeedback(const char* id, ActionResult& result) {
    ImGui::PushID(id);
    ImGui::TextWrapped("%s: %s", actionStatusName(result.status), result.message.c_str());
    if (!result.path.empty()) {
        ImGui::TextWrapped("%s", result.path.c_str());
        if (ImGui::Button("Copy path")) {
            result.pathActionError = SDL_SetClipboardText(result.path.c_str())
                                         ? std::string{}
                                         : std::format("Copy path failed: {}", SDL_GetError());
        }
        if (result.status == ActionStatus::Succeeded) {
            ImGui::SameLine();
            if (ImGui::Button("Reveal in Finder")) {
                result.pathActionError = revealOutput(result.path);
            }
        }
    }
    if (!result.pathActionError.empty()) {
        ImGui::TextWrapped("Output action failed: %s", result.pathActionError.c_str());
    }
    ImGui::PopID();
}

} // namespace lmx::app
