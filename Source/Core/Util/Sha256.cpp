//----------------------------------------------------------------------------------------------------------------------
/// @file Sha256.cpp
/// @brief Implements the lowercase hex SHA-256 digest helper.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Util/Sha256.h"

namespace lmx {

//======================================================================================================================
std::string sha256Hex(std::span<const std::byte> bytes) {
    Sha256 hasher;
    hasher.update(bytes);
    const std::array<uint8_t, 32> digest = hasher.finish();
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        hex[i * 2] = kHexDigits[digest[i] >> 4];
        hex[i * 2 + 1] = kHexDigits[digest[i] & 0xF];
    }
    return hex;
}

} // namespace lmx
