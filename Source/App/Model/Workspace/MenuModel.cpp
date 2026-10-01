//----------------------------------------------------------------------------------------------------------------------
/// @file MenuModel.cpp
/// @brief Builds the editor menu tree from an owned state snapshot.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Workspace/MenuModel.h"

#include "App/Model/Scene/DocumentWorkflow.h"

namespace lmx::app {

//======================================================================================================================
uint32_t menuDebugArgument(DebugView view) {
    return 1 + static_cast<uint32_t>(view.topic) * 256 + view.value;
}

//======================================================================================================================
std::optional<DebugView> menuDebugView(uint32_t argument) {
    if (argument == 0)
        return std::nullopt;
    return DebugView{static_cast<DebugViewTopic>((argument - 1) / 256),
                     static_cast<uint8_t>((argument - 1) % 256)};
}

namespace {

//======================================================================================================================
MenuItem item(std::string label, MenuCommand command, uint32_t argument = 0, bool checked = false,
              std::string reason = {}, std::optional<Shortcut> shortcut = {}) {
    return {.label = std::move(label),
            .command = command,
            .argument = argument,
            .shortcut = std::move(shortcut),
            .checked = checked,
            .enabled = reason.empty(),
            .disabledReason = std::move(reason)};
}

//======================================================================================================================
MenuItem text(std::string label) {
    return {.label = std::move(label)};
}

//======================================================================================================================
MenuItem separator() {
    return {.separator = true};
}

//======================================================================================================================
void disable(MenuItem& node, const std::string& reason) {
    if (node.separator)
        return;
    node.enabled = false;
    node.disabledReason = reason;
    for (auto& child : node.children)
        disable(child, reason);
}

//======================================================================================================================
MenuItem submenu(std::string label, std::vector<MenuItem> children,
                 const std::string& reason = {}) {
    MenuItem result{.label = std::move(label), .children = std::move(children)};
    if (!reason.empty())
        disable(result, reason);
    return result;
}

//======================================================================================================================
MenuItem fileMenu(const MenuContext& context) {
    const std::string busy =
        context.documentIdle ? "" : "Finish the current document operation first.";
    const std::string openReason = !busy.empty()       ? busy
                                   : context.measuring ? "Stop measurement before opening a scene."
                                                       : "";
    std::vector<MenuItem> catalog;
    for (uint32_t index = 0; index < context.scenes.size(); ++index) {
        const auto& scene = context.scenes[index];
        const std::string reason = scene.available        ? ""
                                   : scene.reason.empty() ? "Required scene assets are unavailable."
                                                          : scene.reason;
        catalog.push_back(
            item(scene.label, MenuCommand::OpenCatalog, index, scene.selected, reason));
        if (!scene.available)
            catalog.push_back(text(reason));
    }
    if (context.canRetryScene) {
        catalog.push_back(separator());
        catalog.push_back(text(context.sceneFailure));
        catalog.push_back(item("Retry scene load", MenuCommand::RetryScene));
    }
    const auto document = [&](const char* label, MenuCommand command, DocumentAction action,
                              std::optional<Shortcut> shortcut = {}) {
        const auto reason =
            DocumentWorkflow::unavailableReason(action, context.stopped, context.measuring);
        return item(label, command, 0, false, busy.empty() ? reason.value_or("") : busy, shortcut);
    };
    return submenu("File",
                   {item("Open…", MenuCommand::Open, 0, false, openReason, Shortcut{"O", true}),
                    submenu("Open Scene", std::move(catalog), openReason), separator(),
                    document("Save", MenuCommand::Save, DocumentAction::Save, Shortcut{"S", true}),
                    document("Save As…", MenuCommand::SaveAs, DocumentAction::SaveAs,
                             Shortcut{"S", true, true}),
                    document("Revert", MenuCommand::Revert, DocumentAction::Revert), separator(),
                    item("Quit", MenuCommand::Quit, 0, false, {}, Shortcut{"Q", true})});
}

//======================================================================================================================
MenuItem debugMenu(const MenuContext& context) {
    std::vector<MenuItem> children{item("Final", MenuCommand::DebugView, 0, !context.debug)};
    for (auto topic :
         {DebugViewTopic::Temporal, DebugViewTopic::Lighting, DebugViewTopic::Occlusion}) {
        std::vector<MenuItem> entries;
        for (const auto& entry : context.debugEntries) {
            if (entry.view.topic != topic)
                continue;
            const bool checked = context.debug && context.debug->topic == topic &&
                                 context.debug->value == entry.view.value;
            entries.push_back(item(entry.label, MenuCommand::DebugView,
                                   menuDebugArgument(entry.view), checked,
                                   entry.available ? "" : entry.reason));
        }
        children.push_back(submenu(topic == DebugViewTopic::Temporal   ? "Temporal"
                                   : topic == DebugViewTopic::Lighting ? "Lighting"
                                                                       : "Occlusion",
                                   std::move(entries)));
    }
    return submenu("Debug View", std::move(children));
}

//======================================================================================================================
MenuItem viewMenu(const MenuContext& context) {
    std::vector<MenuItem> children{
        item("Set Scene Camera from View", MenuCommand::SetSceneCamera, 0, false,
             context.stopped ? "" : "Stop playback before setting the saved scene camera."),
        item("Reset Camera", MenuCommand::ResetCamera, 0, false, {}, Shortcut{"Home"}),
        item("Frame Selected", MenuCommand::FrameSelected, 0, false,
             context.canFrame ? "" : "Select an object with reliable geometry bounds in Hierarchy.",
             Shortcut{"F"}),
        item("Selection Outline", MenuCommand::SelectionOutline, 0, context.showOutline,
             !context.outlineReady ? "Outline allocation failed. Resize the viewport to retry."
             : !context.objectSelected
                 ? "Select an object in Hierarchy to show its visible-geometry outline."
                 : ""),
        item("Editor Camera", MenuCommand::EditorCamera),
        debugMenu(context)};
    if (context.measuring)
        for (auto& child : children)
            disable(child, "Stop measurement before changing the view.");
    children.push_back(submenu(
        "Appearance",
        {item("Auto (system)", MenuCommand::Appearance, static_cast<uint32_t>(Appearance::Auto),
              context.appearance == Appearance::Auto),
         item("Light", MenuCommand::Appearance, static_cast<uint32_t>(Appearance::Light),
              context.appearance == Appearance::Light),
         item("Dark", MenuCommand::Appearance, static_cast<uint32_t>(Appearance::Dark),
              context.appearance == Appearance::Dark)}));
    children.push_back(submenu(
        "Density",
        {item("Comfortable", MenuCommand::Density, static_cast<uint32_t>(Density::Comfortable),
              context.density == Density::Comfortable),
         item("Compact", MenuCommand::Density, static_cast<uint32_t>(Density::Compact),
              context.density == Density::Compact)}));
    std::vector<MenuItem> scale{
        item("Zoom Out", MenuCommand::ZoomOut, 0, false,
             context.uiScalePercent > kUiScalePresets.front()
                 ? ""
                 : "UI scale is already at the minimum (75%).",
             Shortcut{"-", true}),
        item("Zoom In", MenuCommand::ZoomIn, 0, false,
             context.uiScalePercent < kUiScalePresets.back()
                 ? ""
                 : "UI scale is already at the maximum (150%).",
             Shortcut{"+", true}),
        item("Reset UI Scale", MenuCommand::ResetUiScale, 0, false, {}, Shortcut{"0", true}),
        separator()};
    for (const uint32_t percent : kUiScalePresets)
        scale.push_back(item(std::to_string(percent) + "%", MenuCommand::UiScale, percent,
                             context.uiScalePercent == percent));
    children.push_back(submenu("UI Scale", std::move(scale)));
    return submenu("View", std::move(children));
}

//======================================================================================================================
MenuItem windowMenu(const MenuContext& context) {
    const auto panel = [&](const char* label, EditorPanel value) {
        return item(label, MenuCommand::Panel, static_cast<uint32_t>(value),
                    context.visibility.isVisible(value));
    };
    return submenu(
        "Window",
        {panel("Hierarchy", EditorPanel::Scene), panel("Viewport", EditorPanel::Viewport),
         panel("Inspector", EditorPanel::Inspector), panel("Rendering", EditorPanel::Rendering),
         panel("Performance summary", EditorPanel::PerformanceSummary),
         panel("Performance", EditorPanel::Performance),
         panel("Render Graph", EditorPanel::RenderGraph), panel("Console", EditorPanel::Console),
         item("Style Gallery", MenuCommand::StyleGallery, 0, context.styleGallery), separator(),
         item("Reset Default Layout", MenuCommand::ResetLayout)});
}

//======================================================================================================================
MenuItem helpMenu(const MenuContext& context) {
    std::vector<MenuItem> controls{
        text("Hold RMB over the image to look."),
        text("While held: WASD move, Q down, E up."),
        text("Release RMB to return to editing."),
        text("Home resets the camera; F frames the selected object."),
        text("C captures the next GPU frame when capture is enabled."),
        text("Text editing, popups and RMB look suppress these shortcuts.")};
    if (!context.labControls.empty()) {
        controls.push_back(separator());
        controls.push_back(text(context.labControls));
    }
    return submenu("Help", {submenu("Controls", std::move(controls))});
}

//======================================================================================================================
const MenuItem* findEnabled(const std::vector<MenuItem>& items, MenuCommand command,
                            uint32_t argument, bool& ancestorsEnabled) {
    for (const auto& item : items) {
        if (item.command == command && item.argument == argument)
            return &item;
        bool enabled = ancestorsEnabled && item.enabled;
        if (const auto* found = findEnabled(item.children, command, argument, enabled)) {
            ancestorsEnabled = enabled;
            return found;
        }
    }
    return nullptr;
}

} // namespace

//======================================================================================================================
EditorShortcut shortcutPolicy(MenuCommand command) {
    switch (command) {
    case MenuCommand::FrameSelected:
        return EditorShortcut::FrameSelected;
    case MenuCommand::ResetCamera:
        return EditorShortcut::ResetCamera;
    case MenuCommand::Capture:
        return EditorShortcut::Capture;
    case MenuCommand::Quit:
        return EditorShortcut::Quit;
    default:
        return EditorShortcut::Document;
    }
}

//======================================================================================================================
KeyboardDecision keyboardDecision(const std::vector<MenuItem>& items, MenuCommand command,
                                  uint32_t argument, const ShortcutContext& focus) {
    if (!shortcutAllowed(shortcutPolicy(command), focus)) {
        if (focus.textInput || focus.cameraLook || focus.popupOpen)
            return {KeyboardOutcome::Focus, {}};
        return {focus.otherSurfaceFocused ? KeyboardOutcome::OtherSurface : KeyboardOutcome::Unmet,
                {}};
    }
    bool ancestorsEnabled = true;
    const auto* item = findEnabled(items, command, argument, ancestorsEnabled);
    if (!item)
        return {};
    if (ancestorsEnabled && (item->enabled || command == MenuCommand::Capture))
        return {KeyboardOutcome::Run, {}};
    const bool document = command == MenuCommand::Open || command == MenuCommand::Save ||
                          command == MenuCommand::SaveAs;
    if (document && !item->disabledReason.empty())
        return {KeyboardOutcome::Report, item->disabledReason};
    return {};
}

//======================================================================================================================
std::vector<MenuItem> buildMenuModel(const MenuContext& context) {
    const std::string captureReason =
        !context.captureReason.empty() ? context.captureReason
        : context.captureAvailable
            ? "Waiting for the next drawable frame."
            : "Relaunch with MTL_CAPTURE_ENABLED=1 xmake run App to enable GPU capture.";
    std::vector<MenuItem> result{
        fileMenu(context), viewMenu(context), windowMenu(context),
        submenu("Debug",
                {item("Capture Next GPU Frame", MenuCommand::Capture, 0, false,
                      context.captureAvailable && !context.capturePending ? "" : captureReason,
                      Shortcut{"C"})}),
        helpMenu(context)};
    if (!context.documentIdle)
        for (size_t index = 1; index < result.size(); ++index)
            disable(result[index], "Finish the current document operation first.");
    return result;
}

} // namespace lmx::app
