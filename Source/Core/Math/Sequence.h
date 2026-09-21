//----------------------------------------------------------------------------------------------------------------------
/// @file Sequence.h
/// @brief Provides radical-inverse and Hammersley low-discrepancy sequence generators.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace lmx {

/// Van der Corput radical inverse of `index` in `base`, by repeated digit extraction.
inline float radicalInverse(uint32_t index, uint32_t base) {
    const float inverseBase = 1.0f / static_cast<float>(base);
    float digitScale = inverseBase;
    float result = 0.0f;
    while (index > 0) {
        result += static_cast<float>(index % base) * digitScale;
        index /= base;
        digitScale *= inverseBase;
    }
    return result;
}

/// Van der Corput radical inverse in base 2: the low-discrepancy second coordinate of the
/// Hammersley sequence, produced by reversing the bits of `index`. Each value depends on `index`
/// alone, so these functions are reproducible without carrying a seed.
inline float radicalInverseBase2(uint32_t index) {
    index = (index << 16u) | (index >> 16u);
    index = ((index & 0x55555555u) << 1u) | ((index & 0xAAAAAAAAu) >> 1u);
    index = ((index & 0x33333333u) << 2u) | ((index & 0xCCCCCCCCu) >> 2u);
    index = ((index & 0x0F0F0F0Fu) << 4u) | ((index & 0xF0F0F0F0u) >> 4u);
    index = ((index & 0x00FF00FFu) << 8u) | ((index & 0xFF00FF00u) >> 8u);
    return static_cast<float>(index) * 2.3283064365386963e-10f; // 1 / 2^32
}

/// The `index`-th of `count` Hammersley points: a uniform first coordinate paired with the base-2
/// radical inverse.
inline glm::vec2 hammersley(uint32_t index, uint32_t count) {
    return {static_cast<float>(index) / static_cast<float>(count), radicalInverseBase2(index)};
}

} // namespace lmx
