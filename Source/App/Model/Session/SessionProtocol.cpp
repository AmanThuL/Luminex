//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProtocol.cpp
/// @brief Validates UTF-8 request lines and encodes compact session responses.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionProtocol.h"

#include "App/Model/Session/SessionCommands.h"
#include "Core/Diagnostics/Assert.h"
#include "Core/IO/JsonWriter.h"

#include <array>
#include <format>
#include <unordered_set>
#include <utility>

namespace lmx::app {
namespace {

//======================================================================================================================
bool validUtf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        size_t count = 0;
        uint32_t value = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 2;
            value = first & 0x1f;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 3;
            value = first & 0x0f;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 4;
            value = first & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (count > text.size() - i)
            return false;
        for (size_t j = 1; j < count; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
            return false;
        i += count;
    }
    return true;
}

//======================================================================================================================
std::string quoted(std::string_view value) {
    JsonWriter writer;
    writer.string(value);
    auto result = writer.take();
    LMX_ASSERT(!result.empty() && result.back() == '\n', "JSON writer omitted final newline");
    result.pop_back();
    return result;
}

//======================================================================================================================
std::string compactJson(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool quotedString = false;
    bool escaped = false;
    for (const char byte : value) {
        if (quotedString) {
            result.push_back(byte);
            if (escaped)
                escaped = false;
            else if (byte == '\\')
                escaped = true;
            else if (byte == '"')
                quotedString = false;
        } else if (byte == '"') {
            quotedString = true;
            result.push_back(byte);
        } else if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n') {
            result.push_back(byte);
        }
    }
    return result;
}

//======================================================================================================================
std::string_view errorCode(SessionError code) {
    switch (code) {
    case SessionError::Protocol:
        return "protocol";
    case SessionError::Tier:
        return "tier";
    case SessionError::Denied:
        return "denied";
    case SessionError::Unavailable:
        return "unavailable";
    case SessionError::Invalid:
        return "invalid";
    case SessionError::Busy:
        return "busy";
    case SessionError::Failed:
        return "failed";
    case SessionError::Cancelled:
        return "cancelled";
    }
    LMX_ASSERT(false, "Unknown session error code");
    return "protocol";
}

} // namespace

//======================================================================================================================
std::expected<SessionRequest, std::string> decodeRequestEnvelope(std::string line) {
    if (line.ends_with('\n')) {
        line.pop_back();
        if (line.ends_with('\r'))
            line.pop_back();
    }
    if (line.size() > kMaxLineBytes)
        return std::unexpected("Session line exceeds 1 MiB");
    if (line.find_first_of("\r\n") != std::string::npos)
        return std::unexpected("Session line contains an embedded line ending");
    if (!validUtf8(line))
        return std::unexpected("Invalid UTF-8 in session line");
    const auto parsed = asset::JsonTokens::parse(std::move(line));
    if (!parsed)
        return std::unexpected("Invalid JSON: " + parsed.error().message);
    const auto root = parsed->root();
    if (!root.isObject())
        return std::unexpected("Session request must be an object");
    if (root.size() > 3)
        return std::unexpected("Session request has too many fields");

    std::unordered_set<std::string> keys;
    for (size_t i = 0; i < root.size(); ++i) {
        auto key = root.memberName(i);
        if (key != "id" && key != "command" && key != "args")
            return std::unexpected("Unknown request field " + key);
        if (!keys.insert(key).second)
            return std::unexpected("Duplicate request field " + key);
    }

    const auto idNode = root.find("id");
    if (!idNode)
        return std::unexpected("Missing id");
    const auto id = idNode->asUInt();
    if (!id)
        return std::unexpected("Invalid id: expected non-negative uint64 integer");
    const auto commandNode = root.find("command");
    if (!commandNode)
        return std::unexpected("Missing command");
    const auto command = commandNode->asString();
    if (!command)
        return std::unexpected("Invalid command: expected string");

    if (const auto args = root.find("args")) {
        if (!args->isObject())
            return std::unexpected("Invalid args: expected object");
        return SessionRequest{*id, *command, *args};
    }
    const auto empty = asset::JsonTokens::parse("{}");
    LMX_ASSERT(empty, "Static empty JSON object failed to parse");
    return SessionRequest{*id, *command, empty->root()};
}

//======================================================================================================================
std::expected<SessionRequest, std::string> decodeRequest(std::string line) {
    auto request = decodeRequestEnvelope(std::move(line));
    if (request && !findCommand(request->command))
        return std::unexpected("Unknown command " + request->command);
    return request;
}

//======================================================================================================================
std::string encodeResult(uint64_t id, std::string_view resultJson) {
    LMX_ASSERT(validUtf8(resultJson), "Session result must be UTF-8");
    LMX_ASSERT(asset::JsonTokens::parse(std::string(resultJson)), "Session result must be JSON");
    return std::format("{{\"id\":{},\"ok\":true,\"result\":{}}}\n", id, compactJson(resultJson));
}

//======================================================================================================================
std::string encodeError(uint64_t id, SessionError code, std::string_view message) {
    LMX_ASSERT(validUtf8(message), "Session error message must be UTF-8");
    return std::format("{{\"id\":{},\"ok\":false,\"error\":{{\"code\":\"{}\",\"message\":{}}}}}\n",
                       id, errorCode(code), quoted(message));
}

} // namespace lmx::app
