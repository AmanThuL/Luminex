//----------------------------------------------------------------------------------------------------------------------
/// @file DrawPolicy.h
/// @brief Declares DrawPolicy for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the frozen draw, material, and per-frame update algorithm P04's 1,024 draws
///        follow (spec section 6).

#pragma once
#include <cstdint>

namespace lmx::experimental::noapi::workload {

/// Grid position of draw `drawIndex` in the frozen 32x32 grid (spec: "draw i instances the shared
/// quad at grid position (i mod 32, i div 32)"). `drawIndex` is in [0, kDrawCount).
struct GridPosition {
    uint32_t x = 0;
    uint32_t y = 0;
};
GridPosition drawGridPosition(uint32_t drawIndex);

/// Material index of draw `drawIndex` (spec: "material index = i mod 64, so every consecutive draw
/// switches material").
uint32_t drawMaterialIndex(uint32_t drawIndex);

/// Scalar material parameters for draw `drawIndex`, each independently drawn from
/// splitmix64(kSeed, drawIndex, <parameter tag>) (spec: "scalar material parameters derive from
/// splitmix64(seed, i)"). Object transforms are static (spec section 6), so these do not vary with
/// frame index -- only with draw index.
struct DrawMaterialParams {
    float roughness = 0.0f;     ///< Unit-mapped, [0, 1).
    float metallic = 0.0f;      ///< Unit-mapped, [0, 1).
    float emissiveScale = 0.0f; ///< Unit-mapped, [0, 1).
};
DrawMaterialParams drawMaterialParams(uint32_t drawIndex);

/// World-space placement of draw `drawIndex`'s quad instance: the grid position above, spaced one
/// unit apart and centred on the origin. Static across frames (spec section 6: "object transforms
/// are static").
struct DrawPlacement {
    float x = 0.0f;
    float z = 0.0f;
};
DrawPlacement drawPlacement(uint32_t drawIndex);

/// The camera's world-space eye and look-at target for `frameIndex` (spec: "the camera animates as
/// a fixed function of frame index"): eye orbits the grid's centre at a fixed height and radius,
/// completing one revolution over the frozen kCorrectnessFrameCount-frame correctness run.
struct CameraPose {
    float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
};
CameraPose cameraForFrame(uint32_t frameIndex);

/// R10's staging content for frame `frameIndex`: kMaterialTextureSize^2 RGBA8 texels, each a
/// deterministic function of frame index and texel position (spec: "staging content is a
/// deterministic function of frame index"), the content P02 copies into material 0's emissive
/// mip 0 that frame.
struct EmissiveStagingTexel {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};
EmissiveStagingTexel emissiveStagingTexel(uint32_t frameIndex, uint32_t x, uint32_t y);

} // namespace lmx::experimental::noapi::workload
