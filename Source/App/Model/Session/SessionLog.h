//----------------------------------------------------------------------------------------------------------------------
/// @file SessionLog.h
/// @brief Declares ordered session action records and their Console attribution.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Console/ConsoleLog.h"
#include "App/Model/Console/ConsoleModel.h"
#include "App/Model/Session/SessionTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// Retained bytes of an action's command name before the truncation note.
inline constexpr size_t kMaxActionCommandBytes = 128;
/// Retained bytes of an action's arguments before the truncation note.
inline constexpr size_t kMaxActionArgumentBytes = 4096;
/// Retained actions; recording beyond this drops the oldest.
inline constexpr size_t kMaxSessionActions = 10000;

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
    std::string command;                      ///< Action or command name, bounded when recorded.
    std::string arguments;                    ///< Caller's arguments, bounded when recorded.
    SessionTier tier = SessionTier::ReadOnly; ///< Permission tier at execution.
    std::optional<uint64_t> plan;             ///< Approved plan identity, if part of one.
    std::string outcome;                      ///< Result text.
    std::vector<SessionEvidence> evidence;    ///< Evidence attached to this action.
};

/// Returns the capitalized actor name used by query replies and the exported record.
std::string_view sessionActorName(Actor actor);

/// Keeps at most limit bytes of text, cut back to a UTF-8 sequence start, and appends
/// "…[truncated N bytes]" naming the removed byte count. Shorter text returns unchanged.
std::string truncateSessionText(std::string text, size_t limit);

/// Main-thread bounded action history sharing a Console store with the editor. The borrowed
/// actions remain valid until the next record; callers must serialize access and retain the
/// SessionLog owner.
class SessionLog {
public:
    /// Retains a non-null shared Console store; the caller controls its lifetime.
    explicit SessionLog(std::shared_ptr<ConsoleLog> console);
    /// Assigns a new sequence, bounds the command and arguments, retains the action and appends
    /// its attributed summary to Console. Beyond kMaxSessionActions the oldest action is dropped.
    uint64_t record(SessionAction action);
    /// Appends evidence to a retained action and ignores one already dropped; a sequence never
    /// assigned violates the caller contract.
    void attach(uint64_t sequence, SessionEvidence evidence);
    /// Borrows retained actions in recording order until the next record or destruction.
    const std::deque<SessionAction>& actions() const;
    /// Counts actions dropped from the front of the history since construction.
    uint64_t dropped() const;
    /// Returns the sequence the next recorded action receives.
    uint64_t nextSequence() const;

private:
    std::shared_ptr<ConsoleLog> m_console;
    std::deque<SessionAction> m_actions;
    uint64_t m_nextSequence = 1;
    uint64_t m_dropped = 0;
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

/// Encodes schema 1, protocol 1, document identity, client, the dropped-action count and the
/// retained ordered actions with evidence.
/// Console preserves one string per matching entry, including embedded newlines, using the supplied
/// displayed snapshot and current severity/search filter with operator and session-client actors
/// selected.
std::string sessionRecordJson(const SessionLog& log, const ConsoleSnapshot& console,
                              std::string_view documentPath, std::string_view documentHash,
                              std::string_view client, const ConsoleFilter& filter = {});

} // namespace lmx::app
