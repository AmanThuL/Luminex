//----------------------------------------------------------------------------------------------------------------------
/// @file String.h
/// @brief Declares string utility functions for Core.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <string>
#include <string_view>

namespace lmx {

/// Converts ASCII uppercase letters (A–Z) to lowercase, leaving all other bytes unchanged.
/// Non-ASCII UTF-8 sequences and non-alphabetic characters pass through untouched.
inline std::string toLowerAscii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        if (c >= 'A' && c <= 'Z') {
            result.push_back(static_cast<char>(c + 32));
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

} // namespace lmx
