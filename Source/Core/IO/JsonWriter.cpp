//----------------------------------------------------------------------------------------------------------------------
/// @file JsonWriter.cpp
/// @brief Implements deterministic JSON formatting with finite round-trip numbers.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/IO/JsonWriter.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/Json.h"

#include <charconv>
#include <cmath>
#include <utility>

namespace lmx {
namespace {

//======================================================================================================================
template <typename T>
std::string shortestNumber(T value) {
    LMX_ASSERT(std::isfinite(value), "JSON numbers must be finite");
    char buffer[128];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    LMX_ASSERT(result.ec == std::errc{}, "JSON number formatting failed");
    return {buffer, result.ptr};
}

} // namespace

//======================================================================================================================
std::string formatShortest(float value) {
    return shortestNumber(value);
}

//======================================================================================================================
void JsonWriter::separateItem() {
    Scope& scope = m_stack.back();
    if (!scope.first) {
        m_text += ',';
    }
    if (scope.inlineLayout) {
        if (!scope.first) {
            m_text += ' ';
        }
    } else {
        m_text += '\n';
        m_text.append(m_stack.size() * 2, ' ');
    }
    scope.first = false;
}

//======================================================================================================================
void JsonWriter::beginValue() {
    if (m_stack.empty()) {
        LMX_ASSERT(!m_hasRoot, "JSON document already has a root value");
        m_hasRoot = true;
    } else if (m_stack.back().close == '}') {
        LMX_ASSERT(m_stack.back().needsValue, "JSON object value requires a key");
        m_stack.back().needsValue = false;
    } else {
        separateItem();
    }
}

//======================================================================================================================
void JsonWriter::beginContainer(char open, char close, bool inlineLayout) {
    beginValue();
    const bool inheritedInline = !m_stack.empty() && m_stack.back().inlineLayout;
    m_text += open;
    m_stack.push_back({close, inlineLayout || inheritedInline});
}

//======================================================================================================================
void JsonWriter::endContainer(char close) {
    LMX_ASSERT(!m_stack.empty() && m_stack.back().close == close,
               "JSON container closing order does not match");
    const Scope scope = m_stack.back();
    LMX_ASSERT(!scope.needsValue, "JSON object key has no value");
    m_stack.pop_back();
    if (!scope.first && !scope.inlineLayout) {
        m_text += '\n';
        m_text.append(m_stack.size() * 2, ' ');
    }
    m_text += close;
}

//======================================================================================================================
void JsonWriter::beginObject() {
    beginContainer('{', '}', false);
}

//======================================================================================================================
void JsonWriter::endObject() {
    endContainer('}');
}

//======================================================================================================================
void JsonWriter::beginArray(bool inlineLayout) {
    beginContainer('[', ']', inlineLayout);
}

//======================================================================================================================
void JsonWriter::endArray() {
    endContainer(']');
}

//======================================================================================================================
void JsonWriter::quoted(std::string_view value) {
    m_text += '"';
    appendJsonEscaped(m_text, value);
    m_text += '"';
}

//======================================================================================================================
void JsonWriter::key(std::string_view name) {
    LMX_ASSERT(!m_stack.empty() && m_stack.back().close == '}', "JSON key requires an object");
    LMX_ASSERT(!m_stack.back().needsValue, "JSON key replaces a missing value");
    separateItem();
    quoted(name);
    m_text += ": ";
    m_stack.back().needsValue = true;
}

//======================================================================================================================
void JsonWriter::string(std::string_view value) {
    beginValue();
    quoted(value);
}

//======================================================================================================================
void JsonWriter::number(float value) {
    beginValue();
    m_text += formatShortest(value);
}

//======================================================================================================================
void JsonWriter::number(double value) {
    beginValue();
    m_text += shortestNumber(value);
}

//======================================================================================================================
void JsonWriter::signedInteger(int64_t value) {
    beginValue();
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    LMX_ASSERT(result.ec == std::errc{}, "JSON integer formatting failed");
    m_text.append(buffer, result.ptr);
}

//======================================================================================================================
void JsonWriter::unsignedInteger(uint64_t value) {
    beginValue();
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    LMX_ASSERT(result.ec == std::errc{}, "JSON integer formatting failed");
    m_text.append(buffer, result.ptr);
}

//======================================================================================================================
void JsonWriter::boolean(bool value) {
    beginValue();
    m_text += value ? "true" : "false";
}

//======================================================================================================================
std::string JsonWriter::take() {
    LMX_ASSERT(m_hasRoot && m_stack.empty(), "JSON document must contain one completed value");
    m_text += '\n';
    m_hasRoot = false;
    return std::exchange(m_text, {});
}

} // namespace lmx
