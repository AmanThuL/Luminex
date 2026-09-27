//----------------------------------------------------------------------------------------------------------------------
/// @file CgltfImplementation.cpp
/// @brief Owns cgltf implementation and validates immutable document tokens through its tokenizer.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Model/JsonTokens.h"

#include "Core/Diagnostics/Assert.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace lmx::asset {

struct JsonTokens::Storage {
    std::string text;
    std::vector<jsmntok_t> tokens;
    std::vector<size_t> next;
    std::vector<std::string> strings;
};

namespace {

//======================================================================================================================
AssetError malformed(size_t byte, std::string_view reason) {
    return {AssetErrorCode::Malformed,
            "JSON at byte " + std::to_string(byte) + ": " + std::string(reason)};
}

//======================================================================================================================
size_t tokenStart(const jsmntok_t& token) {
    return static_cast<size_t>(token.start) - (token.type == JSMN_STRING ? 1 : 0);
}

//======================================================================================================================
size_t tokenEnd(const jsmntok_t& token) {
    return static_cast<size_t>(token.end) + (token.type == JSMN_STRING ? 1 : 0);
}

//======================================================================================================================
std::string_view tokenText(std::string_view text, const jsmntok_t& token) {
    return text.substr(static_cast<size_t>(token.start),
                       static_cast<size_t>(token.end - token.start));
}

//======================================================================================================================
void skipWhitespace(std::string_view text, size_t& cursor) {
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\n' ||
                                    text[cursor] == '\r' || text[cursor] == '\t')) {
        ++cursor;
    }
}

//======================================================================================================================
bool consume(std::string_view text, size_t& cursor, char expected) {
    skipWhitespace(text, cursor);
    if (cursor == text.size() || text[cursor] != expected) {
        return false;
    }
    ++cursor;
    skipWhitespace(text, cursor);
    return true;
}

//======================================================================================================================
bool numberSyntax(std::string_view text) {
    size_t cursor = 0;
    if (cursor < text.size() && text[cursor] == '-') {
        ++cursor;
    }
    const auto digits = [&] {
        const size_t start = cursor;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        return cursor != start;
    };
    if (cursor < text.size() && text[cursor] == '0') {
        ++cursor;
    } else if (!digits()) {
        return false;
    }
    if (cursor < text.size() && text[cursor] == '.') {
        ++cursor;
        if (!digits()) {
            return false;
        }
    }
    if (cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
            ++cursor;
        }
        if (!digits()) {
            return false;
        }
    }
    return cursor == text.size();
}

//======================================================================================================================
void appendUtf8(std::string& result, uint32_t value) {
    if (value <= 0x7f) {
        result += static_cast<char>(value);
    } else if (value <= 0x7ff) {
        result += static_cast<char>(0xc0 | (value >> 6));
        result += static_cast<char>(0x80 | (value & 0x3f));
    } else if (value <= 0xffff) {
        result += static_cast<char>(0xe0 | (value >> 12));
        result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (value & 0x3f));
    } else {
        result += static_cast<char>(0xf0 | (value >> 18));
        result += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
        result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (value & 0x3f));
    }
}

//======================================================================================================================
AssetResult<uint32_t> readHex(std::string_view text, size_t& cursor, size_t offset) {
    if (text.size() - cursor < 4) {
        return std::unexpected(malformed(offset + cursor, "incomplete Unicode escape"));
    }
    uint32_t value = 0;
    const char* first = text.data() + cursor;
    const auto result = std::from_chars(first, first + 4, value, 16);
    if (result.ec != std::errc{} || result.ptr != first + 4) {
        return std::unexpected(malformed(offset + cursor, "invalid Unicode escape"));
    }
    cursor += 4;
    return value;
}

//======================================================================================================================
AssetResult<std::string> decodeString(std::string_view text, size_t offset) {
    std::string result;
    result.reserve(text.size());
    for (size_t cursor = 0; cursor < text.size();) {
        const auto character = static_cast<unsigned char>(text[cursor++]);
        if (character < 0x20) {
            return std::unexpected(malformed(offset + cursor - 1, "unescaped string control"));
        }
        if (character != '\\') {
            result += static_cast<char>(character);
            continue;
        }
        if (cursor == text.size()) {
            return std::unexpected(malformed(offset + cursor, "incomplete string escape"));
        }
        const char escape = text[cursor++];
        switch (escape) {
        case '"':
        case '\\':
        case '/':
            result += escape;
            break;
        case 'b':
            result += '\b';
            break;
        case 'f':
            result += '\f';
            break;
        case 'n':
            result += '\n';
            break;
        case 'r':
            result += '\r';
            break;
        case 't':
            result += '\t';
            break;
        case 'u': {
            auto code = readHex(text, cursor, offset);
            if (!code) {
                return std::unexpected(code.error());
            }
            if (*code >= 0xd800 && *code <= 0xdbff) {
                if (text.substr(cursor, 2) != "\\u") {
                    return std::unexpected(malformed(offset + cursor, "missing low surrogate"));
                }
                cursor += 2;
                const auto low = readHex(text, cursor, offset);
                if (!low) {
                    return std::unexpected(low.error());
                }
                if (*low < 0xdc00 || *low > 0xdfff) {
                    return std::unexpected(malformed(offset + cursor - 4, "invalid low surrogate"));
                }
                *code = 0x10000 + ((*code - 0xd800) << 10) + (*low - 0xdc00);
            } else if (*code >= 0xdc00 && *code <= 0xdfff) {
                return std::unexpected(malformed(offset + cursor - 4, "unpaired low surrogate"));
            }
            appendUtf8(result, *code);
            break;
        }
        default:
            return std::unexpected(malformed(offset + cursor - 1, "invalid string escape"));
        }
    }
    return result;
}

