//----------------------------------------------------------------------------------------------------------------------
/// @file SessionLog.h
/// @brief Declares ordered session action records and their Console attribution.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Console/ConsoleLog.h"
#include "App/Model/Console/ConsoleModel.h"
#include "App/Model/Session/SessionTypes.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

/// An owned evidence path and the SHA-256 digest of its file bytes.
struct SessionEvidence {
    std::string path;   ///< Path recorded for export.
    std::string sha256; ///< Lowercase hexadecimal digest.
};

/// One session action, ordered by a sequence assigned when recorded.
struct SessionAction {
    uint64_t sequence = 0;                    ///< Monotonic identity assigned by SessionLog.
    int64_t timestampMilliseconds = 0;        ///< UTC milliseconds since the Unix epoch.
    Actor actor = Actor::System;              ///< Initiator of the action.
    std::string client;                       ///< Session client name, if applicable.
    std::string command;                      ///< Action or command name.
    std::string arguments;                    ///< Arguments as recorded by the caller.
    SessionTier tier = SessionTier::ReadOnly; ///< Permission tier at execution.
    std::optional<uint64_t> plan;             ///< Approved plan identity, if part of one.
    std::string outcome;                      ///< Result text.
    std::vector<SessionEvidence> evidence;    ///< Evidence attached to this action.
};

/// Main-thread action history sharing a Console store with the editor. Returned spans remain
/// valid until the next record; callers must serialize access and retain the SessionLog owner.
class SessionLog {
public:
    /// Retains a non-null shared Console store; the caller controls its lifetime.
    explicit SessionLog(std::shared_ptr<ConsoleLog> console);
    /// Assigns a new sequence, retains the action and appends its attributed summary to Console.
    uint64_t record(SessionAction action);
    /// Appends evidence to a known action; an unknown sequence violates the caller contract.
    void attach(uint64_t sequence, SessionEvidence evidence);
    /// Borrows actions in recording order until the next record or destruction.
    std::span<const SessionAction> actions() const;

private:
    std::shared_ptr<ConsoleLog> m_console;
    std::vector<SessionAction> m_actions;
    uint64_t m_nextSequence = 1;
};

/// Hashes the supplied regular file's actual bytes, retaining that explicit path. Callers supply a
/// sequence manifest or GPU schema companion; directories, missing, unreadable and symlink files
/// fail.
std::expected<SessionEvidence, std::string>
hashSessionEvidence(const std::filesystem::path& output);

/// Hashes a regular output file, or every regular file in a GPU trace bundle plus its schema
/// companion, in sorted path order. Unsafe entries, missing companions and read errors fail the
/// entire collection. Sequence callers pass their manifest file explicitly.
std::expected<std::vector<SessionEvidence>, std::string>
hashSessionOutputEvidence(const std::filesystem::path& output);

/// Certifies all registered outputs before a result is recorded. Required missing outputs fail;
/// failed or cancelled jobs may omit unwritten outputs. Any existing unsafe or unreadable output
/// fails either mode, without returning a partially certified collection.
std::expected<std::vector<SessionEvidence>, std::string>
hashSessionOutputs(std::span<const std::string> outputs, bool required);

/// Encodes schema 1, protocol 1, document identity, client and ordered actions with evidence.
/// Console preserves one string per matching entry, including embedded newlines, using the supplied
/// displayed snapshot and current severity/search filter with operator and session-client actors
/// selected.
std::string sessionRecordJson(const SessionLog& log, const ConsoleSnapshot& console,
                              std::string_view documentPath, std::string_view documentHash,
                              std::string_view client, const ConsoleFilter& filter = {});

} // namespace lmx::app
