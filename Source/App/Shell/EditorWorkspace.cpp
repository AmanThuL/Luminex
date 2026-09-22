//----------------------------------------------------------------------------------------------------------------------
/// @file EditorWorkspace.cpp
/// @brief Implements workspace persistence, default docking, and UI-scale controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Panels/Console/ConsolePanel.h"
#include "App/Panels/Inspector/InspectorPanel.h"
#include "App/Panels/Scene/ScenePanel.h"
#include "App/Panels/Viewport/ViewportPanel.h"
#include "Core/Diagnostics/Assert.h"

#include <imgui.h>

// DockBuilder and ImGuiSettingsHandler are internal ImGui APIs; contain those calls here.
#include <imgui_internal.h>

#include <algorithm>
#include <string_view>

namespace lmx::app {

namespace {

// The default topology's share of the work area: Scene and Inspector flank a central column whose
// lower quarter holds Console, and the Viewport takes what remains.

// The side panels are never narrower than this while the Viewport still has room to spare.
constexpr float kMinSceneWidthPoints = 220.0f;
constexpr float kMinInspectorWidthPoints = 320.0f;
// When it does not, the Viewport wins and the side and lower panels give these back first.
constexpr float kMinViewportWidthPoints = 640.0f;
constexpr float kMinViewportHeightPoints = 360.0f;

// Luminex's own entry in imgui.ini, written as "[LuminexWorkspace][Workspace]". ImGui disallows
// '[' and ']' in a handler type name and hashes it to route the section back to this handler.
constexpr const char* kWorkspaceSettingsType = "LuminexWorkspace";
constexpr const char* kWorkspaceSettingsName = "Workspace";

//======================================================================================================================
// ImGui settings handlers are C function pointers, so each one recovers the shell's workspace
// storage from the handler's UserData rather than from a process-wide global.
WorkspaceSettings& workspaceSettingsOf(ImGuiSettingsHandler* handler) {
    LMX_ASSERT(handler != nullptr && handler->UserData != nullptr,
               "workspace settings handler: UserData must name the shell's WorkspaceSettings");
    return *static_cast<WorkspaceSettings*>(handler->UserData);
}

//======================================================================================================================
void workspaceSettingsClearAll(ImGuiContext*, ImGuiSettingsHandler* handler) {
    WorkspaceSettings& settings = workspaceSettingsOf(handler);
    settings.sectionSeen = false;
    settings.sectionText.clear();
}

//======================================================================================================================
void* workspaceSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
    if (name == nullptr || std::string_view(name) != kWorkspaceSettingsName) {
        // A future entry name under the same type; leaving the section unseen keeps an ini this
        // build cannot understand on the legacy path rather than half-reading it.
        return nullptr;
    }
    WorkspaceSettings& settings = workspaceSettingsOf(handler);
    settings.sectionSeen = true;
    settings.sectionText.clear();
    return &settings;
}

//======================================================================================================================
void workspaceSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry,
                               const char* line) {
    WorkspaceSettings& settings = *static_cast<WorkspaceSettings*>(entry);
    // ImGui strips the line terminator; parseWorkspaceSettings reads newline-separated text.
    settings.sectionText += line;
    settings.sectionText += '\n';
}

//======================================================================================================================
void workspaceSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler,
                               ImGuiTextBuffer* outBuffer) {
    const WorkspaceSettings& settings = workspaceSettingsOf(handler);
    outBuffer->appendf("[%s][%s]\n", kWorkspaceSettingsType, kWorkspaceSettingsName);
    outBuffer->append(writeWorkspaceSettings(kWorkspaceSchemaVersion, settings.visibility,
                                             settings.uiScalePercent)
                          .c_str());
    outBuffer->append("\n");
}

// The point sizes the default topology gives the three panels that flank the Viewport.
struct DefaultLayoutExtents {
    float sceneWidth = 0.0f;
    float inspectorWidth = 0.0f;
    float consoleHeight = 0.0f;
};

//======================================================================================================================
// The default proportions raised to the side minima, then relaxed by however much the Viewport's
// own minimum content region still needs: the flanking panels narrow proportionally rather than
// squeezing the image the workspace exists to show.
//
// The minima are applied before that relaxation rather than behind a work-area size test, so a
// 1280x720 window satisfies them even though its work area is shorter than 720 points once the menu
// bar is subtracted. A work area with room for both -- anything at least 1280 points wide -- always
// gets them, and a narrower one loses them to the Viewport in proportion either way.
DefaultLayoutExtents defaultLayoutExtents(float workWidth, float workHeight) {
    DefaultLayoutExtents extents;
    extents.sceneWidth = workWidth >= 1500.0f ? 240.0f : kMinSceneWidthPoints;
    extents.inspectorWidth = workWidth >= 1500.0f ? 340.0f : kMinInspectorWidthPoints;
    extents.consoleHeight = workHeight >= 900.0f ? 300.0f : 210.0f;

    const float sideBudget = std::max(workWidth - kMinViewportWidthPoints, 0.0f);
    const float sideWanted = extents.sceneWidth + extents.inspectorWidth;
    if (sideWanted > sideBudget && sideWanted > 0.0f) {
        const float scale = sideBudget / sideWanted;
        extents.sceneWidth *= scale;
        extents.inspectorWidth *= scale;
    }
    extents.consoleHeight =
        std::min(extents.consoleHeight, std::max(workHeight - kMinViewportHeightPoints, 0.0f));
    return extents;
}

