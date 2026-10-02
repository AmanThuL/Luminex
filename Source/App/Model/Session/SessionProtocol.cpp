//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProtocol.cpp
/// @brief Validates UTF-8 request lines and encodes compact session responses.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionProtocol.h"

#include "App/Model/Session/SessionCommands.h"
#include "Core/Diagnostics/Assert.h"
#include "Core/IO/JsonWriter.h"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>

namespace lmx::app {

//======================================================================================================================
bool validSessionUtf8(std::string_view text) {
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

namespace {

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

//======================================================================================================================
// A member name is echoed only while short, so a refusal always fits one response line.
std::string shownName(std::string_view name) {
    return name.size() <= 64 ? std::string(name) : std::string("(long name)");
}

//======================================================================================================================
std::expected<void, std::string> uniqueMemberNames(const asset::JsonNode& value, size_t depth) {
    if (!value.isObject() && !value.isArray())
        return {};
    if (depth > kMaxArgumentDepth)
        return std::unexpected("Arguments nest deeper than 32 levels");
    if (value.isArray()) {
        for (const auto& element : value.elements())
            if (const auto valid = uniqueMemberNames(element, depth + 1); !valid)
                return valid;
        return {};
    }
    // Member access walks from the first member, so the count is bounded before the loop.
    if (value.size() > kMaxArgumentMembers)
        return std::unexpected("An argument object has more than 64 members");
    std::unordered_set<std::string> names;
    for (size_t index = 0; index < value.size(); ++index) {
        auto name = value.memberName(index);
        if (names.contains(name))
            return std::unexpected("Duplicate argument " + shownName(name));
        names.insert(std::move(name));
        if (const auto valid = uniqueMemberNames(value.memberValue(index), depth + 1); !valid)
            return valid;
    }
    return {};
}

//======================================================================================================================
std::expected<void, std::string> knownMemberNames(const asset::JsonNode& object,
                                                  std::span<const std::string_view> allowed,
                                                  std::string_view owner) {
    for (size_t index = 0; index < object.size(); ++index) {
        const auto name = object.memberName(index);
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            return std::unexpected(
                std::format("Unknown argument {} for {}", shownName(name), owner));
    }
    return {};
}

//======================================================================================================================
// Returns the complete member-name set of a command, or empty for settings.set, whose single
// member is named by the setting itself.
std::optional<std::span<const std::string_view>> argumentNames(SessionCommand command) {
    static constexpr std::array<std::string_view, 2> hello{"name", "protocol"};
    static constexpr std::array<std::string_view, 1> cursor{"afterSequence"};
    static constexpr std::array<std::string_view, 3> edits{"summary", "evidence", "edits"};
    static constexpr std::array<std::string_view, 1> withdraw{"proposal"};
    static constexpr std::array<std::string_view, 2> debugView{"topic", "value"};
    static constexpr std::array<std::string_view, 1> scene{"scene"};
    static constexpr std::array<std::string_view, 3> measure{"name", "warmup", "frames"};
    static constexpr std::array<std::string_view, 1> dump{"name"};
    static constexpr std::array<std::string_view, 2> screenshot{"name", "frames"};
    static constexpr std::array<std::string_view, 3> sequence{"name", "frames", "warmup"};
    static constexpr std::array<std::string_view, 2> plan{"summary", "steps"};
    switch (command) {
    case SessionCommand::Hello:
        return hello;
    case SessionCommand::QueryConsole:
    case SessionCommand::QueryLog:
        return cursor;
    case SessionCommand::ProposeEdits:
        return edits;
    case SessionCommand::ProposeWithdraw:
        return withdraw;
    case SessionCommand::SettingsSet:
        return std::nullopt;
    case SessionCommand::DebugViewSet:
        return debugView;
    case SessionCommand::SceneOpen:
        return scene;
    case SessionCommand::MeasureRun:
        return measure;
    case SessionCommand::GraphDump:
        return dump;
    case SessionCommand::CaptureScreenshot:
        return screenshot;
    case SessionCommand::CaptureSequence:
        return sequence;
    case SessionCommand::PlanSubmit:
        return plan;
    default:
        return std::span<const std::string_view>{};
    }
}

//======================================================================================================================
std::string_view commandName(SessionCommand command) {
    for (const auto& spec : sessionCommands())
        if (spec.command == command)
            return spec.name;
    return "command";
}

//======================================================================================================================
std::expected<void, std::string> knownCommandMembers(SessionCommand command,
                                                     const asset::JsonNode& args) {
    const auto names = argumentNames(command);
    return names ? knownMemberNames(args, *names, commandName(command))
                 : std::expected<void, std::string>{};
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
    if (!validSessionUtf8(line))
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
std::expected<void, std::string> validateSessionArguments(SessionCommand command,
                                                          const asset::JsonNode& args) {
    if (!args.isObject())
        return std::unexpected("Arguments must be an object");
    if (const auto valid = uniqueMemberNames(args, 1); !valid)
        return valid;
    if (const auto valid = knownCommandMembers(command, args); !valid)
        return valid;
    if (command != SessionCommand::PlanSubmit)
        return {};
    const auto steps = args.find("steps");
    if (!steps || !steps->isArray())
        return {};
    static constexpr std::array<std::string_view, 2> stepNames{"command", "args"};
    for (const auto& step : steps->elements()) {
        if (!step.isObject())
            continue;
        if (const auto valid = knownMemberNames(step, stepNames, "a plan step"); !valid)
            return valid;
        const auto name = step.find("command");
        const auto arguments = step.find("args");
        const auto text =
            name ? name->asString() : std::expected<std::string, std::string>{std::unexpected("")};
        const auto* spec = text ? findCommand(*text) : nullptr;
        if (spec && spec->command != SessionCommand::PlanSubmit && arguments &&
            arguments->isObject())
            if (const auto valid = knownCommandMembers(spec->command, *arguments); !valid)
                return valid;
    }
    return {};
}

//======================================================================================================================
std::string encodeResult(uint64_t id, std::string_view resultJson) {
    if (!validSessionUtf8(resultJson))
        return encodeError(id, SessionError::Failed, "Session result contains invalid UTF-8.");
    if (!asset::JsonTokens::parse(std::string(resultJson)))
        return encodeError(id, SessionError::Failed, "Session result is not valid JSON.");
    return std::format("{{\"id\":{},\"ok\":true,\"result\":{}}}\n", id, compactJson(resultJson));
}

//======================================================================================================================
std::string encodeError(uint64_t id, SessionError code, std::string_view message) {
    if (!validSessionUtf8(message))
        message = "Session error message contains invalid UTF-8.";
    return std::format("{{\"id\":{},\"ok\":false,\"error\":{{\"code\":\"{}\",\"message\":{}}}}}\n",
                       id, errorCode(code), quoted(message));
}

} // namespace lmx::app
