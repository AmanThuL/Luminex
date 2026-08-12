//----------------------------------------------------------------------------------------------------------------------
/// @file Splitmix64.h
/// @brief Declares Splitmix64 for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the frozen splitmix64 generator every synthetic asset and draw parameter in
/// the
///        M5.1 workload manifest derives from.

#pragma once
#include <cstdint>
#include <initializer_list>

/// Deterministic, API-neutral workload generation shared by both M5.1 encoders
/// (`docs/specs/2026-08-12-m5.1-rhi-execution-model-design.md` section 6). Everything here is a
/// pure function of its inputs, so two runs of the same build -- on either adapter -- produce
/// byte-identical synthetic assets and draw parameters.
namespace lmx::experimental::noapi::workload {

/// The frozen seed every generator in the scored core derives from (spec section 6).
inline constexpr uint64_t kSeed = 0x4C4D5835ull;

/// One splitmix64 step (Steele, Lea & Flood 2014): advances `state` and returns the next 64-bit
/// output. The golden-ratio increment and the two xorshift-multiply rounds are the published
/// constants verbatim -- this is not a project-local variant.
constexpr uint64_t splitmix64Step(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// The one deterministic draw every generator in the manifest calls through: folds `indices` into
/// `seed` one splitmix64 step at a time, then returns one final step's output. Same (seed,
/// indices...) always yields the same value, independent of machine, build, or run -- indices are
/// taken in argument order, so `splitmix64(seed, {material, texture, texel})` and
/// `splitmix64(seed, {texture, material, texel})` are deliberately different draws.
constexpr uint64_t splitmix64(uint64_t seed, std::initializer_list<uint64_t> indices) {
    uint64_t state = seed;
    for (uint64_t index : indices) {
        state ^= splitmix64Step(state) + index;
    }
    return splitmix64Step(state);
}

/// Maps a splitmix64 draw to a float in [0, 1) using its top 24 bits, the usual construction for a
/// uniform mantissa-width float from a wide integer draw.
constexpr float unitFloat(uint64_t draw) {
    constexpr uint64_t kMantissaBits = 24;
    constexpr uint64_t kMantissaScale = uint64_t{1} << kMantissaBits;
    return static_cast<float>(draw >> (64 - kMantissaBits)) / static_cast<float>(kMantissaScale);
}

/// Maps a splitmix64 draw to one RGBA8 texel, each channel independently drawn from consecutive
/// bytes of `draw`.
constexpr void unitRgba8(uint64_t draw, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>(draw >> 0);
    out[1] = static_cast<uint8_t>(draw >> 8);
    out[2] = static_cast<uint8_t>(draw >> 16);
    out[3] = static_cast<uint8_t>(draw >> 24);
}

} // namespace lmx::experimental::noapi::workload