//======================================================================================================================
// DockBuilderSplitNode requires a ratio strictly inside (0, 1), and a minimized window reports a
// zero-sized work area, so a share of the node being split is clamped rather than trusted.
float splitFraction(float extent, float available) {
    if (!(available > 0.0f)) {
        return 0.5f;
    }
    return std::clamp(extent / available, 0.05f, 0.95f);
}

} // namespace

//======================================================================================================================
void EditorShell::registerWorkspaceSettings() {
    ImGuiSettingsHandler workspaceHandler;
    workspaceHandler.TypeName = kWorkspaceSettingsType;
    workspaceHandler.TypeHash = ImHashStr(kWorkspaceSettingsType);
    workspaceHandler.ClearAllFn = workspaceSettingsClearAll;
    workspaceHandler.ReadOpenFn = workspaceSettingsReadOpen;
    workspaceHandler.ReadLineFn = workspaceSettingsReadLine;
    workspaceHandler.WriteAllFn = workspaceSettingsWriteAll;
    workspaceHandler.UserData = &m_workspace;
    ImGui::AddSettingsHandler(&workspaceHandler);
}

//======================================================================================================================
// Builds Scene/Inspector beside the Viewport and Console across the bottom.
// Performance and Render Graph live in independent native windows.
void EditorShell::buildDefaultLayout(uint32_t dockspaceId) {
    const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    const DefaultLayoutExtents extents = defaultLayoutExtents(work.x, work.y);

    // DockSpaceOverViewport already created this node; DockBuilder needs a fresh owned node.
    // Removing it also undocks every window it held, which is what keeps a repeated reset from
    // accumulating nodes or leaving a second copy of a panel docked elsewhere.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // Split ratios derive from the current node size, so size it first.
    ImGui::DockBuilderSetNodeSize(dockspaceId, work);

    // Each ratio is a share of the node being split, and that node shrinks as the splits proceed.
    ImGuiID centerId = dockspaceId;
    ImGuiID consoleId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down,
                                splitFraction(extents.consoleHeight, work.y), &consoleId,
                                &centerId);
    ImGuiID sceneId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, splitFraction(extents.sceneWidth, work.x),
                                &sceneId, &centerId);
    ImGuiID inspectorId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right,
                                splitFraction(extents.inspectorWidth, work.x - extents.sceneWidth),
                                &inspectorId, &centerId);

    ImGui::DockBuilderDockWindow(kScenePanelWindowName, sceneId);
    ImGui::DockBuilderDockWindow(kInspectorPanelWindowName, inspectorId);
    ImGui::DockBuilderDockWindow(kConsolePanelWindowName, consoleId);
    // Performance and Render Graph are deliberately absent: their window classes forbid docking
    // into an unclassed node, so each owns its own OS window and has no default dock node.
    ImGui::DockBuilderDockWindow(kViewportPanelWindowName, centerId);
    ImGui::DockBuilderFinish(dockspaceId);
}

//======================================================================================================================
void EditorShell::setUiScale(uint32_t percent) {
    percent = normalizedUiScalePercent(percent);
    if (m_workspace.uiScalePercent != percent) {
        m_workspace.uiScalePercent = percent;
        ImGui::MarkIniSettingsDirty();
    }
}

//======================================================================================================================
void EditorShell::updateUiScaleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    // ImGui's macOS behavior maps physical Command to its logical Ctrl modifier.
    const bool command = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
    const bool control = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    if (!command || control || io.KeyAlt || io.WantTextInput || io.AppFocusLost || m_looking ||
        ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, true));
    } else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
               ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, false));
    } else if (ImGui::IsKeyPressed(ImGuiKey_0, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
        setUiScale(kDefaultUiScalePercent);
    }
}

//======================================================================================================================
void EditorShell::setPanelVisible(EditorPanel panel, bool visible) {
    if (m_workspace.visibility.isVisible(panel) == visible) {
        return;
    }
    m_workspace.visibility.setVisible(panel, visible);
    if (panel == EditorPanel::Performance)
        m_performancePanel.requestFocus = visible;
    // Nothing moved a window, so ImGui has no reason of its own to rewrite the ini; without this
    // the new visibility would be lost on exit.
    ImGui::MarkIniSettingsDirty();
}

} // namespace lmx::app
