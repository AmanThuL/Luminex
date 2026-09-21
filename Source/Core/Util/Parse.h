//----------------------------------------------------------------------------------------------------------------------
/// @file Parse.h
/// @brief Provides complete-string numeric parsing without domain range policy.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <charconv>
#include <concepts>
#include <string_view>
#include <system_error>

namespace lmx {

/// Parses the complete string with from_chars's decimal/general-format grammar and no whitespace.
/// Returns false for empty, partial, invalid or out-of-type-range input. On failure the output
/// follows from_chars and may contain a parsed prefix. Positivity, finiteness and domain bounds
/// are the caller's responsibility; successfully parsed infinities and NaNs are not rejected here.
template <typename T>
    requires((std::integral<T> && !std::same_as<T, bool>) || std::floating_point<T>)
bool parseNumber(std::string_view text, T& value) {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

} // namespace lmx
