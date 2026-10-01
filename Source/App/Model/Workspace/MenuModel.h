//----------------------------------------------------------------------------------------------------------------------
/// @file MenuModel.h
/// @brief Declares the platform-neutral editor menu tree and its state snapshot.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/EditorShortcuts.h"
#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Workspace/WorkspaceModel.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmx::app {

/// Shell actions; option commands interpret argument as documented on each enumerator.
enum class MenuCommand {
    Open,             ///< Opens a scene document chooser.
    OpenCatalog,      ///< Opens the catalog entry at argument's stable display index.
    RetryScene,       ///< Retries the failed scene, independently of catalog selection.
    Save,             ///< Saves the active document.
    SaveAs,           ///< Chooses a new document destination.
    Revert,           ///< Requests discarding document edits.
    Quit,             ///< Requests the unsaved-changes quit workflow.
    SetSceneCamera,   ///< Copies the editor view into the saved scene camera.
    ResetCamera,      ///< Restores the authored camera.
    FrameSelected,    ///< Fits current selection using the renderer at dispatch consumption.
    SelectionOutline, ///< Toggles the visible-geometry outline.
    EditorCamera,     ///< Selects and focuses the editor camera Inspector.
    DebugView,        ///< Zero means Final; otherwise use menuDebugArgument.
    Appearance,       ///< Argument is an Appearance enumerator.
    Density,          ///< Argument is a Density enumerator.
    ZoomOut,          ///< Steps to the preceding UI scale preset.
    ZoomIn,           ///< Steps to the next UI scale preset.
    ResetUiScale,     ///< Restores 100 percent UI scale.
    UiScale,          ///< Argument is a UI scale preset percentage.
    Panel,            ///< Argument is an EditorPanel enumerator to toggle.
    StyleGallery,     ///< Toggles the transient Style Gallery window.
    ResetLayout,      ///< Queues the default workspace layout.
    Capture,          ///< Requests the next GPU frame or its unavailable recovery notice.
};

/// Platform-neutral key equivalent; key is O, S, Q, Home, F, -, +, 0 or C.
struct Shortcut {
    std::string key;      ///< Unmodified logical key name.
    bool command = false; ///< Requires the platform Command modifier.
    bool shift = false;   ///< Requires Shift in addition to Command when set.
};

/// Owned tree node. A nonseparator leaf without a command is explanatory text.
struct MenuItem {
    std::string label;                  ///< Display text, without toolkit identity suffixes.
    std::optional<MenuCommand> command; ///< Present only for actionable leaves.
    uint32_t argument = 0;              ///< Option identity within the command.
    std::optional<Shortcut> shortcut;   ///< Exact key equivalent, absent for mouse-only items.
    bool checked = false;               ///< Reflects current state even when disabled.
    bool enabled = true;                ///< Includes ancestor restrictions.
    bool separator = false;             ///< A divider with no action or children.
    std::string disabledReason;         ///< Nonempty for every disabled nonseparator node.
    std::vector<MenuItem> children;     ///< Owned submenu rows; empty on leaves.
};

/// Owned catalog metadata, with no scene or GPU resource lifetime dependency.
struct MenuScene {
    std::string label;      ///< Catalog display name.
    bool available = false; ///< Required assets are available.
    bool selected = false;  ///< This is the active scene.
    std::string reason;     ///< Asset recovery guidance when unavailable.
};

/// Value snapshot of the state used by File, View, Window, Debug and Help.
struct MenuContext {
    bool documentIdle = true;      ///< No document confirmation, chooser or work is outstanding.
    bool stopped = true;           ///< Playback is Stopped, not Paused.
    bool measuring = false;        ///< Measurement owns scene controls.
    std::vector<MenuScene> scenes; ///< Stable catalog display order, used by OpenCatalog arguments.
    std::string sceneFailure;      ///< Complete load failure and recovery text, empty on success.
    std::string retrySceneName;    ///< Failed catalog display name for loading feedback.
    bool canRetryScene = false;    ///< A failed scene identity is retained by the shell.
    bool canFrame = false;         ///< Selection has reliable geometry bounds.
    bool objectSelected = false;   ///< Selection is an object.
    bool outlineReady = false;     ///< Outline and current renderer extents match.
    bool showOutline = true;       ///< Stored outline toggle, independent of availability.
    std::optional<DebugView> debug;           ///< Current diagnostic; null means Final.
    std::vector<DebugViewEntry> debugEntries; ///< Resolved diagnostics and their actual reasons.
    Appearance appearance = Appearance::Auto; ///< Effective preference, including CLI override.
    Density density = Density::Comfortable;   ///< Stored spacing preference.
    uint32_t uiScalePercent = kDefaultUiScalePercent; ///< Current text/control scale percentage.
    WorkspaceVisibility visibility;                   ///< Current panel toggles.
    bool styleGallery = false;                        ///< Transient gallery visibility.
    bool captureAvailable = false;                    ///< Capture startup capability.
    bool capturePending = false;                      ///< A request awaits a drawable.
    std::string captureReason;                        ///< Current capture recovery/status text.
    std::string labControls;                          ///< Scene-specific Help text, if any.
};

/// Encodes a diagnostic as 1 + topic * 256 + value; zero is reserved for Final.
uint32_t menuDebugArgument(DebugView view);
/// Decodes menuDebugArgument; zero returns Final. Other inputs must be model-produced arguments.
std::optional<DebugView> menuDebugView(uint32_t argument);
/// Builds an owned menu snapshot without I/O, UI calls or changes to the supplied state.
/// Every actionable command/argument pair appears once; retry has its own command.
std::vector<MenuItem> buildMenuModel(const MenuContext& context);

/// What the frame that consumes a keyboard chord does with its matched command.
enum class KeyboardOutcome {
    Run,      ///< Execute the command.
    Report,   ///< Post the decision's reason once instead of executing.
    Focus,    ///< Text entry, a popup or mouse look owns the key; expected while editing.
    Policy,   ///< Another surface owns the key, or the command lacks its own prerequisite.
    Disabled, ///< The row or an ancestor is unavailable and has no reason to post.
};

/// Outcome of one matched chord; reason is nonempty only for Report.
struct KeyboardDecision {
    KeyboardOutcome outcome = KeyboardOutcome::Disabled; ///< What the consuming frame does.
    std::string reason;                                  ///< The row's disabledReason to post.
};

/// Focus policy of a command's keyboard route; Quit is exempt from every focus gate.
EditorShortcut shortcutPolicy(MenuCommand command);
/// Decides a chord against the current model and keyboard ownership, without side effects.
/// Focus is checked first, so a chord typed into a field never reports. An unavailable Open, Save
/// or Save As then reports its row's reason; Capture runs while unavailable so its intent can
/// explain recovery; every other unavailable or missing command is Disabled.
KeyboardDecision keyboardDecision(const std::vector<MenuItem>& items, MenuCommand command,
                                  uint32_t argument, const ShortcutContext& focus);

} // namespace lmx::app
