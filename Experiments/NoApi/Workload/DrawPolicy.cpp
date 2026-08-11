//----------------------------------------------------------------------------------------------------------------------
/// @file DrawPolicy.cpp
/// @brief Defines the frozen draw, material, and per-frame update algorithm.
//----------------------------------------------------------------------------------------------------------------------

#include "Workload/DrawPolicy.h"
#include "Workload/RepresentativeGraph.h"
#include "Workload/Splitmix64.h"

#include <cmath>
#include <numbers>

namespace lmx::noapi::workload {

namespace {
// Parameter tags fold into the splitmix64 draw alongside the draw index, so each scalar is its
// own independent draw rather than three bits sliced from one.
constexpr uint64_t kRoughnessTag = 0;
constexpr uint64_t kMetallicTag = 1;
constexpr uint64_t kEmissiveScaleTag = 2;

// One quad's footprint in world units; the grid is centred on the origin.
constexpr float kGridSpacing = 1.0f;
constexpr float kGridHalfExtent = (kDrawGridSize - 1) * kGridSpacing * 0.5f;

constexpr float kCameraRadius = 40.0f;
constexpr float kCameraHeight = 24.0f;
} // namespace

//======================================================================================================================
GridPosition drawGridPosition(uint32_t drawIndex) {
    return {.x = drawIndex % kDrawGridSize, .y = drawIndex / kDrawGridSize};
}

//======================================================================================================================
uint32_t drawMaterialIndex(uint32_t drawIndex) { return drawIndex % kMaterialCount; }

//======================================================================================================================
DrawMaterialParams drawMaterialParams(uint32_t drawIndex) {
    return {.roughness = unitFloat(splitmix64(kSeed, {drawIndex, kRoughnessTag})),
           .metallic = unitFloat(splitmix64(kSeed, {drawIndex, kMetallicTag})),
           .emissiveScale = unitFloat(splitmix64(kSeed, {drawIndex, kEmissiveScaleTag}))};
}

//======================================================================================================================
DrawPlacement drawPlacement(uint32_t drawIndex) {
    const GridPosition grid = drawGridPosition(drawIndex);
    return {.x = static_cast<float>(grid.x) * kGridSpacing - kGridHalfExtent,
           .z = static_cast<float>(grid.y) * kGridSpacing - kGridHalfExtent};
}

//======================================================================================================================
CameraPose cameraForFrame(uint32_t frameIndex) {
    const float angle = 2.0f * std::numbers::pi_v<float> *
                        (static_cast<float>(frameIndex % kCorrectnessFrameCount) /
                         static_cast<float>(kCorrectnessFrameCount));
    return {.eyeX = kCameraRadius * std::cos(angle),
           .eyeY = kCameraHeight,
           .eyeZ = kCameraRadius * std::sin(angle),
           .targetX = 0.0f,
           .targetY = 0.0f,
           .targetZ = 0.0f};
}

//======================================================================================================================
EmissiveStagingTexel emissiveStagingTexel(uint32_t frameIndex, uint32_t x, uint32_t y) {
    const uint64_t draw = splitmix64(kSeed, {frameIndex, x, y});
    uint8_t bytes[4];
    unitRgba8(draw, bytes);
    return {.r = bytes[0], .g = bytes[1], .b = bytes[2], .a = bytes[3]};
}

} // namespace lmx::noapi::workload
