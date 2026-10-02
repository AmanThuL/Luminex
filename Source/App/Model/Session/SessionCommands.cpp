//----------------------------------------------------------------------------------------------------------------------
/// @file SessionCommands.cpp
/// @brief Defines the session command inventory and permission checks.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionCommands.h"

#include <algorithm>
#include <array>
#include <format>

namespace lmx::app {
namespace {

constexpr std::array<CommandSpec, 23> kCommands = {{
    {SessionCommand::Hello, "hello", SessionTier::ReadOnly},
    {SessionCommand::QueryStatus, "query.status", SessionTier::ReadOnly},
    {SessionCommand::QueryHierarchy, "query.hierarchy", SessionTier::ReadOnly},
    {SessionCommand::QuerySelection, "query.selection", SessionTier::ReadOnly},
    {SessionCommand::QueryCamera, "query.camera", SessionTier::ReadOnly},
    {SessionCommand::QuerySettings, "query.settings", SessionTier::ReadOnly},
    {SessionCommand::QueryReadings, "query.readings", SessionTier::ReadOnly},
    {SessionCommand::QueryPerformance, "query.performance", SessionTier::ReadOnly},
    {SessionCommand::QueryGraph, "query.graph", SessionTier::ReadOnly},
    {SessionCommand::QueryConsole, "query.console", SessionTier::ReadOnly},
    {SessionCommand::QueryProposals, "query.proposals", SessionTier::ReadOnly},
    {SessionCommand::QueryLog, "query.log", SessionTier::ReadOnly},
    {SessionCommand::ProposeEdits, "propose.edits", SessionTier::Propose},
    {SessionCommand::ProposeWithdraw, "propose.withdraw", SessionTier::Propose},
    {SessionCommand::SettingsSet, "settings.set", SessionTier::Apply},
    {SessionCommand::DebugViewSet, "debugview.set", SessionTier::Apply},
    {SessionCommand::SceneOpen, "scene.open", SessionTier::Apply},
    {SessionCommand::MeasureRun, "measure.run", SessionTier::Apply},
    {SessionCommand::CaptureGpu, "capture.gpu", SessionTier::Apply},
    {SessionCommand::CaptureScreenshot, "capture.screenshot", SessionTier::Apply},
    {SessionCommand::CaptureSequence, "capture.sequence", SessionTier::Apply},
    {SessionCommand::GraphDump, "graph.dump", SessionTier::Apply},
    {SessionCommand::PlanSubmit, "plan.submit", SessionTier::Apply},
}};

} // namespace

//======================================================================================================================
std::span<const CommandSpec> sessionCommands() {
    return kCommands;
}

//======================================================================================================================
const CommandSpec* findCommand(std::string_view name) {
    const auto found = std::find_if(kCommands.begin(), kCommands.end(),
                                    [name](const CommandSpec& spec) { return spec.name == name; });
    return found == kCommands.end() ? nullptr : &*found;
}

//======================================================================================================================
std::optional<std::string> tierRefusal(const CommandSpec& command, SessionTier ceiling) {
    if (command.tier <= ceiling)
        return std::nullopt;
    return std::format("Command {} exceeds the connection tier", command.name);
}

//======================================================================================================================
std::expected<std::string, std::string> evidenceName(std::string_view name) {
    if (name.empty() || name.size() > 128)
        return std::unexpected("Evidence name must contain 1 to 128 bytes");
    if (name.front() == '.' || name.find("..") != std::string_view::npos)
        return std::unexpected("Evidence name cannot start with a dot or contain '..'");
    const bool valid = std::all_of(name.begin(), name.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-';
    });
    if (!valid)
        return std::unexpected(
            "Evidence name must use ASCII letters, digits, dot, underscore or hyphen");
    return std::string(name);
}

//======================================================================================================================
std::vector<std::string> childRunEnvironment(const char* const* environment) {
    std::vector<std::string> kept;
    for (; environment && *environment; ++environment) {
        const std::string_view entry(*environment);
        if (!entry.starts_with("LMX_"))
            kept.emplace_back(entry);
    }
    return kept;
}

} // namespace lmx::app
