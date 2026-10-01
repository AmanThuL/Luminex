//----------------------------------------------------------------------------------------------------------------------
/// @file EditorMenus.cpp
/// @brief Renders the shared menu model and routes editor commands to their existing owners.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"
#include "App/Model/Scene/SelectionBounds.h"
#include "App/Panels/Inspector/InspectorPanel.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "App/Panels/Viewport/ViewportPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>

namespace lmx::app {

namespace {

//======================================================================================================================
std::string shortcutLabel(const std::optional<Shortcut>& shortcut) {
    if (!shortcut)
        return {};
    return std::string(shortcut->command ? "Cmd+" : "") + (shortcut->shift ? "Shift+" : "") +
           shortcut->key;
}

//======================================================================================================================
void menuTooltip(const MenuItem& item) {
    if (!item.enabled) {
        editorTooltip(item.disabledReason.c_str());
        return;
    }
    if (!item.command)
        return;
    switch (*item.command) {
    case MenuCommand::OpenCatalog:
        editorTooltip(std::format("Open {}. The current scene stays active until loading succeeds.",
                                  item.label)
                          .c_str());
        break;
    case MenuCommand::RetryScene:
        editorTooltip("Retry the failed catalog entry. The current scene and selection are kept if "
                      "it fails again.");
        break;
    case MenuCommand::SetSceneCamera:
        editorTooltip("Use this view as the scene camera on the next Save.");
        break;
    case MenuCommand::FrameSelected:
        editorTooltip("Fit the selected object's bounds; stop camera-rail following.");
        break;
    case MenuCommand::SelectionOutline:
        editorTooltip("Outline the selected object's visible geometry.");
        break;
    case MenuCommand::Capture:
        editorTooltip("Capture the next acquired GPU frame for Xcode inspection.");
        break;
    default:
        break;
    }
}

//======================================================================================================================
[[maybe_unused]] void drawMenuItems(EditorShell& shell, const std::vector<MenuItem>& items,
                                    const MenuContext& context, bool catalog = false) {
    std::optional<std::string> loading;
    for (size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (catalog && loading && item.separator)
            break;
        ImGui::PushID(static_cast<int>(index));
        if (item.separator) {
            ImGui::Separator();
        } else if (!item.children.empty()) {
            const bool opened = ImGui::BeginMenu(item.label.c_str(), item.enabled);
            menuTooltip(item);
            if (opened) {
                drawMenuItems(shell, item.children, context, item.label == "Open Scene");
                ImGui::EndMenu();
            }
        } else if (item.command) {
            bool chosen = false;
            if (item.command == MenuCommand::OpenCatalog) {
                ImGui::BeginDisabled(!item.enabled);
                chosen = ImGui::Selectable(item.label.c_str(), item.checked,
                                           ImGuiSelectableFlags_NoAutoClosePopups) &&
                         !item.checked;
                menuTooltip(item);
                ImGui::EndDisabled();
            } else if (item.command == MenuCommand::RetryScene) {
                ImGui::BeginDisabled(!item.enabled);
                chosen = ImGui::Button(item.label.c_str());
                menuTooltip(item);
                ImGui::EndDisabled();
            } else {
                const auto shortcut = shortcutLabel(item.shortcut);
                chosen = ImGui::MenuItem(item.label.c_str(),
                                         shortcut.empty() ? nullptr : shortcut.c_str(),
                                         item.checked, item.enabled);
                menuTooltip(item);
            }
            if (chosen) {
                shell.runMenuCommand(*item.command, item.argument);
                if (item.command == MenuCommand::OpenCatalog)
                    loading = item.label;
                else if (item.command == MenuCommand::RetryScene)
                    loading = context.retrySceneName;
            }
        } else {
            const bool wrap =
                catalog || (!context.labControls.empty() && item.label == context.labControls);
            if (wrap)
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                                       editor_style::scaled(catalog ? 360.0f : 420.0f));
            if (catalog)
                editor_style::message(item.label.c_str(), true);
            else
                ImGui::TextUnformatted(item.label.c_str());
            if (wrap)
                ImGui::PopTextWrapPos();
        }
        ImGui::PopID();
    }
    if (loading) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(360.0f));
        editor_style::message(std::format("Loading {}...", *loading).c_str());
        ImGui::PopTextWrapPos();
    }
}

} // namespace

//======================================================================================================================
MenuContext EditorShell::menuContext(const render::Renderer& renderer,
                                     const rojoRHI::Device& device) {
    MenuContext context{.documentIdle = m_documentWorkflow.step() == WorkflowStep::Idle,
                        .stopped = m_playback.state() == PlaybackState::Stopped,
                        .measuring = m_measurement.active(),
                        .canRetryScene = m_sceneLoading.failedScene().has_value(),
                        .canFrame =
                            selectedObjectBounds(m_session.scene(), m_selection).has_value(),
                        .objectSelected = m_selection.subject == EditorSubject::Object,
                        .outlineReady = m_selectionOutline->target().width() == renderer.width() &&
                                        m_selectionOutline->target().height() == renderer.height(),
                        .showOutline = m_showSelectionOutline,
                        .debug = activeDebugView(m_settings),
                        .debugEntries = debugViewEntries(m_settings, viewportHzbLevels(renderer),
                                                         effectiveReconstruction(renderer, device)),
                        .appearance = m_workspace.appearance.effective(),
                        .density = m_workspace.density,
                        .uiScalePercent = m_workspace.uiScalePercent,
                        .visibility = m_workspace.visibility,
                        .styleGallery = m_styleGallery.open,
                        .captureAvailable = m_actions.captureAvailable(),
                        .capturePending = m_actions.captureResult().status == ActionStatus::Pending,
                        .captureReason = m_actions.captureResult().message,
                        .labControls = std::string(labDescription(m_activeSceneId))};
    for (const auto& entry : m_library.entries())
        context.scenes.push_back(
            {entry.displayName, entry.available, entry.id == m_activeSceneId, entry.hint});
    if (const auto& failed = m_sceneLoading.failedScene()) {
        context.retrySceneName = m_library.entry(*failed).displayName;
        context.sceneFailure = std::format("{} could not load: {}\nCurrent scene kept. Fix the "
                                           "cause and retry, or choose another scene.",
                                           context.retrySceneName, m_sceneLoading.failureMessage());
    }
    return context;
}

