//----------------------------------------------------------------------------------------------------------------------
/// @file EditorMenus.cpp
/// @brief Draws global editor menus and forwards capture transitions to notices.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"
#include "App/Model/Scene/SelectionBounds.h"
#include "App/Panels/Console/ConsolePanel.h"
#include "App/Panels/Inspector/InspectorPanel.h"
#include "App/Panels/Rendering/RenderingPanel.h"
#include "App/Panels/Scene/ScenePanel.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "App/Panels/Viewport/ViewportPanel.h"

#include <imgui.h>

namespace lmx::app {

//======================================================================================================================
void EditorShell::buildMainMenu(const render::Renderer& renderer) {
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(m_measurement.active());
        const auto requested = drawSceneMenu(SceneMenuContext{
            .library = m_library, .activeSceneId = m_activeSceneId, .loading = m_sceneLoading});
        if (requested && *requested != m_activeSceneId)
            m_sceneLoading.request(*requested);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))
            m_actions.requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::BeginDisabled(m_measurement.active());
        if (ImGui::MenuItem("Reset Camera", "Home"))
            resetCamera();
        const bool canFrame = selectedObjectBounds(m_session.scene(), m_selection).has_value();
        if (ImGui::MenuItem("Frame Selected", "F", false, canFrame))
            frameSelected(renderer);
        editorTooltip(canFrame ? "Fit the selected object's bounds; stop camera-rail following."
                               : "Select an object with reliable geometry bounds in Hierarchy.");
        const bool outlineReady = m_selectionOutline->target().width() == renderer.width() &&
                                  m_selectionOutline->target().height() == renderer.height();
        ImGui::MenuItem("Selection Outline", nullptr, &m_showSelectionOutline,
                        m_selection.subject == EditorSubject::Object && outlineReady);
        editorTooltip(!outlineReady ? "Outline allocation failed. Resize the viewport to retry."
                      : m_selection.subject != EditorSubject::Object
                          ? "Select an object in Hierarchy to show its visible-geometry outline."
                          : "Outline the selected object's visible geometry.");
        if (ImGui::MenuItem("Editor Camera")) {
            m_selection = initialSelection(m_activeSceneId);
            setPanelVisible(EditorPanel::Inspector, true);
            ImGui::SetWindowFocus(kInspectorPanelWindowName);
        }
        if (ImGui::BeginMenu("Debug View")) {
            const auto active = activeDebugView(m_settings);
            if (ImGui::MenuItem("Final", nullptr, !active))
                selectDebugView(m_settings, std::nullopt);
            const auto entries = debugViewEntries(m_settings, viewportHzbLevels(renderer));
            for (auto topic :
                 {DebugViewTopic::Temporal, DebugViewTopic::Lighting, DebugViewTopic::Occlusion}) {
                const char* label = topic == DebugViewTopic::Temporal   ? "Temporal"
                                    : topic == DebugViewTopic::Lighting ? "Lighting"
                                                                        : "Occlusion";
                if (ImGui::BeginMenu(label)) {
                    for (const auto& entry : entries) {
                        if (entry.view.topic != topic)
                            continue;
                        const bool selected =
                            active && active->topic == topic && active->value == entry.view.value;
                        if (ImGui::MenuItem(entry.label.c_str(), nullptr, selected,
                                            entry.available))
                            selectDebugView(m_settings, entry.view);
                        if (!entry.available)
                            editorTooltip(entry.reason.c_str());
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
        if (ImGui::BeginMenu("UI Scale")) {
            const uint32_t current = m_workspace.uiScalePercent;
            if (ImGui::MenuItem("Zoom Out", "Cmd+-", false, current > kUiScalePresets.front()))
                setUiScale(stepUiScalePercent(current, false));
            if (ImGui::MenuItem("Zoom In", "Cmd++", false, current < kUiScalePresets.back()))
                setUiScale(stepUiScalePercent(current, true));
            if (ImGui::MenuItem("Reset UI Scale", "Cmd+0"))
                setUiScale(kDefaultUiScalePercent);
            ImGui::Separator();
            for (const uint32_t percent : kUiScalePresets) {
                const std::string label = std::to_string(percent) + "%";
                if (ImGui::MenuItem(label.c_str(), nullptr, current == percent))
                    setUiScale(percent);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        const auto visibilityItem = [this](const char* label, EditorPanel panel) {
            bool visible = m_workspace.visibility.isVisible(panel);
            if (ImGui::MenuItem(label, nullptr, &visible))
                setPanelVisible(panel, visible);
        };
        visibilityItem(kScenePanelWindowName, EditorPanel::Scene);
        visibilityItem(kViewportPanelWindowName, EditorPanel::Viewport);
        visibilityItem(kInspectorPanelWindowName, EditorPanel::Inspector);
        visibilityItem(kRenderingPanelWindowName, EditorPanel::Rendering);
        visibilityItem(kPerformancePanelWindowName, EditorPanel::Performance);
        visibilityItem(kRenderGraphPanelWindowName, EditorPanel::RenderGraph);
        visibilityItem(kConsolePanelWindowName, EditorPanel::Console);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Default Layout"))
            m_actions.requestResetLayout();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Capture Next GPU Frame", "C", false,
                            m_actions.captureAvailable() &&
                                m_actions.captureResult().status != ActionStatus::Pending))
            m_actions.requestCapture();
        editorTooltip(m_actions.captureAvailable()
                          ? "Capture the next acquired GPU frame for Xcode inspection."
                          : m_actions.captureResult().message.c_str());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::BeginMenu("Controls")) {
            ImGui::TextUnformatted("Hold RMB over the image to look.");
            ImGui::TextUnformatted("While held: WASD move, Q down, E up.");
            ImGui::TextUnformatted("Release RMB to return to editing.");
            ImGui::TextUnformatted("Home resets the camera; F frames the selected object.");
            ImGui::TextUnformatted("C captures the next GPU frame when capture is enabled.");
            ImGui::TextUnformatted("Text editing, popups and RMB look suppress these shortcuts.");
            const auto lab = labDescription(m_activeSceneId);
            if (!lab.empty()) {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(420.0f));
                ImGui::TextUnformatted(lab.data(), lab.data() + lab.size());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    buildPlaybackTransport();
    ImGui::EndMainMenuBar();
}

//======================================================================================================================
void EditorShell::postCaptureNotice() {
    const auto& result = m_actions.captureResult();
    if (m_actions.captureFeedbackVisible() && (result.status != m_lastCaptureNotice.status ||
                                               result.message != m_lastCaptureNotice.message ||
                                               result.path != m_lastCaptureNotice.path)) {
        m_notices.post(result, ImGui::GetTime());
        m_lastCaptureNotice = result;
    }
}

} // namespace lmx::app