//======================================================================================================================
AssetResult<void> validateContainer(std::string_view text, const std::vector<jsmntok_t>& tokens,
                                    const std::vector<size_t>& next, size_t index) {
    const auto& container = tokens[index];
    const bool object = container.type == JSMN_OBJECT;
    size_t cursor = static_cast<size_t>(container.start) + 1;
    skipWhitespace(text, cursor);
    size_t count = 0;
    for (size_t child = index + 1; child < next[index];) {
        if (count > 0 && !consume(text, cursor, ',')) {
            return std::unexpected(malformed(cursor, "expected a comma"));
        }
        if (object) {
            if (tokens[child].type != JSMN_STRING || tokenStart(tokens[child]) != cursor) {
                return std::unexpected(malformed(cursor, "expected an object key"));
            }
            cursor = tokenEnd(tokens[child]);
            if (!consume(text, cursor, ':')) {
                return std::unexpected(malformed(cursor, "expected a colon"));
            }
            ++child;
        }
        if (child >= next[index] || tokenStart(tokens[child]) != cursor) {
            return std::unexpected(malformed(cursor, "expected a value"));
        }
        cursor = tokenEnd(tokens[child]);
        skipWhitespace(text, cursor);
        child = next[child];
        ++count;
    }
    if (count != static_cast<size_t>(container.size) ||
        cursor + 1 != static_cast<size_t>(container.end) || text[cursor] != (object ? '}' : ']')) {
        return std::unexpected(malformed(cursor, "unexpected container contents"));
    }
    return {};
}

//======================================================================================================================
AssetResult<void> validateTokens(std::string_view text, const std::vector<jsmntok_t>& tokens,
                                 std::vector<size_t>& next, std::vector<std::string>& strings) {
    next.resize(tokens.size());
    strings.resize(tokens.size());
    for (size_t index = tokens.size(); index-- > 0;) {
        const auto& token = tokens[index];
        size_t following = index + 1;
        if (token.type == JSMN_OBJECT || token.type == JSMN_ARRAY) {
            while (following < tokens.size() && tokens[following].start < token.end) {
                following = next[following];
            }
        }
        next[index] = following;
    }
    size_t cursor = 0;
    skipWhitespace(text, cursor);
    if (tokens.empty() || tokenStart(tokens[0]) != cursor) {
        return std::unexpected(malformed(cursor, "expected a root value"));
    }
    cursor = tokenEnd(tokens[0]);
    skipWhitespace(text, cursor);
    if (next[0] != tokens.size() || cursor != text.size()) {
        return std::unexpected(malformed(cursor, "unexpected content after the root value"));
    }
    for (size_t index = 0; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        if (token.type == JSMN_OBJECT || token.type == JSMN_ARRAY) {
            const auto valid = validateContainer(text, tokens, next, index);
            if (!valid) {
                return valid;
            }
        } else if (token.type == JSMN_STRING) {
            auto decoded = decodeString(tokenText(text, token), static_cast<size_t>(token.start));
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            strings[index] = std::move(*decoded);
        } else {
            const auto value = tokenText(text, token);
            if (value != "true" && value != "false" && value != "null" && !numberSyntax(value)) {
                return std::unexpected(
                    malformed(static_cast<size_t>(token.start), "invalid value"));
            }
        }
    }
    return {};
}

//======================================================================================================================
std::string pointerPart(std::string_view key) {
    std::string result;
    for (const char value : key) {
        if (value == '~') {
            result += "~0";
        } else if (value == '/') {
            result += "~1";
        } else {
            result += value;
        }
    }
    return result;
}

//======================================================================================================================
template <typename T>
std::optional<T> convertNumber(std::string_view text) {
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    return value;
}

} // namespace

//======================================================================================================================
JsonTokens::JsonTokens(std::shared_ptr<const Storage> storage) : m_storage(std::move(storage)) {}

