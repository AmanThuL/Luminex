//----------------------------------------------------------------------------------------------------------------------
/// @file AlphaMaskParams.h
/// @brief Declares the shared masked-draw cutoff block and binding.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx::render {

/// Shader cutoff block shared by masked scene and shadow draws.
struct AlphaMaskParams {
    float cutoff; ///< Texture alpha times factor alpha must meet this threshold.
};
static_assert(sizeof(AlphaMaskParams) == 4);
/// Frame-data binding used by Shaders/AlphaMask.slang.
constexpr uint32_t kAlphaMaskParamsSlot = 4;

} // namespace lmx::render