//======================================================================================================================
void EditorShell::runMenuCommand(MenuCommand command, uint32_t argument) {
    switch (command) {
    case MenuCommand::Open:
        requestDocumentAction(DocumentAction::Open);
        break;
    case MenuCommand::OpenCatalog: {
        const auto entries = m_library.entries();
        if (argument < entries.size() && entries[argument].id != m_activeSceneId)
            requestDocumentAction(DocumentAction::OpenCatalog, entries[argument].id);
        break;
    }
    case MenuCommand::RetryScene:
        if (const auto& failed = m_sceneLoading.failedScene())
            requestDocumentAction(DocumentAction::OpenCatalog, *failed);
        break;
    case MenuCommand::Save:
        requestDocumentAction(DocumentAction::Save);
        break;
    case MenuCommand::SaveAs:
        requestDocumentAction(DocumentAction::SaveAs);
        break;
    case MenuCommand::Revert:
        requestDocumentAction(DocumentAction::Revert);
        break;
    case MenuCommand::Quit:
        requestQuit();
        break;
    case MenuCommand::SetSceneCamera:
        setSceneCamera();
        break;
    case MenuCommand::ResetCamera:
        resetCamera();
        break;
    case MenuCommand::FrameSelected:
        m_frameSelectionRequested = true;
        break;
    case MenuCommand::SelectionOutline:
        m_showSelectionOutline = !m_showSelectionOutline;
        break;
    case MenuCommand::EditorCamera:
        m_selection = initialSelection(m_activeSceneId);
        setPanelVisible(EditorPanel::Inspector, true);
        ImGui::SetWindowFocus(kInspectorPanelWindowName);
        break;
    case MenuCommand::DebugView:
        selectDebugView(m_settings, menuDebugView(argument));
        break;
    case MenuCommand::Appearance:
        setAppearance(static_cast<Appearance>(argument));
        break;
    case MenuCommand::Density:
        m_workspace.density = static_cast<Density>(argument);
        ImGui::MarkIniSettingsDirty();
        break;
    case MenuCommand::ZoomOut:
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, false));
        break;
    case MenuCommand::ZoomIn:
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, true));
        break;
    case MenuCommand::ResetUiScale:
        setUiScale(kDefaultUiScalePercent);
        break;
    case MenuCommand::UiScale:
        setUiScale(argument);
        break;
    case MenuCommand::Panel: {
        const auto panel = static_cast<EditorPanel>(argument);
        setPanelVisible(panel, !m_workspace.visibility.isVisible(panel));
        break;
    }
    case MenuCommand::StyleGallery:
        m_styleGallery.open = !m_styleGallery.open;
        break;
    case MenuCommand::ResetLayout:
        m_actions.requestResetLayout();
        break;
    case MenuCommand::Capture:
        // Keyboard requests also restore the existing unavailable recovery notice after dismissal.
        if (!m_actions.captureAvailable())
            m_lastCaptureNotice = {};
        m_actions.requestCapture();
        break;
    }
}

//======================================================================================================================
void EditorShell::buildMainMenu(const render::Renderer& renderer, const rojoRHI::Device& device) {
    if (!ImGui::BeginMainMenuBar())
        return;
    const auto context = menuContext(renderer, device);
#ifndef __APPLE__
    drawMenuItems(*this, buildMenuModel(context), context);
#endif
    ImGui::BeginDisabled(!context.documentIdle);
    buildPlaybackTransport();
    ImGui::EndDisabled();
    ImGui::EndMainMenuBar();
}

//======================================================================================================================
void EditorShell::updateNativeMenu(const render::Renderer& renderer,
                                   const rojoRHI::Device& device) {
#ifdef __APPLE__
    m_nativeMenu->update(buildMenuModel(menuContext(renderer, device)), shortcutContext());
#else
    (void)renderer;
    (void)device;
#endif
}

//======================================================================================================================
void EditorShell::consumeNativeMenuCommands(const render::Renderer& renderer, bool afterPanels) {
#ifdef __APPLE__
    const auto context = shortcutContext();
    for (const auto& request : m_nativeMenu->takeCommands(afterPanels ? &context : nullptr)) {
        if (!request.unavailableReason.empty()) {
            m_notices.post({ActionStatus::Unavailable, request.unavailableReason, {}},
                           ImGui::GetTime());
            continue;
        }
        runMenuCommand(request.command, request.argument);
        consumeFrameSelection(renderer);
    }
#else
    (void)renderer;
    (void)afterPanels;
#endif
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
