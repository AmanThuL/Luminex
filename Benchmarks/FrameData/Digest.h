//----------------------------------------------------------------------------------------------------------------------
/// @file Digest.h
/// @brief Declares the byte-exact readback digest FrameDataBench's --verify mode reports.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace lmx::bench {

/// Deterministic FNV-1a (64-bit) digest of one readback buffer. Used by `--verify` so the paired
/// driver can refuse to compare timing between a baseline and candidate run whose rendered output
/// differs byte-for-byte -- a small local implementation rather than a third-party hash, since a
/// stable, dependency-free digest is all the comparison needs.
inline uint64_t fnv1a64(std::span<const std::byte> bytes) {
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a 64-bit offset basis.
    for (const std::byte b : bytes) {
        hash ^= static_cast<uint64_t>(b);
        hash *= 0x100000001b3ULL; // FNV-1a 64-bit prime.
    }
    return hash;
}

/// Formats a digest as a fixed-width, lowercase hex string for JSON and CLI output.
inline std::string digestToHex(uint64_t digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kHex[digest & 0xF];
        digest >>= 4;
    }
    return out;
}

} // namespace lmx::bench
