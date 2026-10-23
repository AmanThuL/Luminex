//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutlineFrameData.h
/// @brief Declares the selection outline frame data and the pure functions that build it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Matches Shaders/Passes/SelectionOutline/SelectionMask.slang. Coverage is non-color data; all
/// scene transforms remain borrowed.
struct MaskParams {
    glm::mat4 viewProj;    ///< 0 -- unjittered world-to-clip transform.
    uint32_t instanceRow;  ///< 64 -- instance row of the draw being rasterized.
    float selected;        ///< 68 -- coverage value written, one for the selected draw.
    uint32_t padding[2]{}; ///< 72 -- explicit ABI padding.
};
static_assert(sizeof(MaskParams) == 80);
static_assert(offsetof(MaskParams, instanceRow) == 64);
static_assert(offsetof(MaskParams, selected) == 68);

/// Matches Shaders/Passes/SelectionOutline/SelectionOutline.slang's constant block.
struct OutlineParams {
    float radius; ///< 0 -- outline radius in target pixels.
};
static_assert(sizeof(OutlineParams) == 4);

/// Builds the coverage parameters of one draw: the transform and row as given, the coverage value
/// one, zero padding.
inline MaskParams buildMaskParams(const glm::mat4& viewProjection, uint32_t instanceRow) {
    return MaskParams{.viewProj = viewProjection, .instanceRow = instanceRow, .selected = 1.0f};
}

/// Builds the outline parameters: radius is 1.5 target pixels per backing-scale unit, clamped to
/// [1, 6] pixels.
inline OutlineParams buildOutlineParams(float backingScale) {
    return OutlineParams{std::clamp(backingScale * 1.5f, 1.0f, 6.0f)};
}

} // namespace lmx::render
