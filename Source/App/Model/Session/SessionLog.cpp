//----------------------------------------------------------------------------------------------------------------------
/// @file SessionLog.cpp
/// @brief Records attributed session actions and their evidence.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionLog.h"

#include "App/Model/Session/SessionProtocol.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/JsonWriter.h"
#include "Core/Util/Sha256.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

namespace lmx::app {

//======================================================================================================================
std::string_view sessionStateLabel(SessionState state) {
    switch (state) {
    case SessionState::Idle:
        return "idle";
    case SessionState::Working:
        return "working";
    case SessionState::Awaiting:
        return "awaiting";
    case SessionState::Proposed:
        return "proposed";
    case SessionState::Applied:
        return "applied";
    case SessionState::Error:
        return "error";
    case SessionState::Stale:
        return "stale";
    }
    LMX_ASSERT(false, "Unknown session state");
    return "error";
}

//======================================================================================================================
SessionLog::SessionLog(std::shared_ptr<ConsoleLog> console) : m_console(std::move(console)) {
    LMX_ASSERT(m_console != nullptr, "SessionLog requires a Console store");
}

//======================================================================================================================
uint64_t SessionLog::record(SessionAction action) {
    action.sequence = m_nextSequence++;
    const uint64_t sequence = action.sequence;
    m_console->append(log::Level::Info, action.timestampMilliseconds,
                      std::format("{} {} -> {}", action.command, action.arguments, action.outcome),
                      action.actor);
    m_actions.push_back(std::move(action));
    return sequence;
}

//======================================================================================================================
void SessionLog::attach(uint64_t sequence, SessionEvidence evidence) {
    const auto it =
        std::find_if(m_actions.begin(), m_actions.end(), [sequence](const SessionAction& action) {
            return action.sequence == sequence;
        });
    LMX_ASSERT(it != m_actions.end(), "Unknown session action sequence");
    it->evidence.push_back(std::move(evidence));
}

//======================================================================================================================
std::span<const SessionAction> SessionLog::actions() const {
    return m_actions;
}

//======================================================================================================================
std::expected<SessionEvidence, std::string>
hashSessionEvidence(const std::filesystem::path& output) {
    std::error_code error;
    auto status = std::filesystem::symlink_status(output, error);
    if (error || std::filesystem::is_symlink(status))
        return std::unexpected("Evidence is missing or a symlink: " + output.string());
    const auto& path = output;
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        return std::unexpected("Evidence hash requires a regular file: " + path.string());
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::unexpected("Cannot read evidence: " + path.string());
    const std::string bytes(std::istreambuf_iterator<char>{file}, {});
    if (file.bad())
        return std::unexpected("Could not finish reading evidence: " + path.string());
    return SessionEvidence{path.string(), sha256Hex(std::as_bytes(std::span(bytes)))};
}

//======================================================================================================================
std::expected<std::vector<SessionEvidence>, std::string>
hashSessionOutputEvidence(const std::filesystem::path& output) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(output, error);
    if (error || std::filesystem::is_symlink(status))
        return std::unexpected("Evidence is missing or a symlink: " + output.string());
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(status)) {
        for (std::filesystem::recursive_directory_iterator it(output, error), end;
             !error && it != end; it.increment(error)) {
            const auto entry = it->symlink_status(error);
            if (error)
                break;
            if (std::filesystem::is_regular_file(entry))
                files.push_back(it->path());
            else if (!std::filesystem::is_directory(entry))
                return std::unexpected("Unsafe GPU evidence entry: " + it->path().string());
        }
        if (error)
            return std::unexpected("Cannot read GPU evidence tree: " + error.message());
        files.emplace_back(output.string() + ".schema.json");
    } else {
        files.push_back(output);
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.native() < b.native(); });
    std::vector<SessionEvidence> evidence;
    evidence.reserve(files.size());
    for (const auto& file : files) {
        auto hashed = hashSessionEvidence(file);
        if (!hashed)
            return std::unexpected(hashed.error());
        evidence.push_back(std::move(*hashed));
    }
    return evidence;
}

//======================================================================================================================
std::expected<std::vector<SessionEvidence>, std::string>
hashSessionOutputs(std::span<const std::string> outputs, bool required) {
    std::vector<SessionEvidence> files;
    for (const auto& output : outputs) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(output, error);
        if (!required && (error == std::errc::no_such_file_or_directory ||
                          (!error && !std::filesystem::exists(status))))
            continue;
        auto evidence = hashSessionOutputEvidence(output);
        if (!evidence)
            return std::unexpected(evidence.error());
        files.insert(files.end(), std::make_move_iterator(evidence->begin()),
                     std::make_move_iterator(evidence->end()));
    }
    return files;
}

//======================================================================================================================
std::string sessionRecordJson(const SessionLog& log, const ConsoleSnapshot& console,
                              std::string_view documentPath, std::string_view documentHash,
                              std::string_view client, const ConsoleFilter& filter) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("schema");
    writer.integer(1);
    writer.key("protocol");
    writer.integer(kSessionProtocol);
    writer.key("document");
    writer.beginObject();
    writer.key("path");
    writer.string(documentPath);
    writer.key("hash");
    writer.string(documentHash);
    writer.endObject();
    writer.key("client");
    writer.string(client);
    writer.key("actions");
    writer.beginArray();
    for (const auto& action : log.actions()) {
        writer.beginObject();
        writer.key("sequence");
        writer.integer(action.sequence);
        writer.key("timestampMilliseconds");
        writer.integer(action.timestampMilliseconds);
        writer.key("actor");
        writer.string(action.actor == Actor::Operator ? "operator"
                      : action.actor == Actor::Agent  ? "Agent"
                                                      : "system");
        writer.key("client");
        writer.string(action.client);
        writer.key("command");
        writer.string(action.command);
        writer.key("arguments");
        writer.string(action.arguments);
        writer.key("tier");
        writer.integer(static_cast<uint8_t>(action.tier));
        if (action.plan) {
            writer.key("plan");
            writer.integer(*action.plan);
        }
        writer.key("outcome");
        writer.string(action.outcome);
        writer.key("evidence");
        writer.beginArray();
        for (const auto& evidence : action.evidence) {
            writer.beginObject();
            writer.key("path");
            writer.string(evidence.path);
            writer.key("sha256");
            writer.string(evidence.sha256);
            writer.endObject();
        }
        writer.endArray();
        writer.endObject();
    }
    writer.endArray();
    writer.key("console");
    writer.beginArray();
    auto selected = filter;
    selected.actors = {true, false, true};
    for (const auto& entry : console.entries) {
        if (!consoleEntryMatches(entry, selected))
            continue;
        ConsoleSnapshot single;
        single.entries.push_back(entry);
        auto text = consoleVisibleText(single, selected);
        text.pop_back();
        writer.string(text);
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

} // namespace lmx::app
