//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProtocol.h
/// @brief Declares one-line session request parsing and response encoding.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Model/JsonTokens.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace lmx::app {

/// Current local socket protocol version.
inline constexpr uint32_t kSessionProtocol = 1;
/// Maximum request bytes before an optional line ending.
inline constexpr size_t kMaxLineBytes = 1 << 20;

/// Parsed request; args retains its own immutable JSON storage across copies and moves.
struct SessionRequest {
    uint64_t id;          ///< Client request identity, including the full unsigned 64-bit range.
    std::string command;  ///< Exact known command name.
    asset::JsonNode args; ///< Object arguments, or an owned empty object when absent.
};

/// Stable lowercase error codes sent to session clients.
enum class SessionError {
    Protocol,    ///< Invalid protocol framing or handshake.
    Tier,        ///< Command exceeds the operator-granted ceiling.
    Denied,      ///< Operator denied an approval request.
    Unavailable, ///< Command cannot run in the current editor state.
    Invalid,     ///< Command arguments or subject are invalid.
    Busy,        ///< Request capacity or exclusive work is occupied.
    Failed,      ///< Accepted work failed during execution.
    Cancelled    ///< Pending or active work was cancelled.
};

/// Validates complete UTF-8 scalar sequences, rejecting overlong, surrogate and truncated bytes.
/// Shared by disk-sidecar admission and both directions of the session protocol.
bool validSessionUtf8(std::string_view text);

/// Parses one complete UTF-8 JSON request line. A final LF or CRLF is accepted; embedded line
/// endings, oversized lines, invalid UTF-8, unknown commands and ambiguous root keys fail with
/// a named reason. The returned request owns all argument storage.
std::expected<SessionRequest, std::string> decodeRequest(std::string line);
/// Parses a framed request without requiring the command to be in the current inventory. The
/// dispatcher can answer a well-formed unknown command with `invalid` and its exact request id.
std::expected<SessionRequest, std::string> decodeRequestEnvelope(std::string line);
/// Encodes one successful response with an exact unsigned id and a valid JSON result value.
/// The returned wire message ends with exactly one LF. Invalid external UTF-8 or JSON returns
/// an explicit failed response with the same id instead of terminating the process.
std::string encodeResult(uint64_t id, std::string_view resultJson);
/// Encodes one failed response, escaping the message into a single UTF-8 JSON line.
/// Invalid UTF-8 message bytes are replaced by an explicit safe diagnostic, preserving the code.
std::string encodeError(uint64_t id, SessionError code, std::string_view message);

} // namespace lmx::app
