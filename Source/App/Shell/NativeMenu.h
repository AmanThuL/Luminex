//----------------------------------------------------------------------------------------------------------------------
/// @file NativeMenu.h
/// @brief Declares the native menu owner and its queued shell commands.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/EditorShortcuts.h"
#include "App/Model/Workspace/MenuModel.h"

#include <memory>
#include <string>
#include <vector>

namespace lmx::app {

/// One drained native request. A nonempty unavailableReason replaces execution: the shell posts it
/// as an unavailable notice for a chord whose row is disabled.
struct NativeMenuCommand {
    MenuCommand command = MenuCommand::Open; ///< Command identity from the model.
    uint32_t argument = 0;                   ///< Option identity within the command.
    std::string unavailableReason;           ///< The row's disabled reason, or empty to run.
};

/// Owns the macOS application menu on the UI thread, after SDL and ImGui initialization.
/// Destroy before the ImGui context; native callbacks only queue shell commands or SDL edit keys.
class NativeMenuBar {
public:
    /// Replaces SDL's default menu, retaining it for restoration at destruction.
    static std::unique_ptr<NativeMenuBar> install();
    /// Disconnects callbacks and restores the previous menu if this owner is still installed.
    ~NativeMenuBar();
    /// The process menu has one owner.
    NativeMenuBar(const NativeMenuBar&) = delete;
    /// The process menu cannot be reassigned between owners.
    NativeMenuBar& operator=(const NativeMenuBar&) = delete;
    /// Publishes an owned model and keyboard observation before polling and after drawing panels.
    /// Appearance comes from the model's checked Appearance option; Auto clears menu appearance.
    void update(std::vector<MenuItem> items, const ShortcutContext& context);
    /// After SDL polling, binds staged shortcuts to ImGui input events and drains mouse actions.
    std::vector<NativeMenuCommand> takeCommands();
    /// After all panels, resolves consumed keys against current ownership without storing context.
    /// Unprocessed keys wait through ImGui input trickling; command order is retained. Quit skips
    /// this resolution: its chord queues a ready command from any focus.
    std::vector<NativeMenuCommand> takeCommands(const ShortcutContext* completedFrame);

private:
    NativeMenuBar();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace lmx::app
