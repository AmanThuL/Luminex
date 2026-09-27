//----------------------------------------------------------------------------------------------------------------------
/// @file JsonWriter.h
/// @brief Declares deterministic JSON document writing and round-trip float formatting.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lmx {

/// Returns the shortest locale-independent decimal that round-trips to the same float bits.
/// Preserves negative zero; asserts when value is not finite.
std::string formatShortest(float value);

/// Builds one JSON value with two-space indentation, insertion-ordered keys and a final newline.
/// Inline arrays keep their nested containers on the same line. Strings preserve UTF-8 bytes and
/// escape JSON controls. Invalid call order and non-finite numbers assert. Instances are not shared
/// between threads; take() finishes a balanced document and resets the writer for reuse.
class JsonWriter {
public:
    /// Starts an object as the root, an array element, or a value after key().
    void beginObject();
    /// Closes the current object; asserts if a key still needs its value.
    void endObject();
    /// Starts an array; inlineLayout keeps elements and all nested containers on one line.
    void beginArray(bool inlineLayout = false);
    /// Closes the current array.
    void endArray();
    /// Writes the next object key; the next operation must write its value.
    void key(std::string_view name);
    /// Writes an escaped, quoted string value.
    void string(std::string_view value);
    /// Writes a finite float using its shortest round-trip representation.
    void number(float value);
    /// Writes a finite double using its shortest round-trip representation.
    void number(double value);
    /// Writes a signed or unsigned integer exactly, without floating-point conversion.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    void integer(T value) {
        if constexpr (std::is_signed_v<T>) {
            signedInteger(static_cast<int64_t>(value));
        } else {
            unsignedInteger(static_cast<uint64_t>(value));
        }
    }
    /// Writes a JSON true or false value.
    void boolean(bool value);
    /// Returns the completed document and resets this writer; asserts on empty or unclosed output.
    std::string take();

private:
    struct Scope {
        char close;
        bool inlineLayout;
        bool first = true;
        bool needsValue = false;
    };

    void beginValue();
    void separateItem();
    void beginContainer(char open, char close, bool inlineLayout);
    void endContainer(char close);
    void signedInteger(int64_t value);
    void unsignedInteger(uint64_t value);
    void quoted(std::string_view value);

    std::string m_text;
    std::vector<Scope> m_stack;
    bool m_hasRoot = false;
};

} // namespace lmx
