//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the Scene panel's scene-catalog selector.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ScenePanel.h"

#include <imgui.h>

#include <span>
#include <string>

namespace lmx::app {

//======================================================================================================================
std::optional<engine::SceneId> drawScenePanel(bool& open, const engine::SceneLibrary& library,
                                              engine::SceneId activeSceneId) {
    std::optional<engine::SceneId> chosen;
    if (ImGui::Begin(kScenePanelWindowName, &open)) {
        const std::span<const engine::SceneEntry> entries = library.entries();
        if (ImGui::BeginCombo("Scene", library.entry(activeSceneId).displayName.data())) {
            for (size_t i = 0; i < entries.size(); ++i) {
                const engine::SceneEntry& entry = entries[i];
                ImGui::PushID(static_cast<int>(i));
                if (!entry.available) {
                    ImGui::BeginDisabled();
                }
                // Show availability hints inline because disabled entries cannot be hovered
                // reliably.
                const std::string label =
                    entry.hint.empty() ? std::string(entry.displayName)
                                       : std::string(entry.displayName) + " (" + entry.hint + ")";
                if (ImGui::Selectable(label.c_str(), entry.id == activeSceneId)) {
                    chosen = entry.id;
                }
                if (!entry.available) {
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::End();
    return chosen;
}

} // namespace lmx::app
