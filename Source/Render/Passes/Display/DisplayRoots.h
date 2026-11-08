//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayRoots.h
/// @brief Declares the display transform's shader root layout.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Views and scene-linear bloom strength consumed by the display fragment shader.
struct DisplayRoot {
    uint32_t sceneColor;  ///< Sampled scene-color view.
    uint32_t bloom;       ///< Sampled bloom view, unused when intensity is zero.
    uint32_t padding = 0; ///< Explicit zero padding shared with the shader mirror.
    float bloomIntensity; ///< Scene-linear bloom multiplier.
};

static_assert(sizeof(DisplayRoot) == 16);
static_assert(offsetof(DisplayRoot, sceneColor) == 0);
static_assert(offsetof(DisplayRoot, bloom) == 4);
static_assert(offsetof(DisplayRoot, padding) == 8);
static_assert(offsetof(DisplayRoot, bloomIntensity) == 12);

} // namespace lmx::render
