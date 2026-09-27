//----------------------------------------------------------------------------------------------------------------------
/// @file JsonTokens.h
/// @brief Declares immutable JSON token ownership and typed document navigation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::asset {

/// A typed view into shared immutable JSON storage, declared below.
class JsonNode;

/// Owns immutable source text and validated JSON tokens. Copies share the same storage; nodes keep
/// that storage alive independently of this object, including across moves and destruction.
class JsonTokens {
public:
    /// Parses one complete JSON value. Malformed input reports a zero-based byte offset; numeric
    /// representability is checked by typed node accessors, not by the syntax parser.
    static AssetResult<JsonTokens> parse(std::string text);
    /// Returns the document root. Calling this on a moved-from document asserts.
    JsonNode root() const;

private:
    struct Storage;
    explicit JsonTokens(std::shared_ptr<const Storage> storage);
    std::shared_ptr<const Storage> m_storage;
    friend class JsonNode;
};

/// Shares ownership of immutable token storage; remains valid after its JsonTokens is destroyed.
/// Navigation creates new nodes with RFC 6901 JSON pointers. Reads are safe on separate copies
/// across threads. Typed conversions report type/range errors with the node's pointer.
class JsonNode {
public:
    /// Returns whether this value is an object.
    bool isObject() const;
    /// Returns whether this value is an array.
    bool isArray() const;
    /// Returns whether this value is a string.
    bool isString() const;
    /// Returns whether this value is a JSON number, independently of representable range.
    bool isNumber() const;
    /// Returns whether this value is true or false.
    bool isBool() const;
    /// Returns whether this value is null.
    bool isNull() const;
    /// Finds the first member with a decoded key, or returns empty. Asserts unless this is an
    /// object.
    std::optional<JsonNode> find(std::string_view key) const;
    /// Returns array length or object member count; asserts for scalar values.
    size_t size() const;
    /// Returns a decoded object key in source order; asserts for non-objects or invalid index.
    std::string memberName(size_t index) const;
    /// Returns the corresponding object value with its escaped pointer; same bounds contract.
    JsonNode memberValue(size_t index) const;
    /// Returns an array element; asserts unless this is an array and index is smaller than size().
    JsonNode at(size_t index) const;
    /// Reads a finite float with complete-token from_chars conversion; overflow/underflow fails.
    std::expected<float, std::string> asFloat() const;
    /// Reads a finite double with complete-token from_chars conversion; overflow/underflow fails.
    std::expected<double, std::string> asDouble() const;
    /// Reads an unsigned 64-bit integer; signs, fractions, exponents and out-of-range values fail.
    std::expected<uint64_t, std::string> asUInt() const;
    /// Returns an owned UTF-8 string with JSON escapes and surrogate pairs decoded.
    std::expected<std::string, std::string> asString() const;
    /// Reads a JSON boolean; string and number values do not coerce.
    std::expected<bool, std::string> asBool() const;
    /// Returns the JSON pointer owned by this node; the root pointer is the empty string.
    const std::string& path() const;

private:
    JsonNode(std::shared_ptr<const JsonTokens::Storage> storage, size_t index, std::string path);
    std::string_view tokenText() const;
    std::string error(std::string_view reason) const;
    std::shared_ptr<const JsonTokens::Storage> m_storage;
    size_t m_index;
    std::string m_path;
    friend class JsonTokens;
};

} // namespace lmx::asset