//======================================================================================================================
AssetResult<JsonTokens> JsonTokens::parse(std::string text) {
    if (text.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(malformed(0, "document exceeds tokenizer limits"));
    }
    const size_t nul = text.find('\0');
    if (nul != std::string::npos) {
        return std::unexpected(malformed(nul, "unexpected NUL byte"));
    }
    // Strict jsmn requires a delimiter after a scalar root; whitespace preserves all source
    // offsets.
    text += '\n';
    jsmn_parser parser;
    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, text.data(), text.size(), nullptr, 0);
    if (count <= 0) {
        return std::unexpected(malformed(parser.pos, "invalid or incomplete JSON value"));
    }
    auto storage = std::make_shared<Storage>();
    storage->text = std::move(text);
    storage->tokens.resize(static_cast<size_t>(count));
    jsmn_init(&parser);
    const int parsed = jsmn_parse(&parser, storage->text.data(), storage->text.size(),
                                  storage->tokens.data(), storage->tokens.size());
    if (parsed < 0) {
        return std::unexpected(malformed(parser.pos, "invalid or incomplete JSON value"));
    }
    storage->tokens.resize(static_cast<size_t>(parsed));
    const auto valid =
        validateTokens(storage->text, storage->tokens, storage->next, storage->strings);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    return JsonTokens(std::move(storage));
}

//======================================================================================================================
JsonNode JsonTokens::root() const {
    LMX_ASSERT(m_storage != nullptr, "JSON document has been moved from");
    return JsonNode(m_storage, 0, {});
}

//======================================================================================================================
JsonNode::JsonNode(std::shared_ptr<const JsonTokens::Storage> storage, size_t index,
                   std::string path)
    : m_storage(std::move(storage)), m_index(index), m_path(std::move(path)) {}

//======================================================================================================================
std::string_view JsonNode::tokenText() const {
    return asset::tokenText(m_storage->text, m_storage->tokens[m_index]);
}

//======================================================================================================================
bool JsonNode::isObject() const {
    return m_storage->tokens[m_index].type == JSMN_OBJECT;
}

//======================================================================================================================
bool JsonNode::isArray() const {
    return m_storage->tokens[m_index].type == JSMN_ARRAY;
}

//======================================================================================================================
bool JsonNode::isString() const {
    return m_storage->tokens[m_index].type == JSMN_STRING;
}

//======================================================================================================================
bool JsonNode::isNumber() const {
    return m_storage->tokens[m_index].type == JSMN_PRIMITIVE &&
           (tokenText().front() == '-' ||
            (tokenText().front() >= '0' && tokenText().front() <= '9'));
}

//======================================================================================================================
bool JsonNode::isBool() const {
    return m_storage->tokens[m_index].type == JSMN_PRIMITIVE &&
           (tokenText() == "true" || tokenText() == "false");
}

//======================================================================================================================
bool JsonNode::isNull() const {
    return m_storage->tokens[m_index].type == JSMN_PRIMITIVE && tokenText() == "null";
}

//======================================================================================================================
std::optional<JsonNode> JsonNode::find(std::string_view key) const {
    LMX_ASSERT(isObject(), "JSON member lookup requires an object");
    for (size_t index = m_index + 1; index < m_storage->next[m_index];
         index = m_storage->next[index + 1]) {
        if (m_storage->strings[index] == key) {
            return JsonNode(m_storage, index + 1, m_path + '/' + pointerPart(key));
        }
    }
    return std::nullopt;
}

//======================================================================================================================
size_t JsonNode::size() const {
    LMX_ASSERT(isObject() || isArray(), "JSON size requires an object or array");
    return static_cast<size_t>(m_storage->tokens[m_index].size);
}

//======================================================================================================================
JsonNode JsonNode::at(size_t index) const {
    LMX_ASSERT(isArray() && index < size(), "JSON array index is out of bounds");
    size_t token = m_index + 1;
    for (size_t remaining = index; remaining > 0; --remaining) {
        token = m_storage->next[token];
    }
    return JsonNode(m_storage, token, m_path + '/' + std::to_string(index));
}

//======================================================================================================================
std::string JsonNode::error(std::string_view reason) const {
    return "JSON pointer '" + m_path + "': " + std::string(reason);
}

//======================================================================================================================
std::expected<float, std::string> JsonNode::asFloat() const {
    if (!isNumber()) {
        return std::unexpected(error("expected a number"));
    }
    const auto value = convertNumber<float>(tokenText());
    if (!value) {
        return std::unexpected(error("number is outside the finite float range"));
    }
    return *value;
}

//======================================================================================================================
std::expected<double, std::string> JsonNode::asDouble() const {
    if (!isNumber()) {
        return std::unexpected(error("expected a number"));
    }
    const auto value = convertNumber<double>(tokenText());
    if (!value) {
        return std::unexpected(error("number is outside the finite double range"));
    }
    return *value;
}

//======================================================================================================================
std::expected<uint64_t, std::string> JsonNode::asUInt() const {
    if (!isNumber()) {
        return std::unexpected(error("expected an unsigned integer"));
    }
    const auto value = convertNumber<uint64_t>(tokenText());
    if (!value) {
        return std::unexpected(error("expected an unsigned integer in the uint64 range"));
    }
    return *value;
}

//======================================================================================================================
std::expected<std::string, std::string> JsonNode::asString() const {
    if (!isString()) {
        return std::unexpected(error("expected a string"));
    }
    return m_storage->strings[m_index];
}

//======================================================================================================================
std::expected<bool, std::string> JsonNode::asBool() const {
    if (!isBool()) {
        return std::unexpected(error("expected a boolean"));
    }
    return tokenText() == "true";
}

//======================================================================================================================
const std::string& JsonNode::path() const {
    return m_path;
}

} // namespace lmx::asset
