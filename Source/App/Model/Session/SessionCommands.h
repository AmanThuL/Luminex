//----------------------------------------------------------------------------------------------------------------------
/// @file SessionCommands.h
/// @brief Declares the session command inventory, permission checks and evidence names.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionTypes.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lmx::app {

/// Stable command identities for the local session protocol.
enum class SessionCommand {
    Hello,             ///< Start a connection and declare the protocol version.
    QueryStatus,       ///< Read editor and connection status.
    QueryHierarchy,    ///< Read scene subjects.
    QuerySelection,    ///< Read the current selection.
    QueryCamera,       ///< Read the editor camera.
    QuerySettings,     ///< Read rendering settings.
    QueryReadings,     ///< Read renderer diagnostics.
    QueryPerformance,  ///< Read performance measurements.
    QueryGraph,        ///< Read the render graph.
    QueryConsole,      ///< Read visible Console rows.
    QueryProposals,    ///< Read pending proposals.
    QueryLog,          ///< Read session action history.
    ProposeEdits,      ///< Submit editor changes for review.
    ProposeWithdraw,   ///< Withdraw a proposal.
    SettingsSet,       ///< Request a rendering setting change.
    DebugViewSet,      ///< Request a debug view change.
    SceneOpen,         ///< Request a scene switch.
    MeasureRun,        ///< Request a measurement run.
    CaptureGpu,        ///< Request a GPU capture.
    CaptureScreenshot, ///< Request a still image.
    CaptureSequence,   ///< Request a frame sequence.
    GraphDump,         ///< Request a graph dump.
    PlanSubmit         ///< Request an ordered apply plan.
};

/// Name and minimum operator-granted tier for one command.
struct CommandSpec {
    SessionCommand command; ///< Stable identity.
    std::string_view name;  ///< Exact wire name.
    SessionTier tier;       ///< Minimum permission tier.
};

/// Returns the immutable inventory of all protocol commands, including hello.
std::span<const CommandSpec> sessionCommands();
/// Finds an exact wire name; returns null for unknown commands.
const CommandSpec* findCommand(std::string_view name);
/// Returns a reason naming the command when its tier exceeds the connection ceiling.
std::optional<std::string> tierRefusal(const CommandSpec& command, SessionTier ceiling);
/// Validates a single evidence path component and returns an owned name. Rejects empty names,
/// names over 128 bytes, a leading dot, any two consecutive dots, and bytes outside ASCII
/// letters, digits, dot, underscore and hyphen.
std::expected<std::string, std::string> evidenceName(std::string_view name);

} // namespace lmx::app
