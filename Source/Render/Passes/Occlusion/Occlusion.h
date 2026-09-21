//----------------------------------------------------------------------------------------------------------------------
/// @file Occlusion.h
/// @brief Declares ordered previous-view box projection and the CPU depth oracle.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Core/Math/Aabb.h"
#include <array>
#include <cstddef>
#include <glm/mat4x4.hpp>
#include <span>
namespace lmx::render {
/// Positive clip-w retention guard, fixed before measurement.
inline constexpr float kOcclusionNearGuard = 1e-6f;
/// Reversed-depth retention guard, fixed before measurement.
inline constexpr float kOcclusionDepthGuard = 1e-5f;
/// Retention classification stored in state-word bits 8 through 11.
enum class OcclusionOutcome : uint32_t {
    NotTested,      ///< Occlusion disabled or prior frustum/bypass result.
    Retained,       ///< Tested depth does not prove rejection.
    HistoryInvalid, ///< Source evidence globally invalid.
    NearCrossing,   ///< A corner crosses the source near plane or is nonfinite.
    OutsideSource,  ///< Guarded rectangle leaves the source active extent.
    RectTooLarge    ///< No available level covers the rectangle in at most two texels per axis.
};
/// Exact slot-13 shader ABI; explicit rows avoid matrix storage ambiguity.
struct OcclusionParams {
    std::array<glm::vec4, 4> sourceRows{};   ///< Rows of the source jittered view-projection.
    uint32_t sourceWidth = 0;                ///< Source active width.
    uint32_t sourceHeight = 0;               ///< Source active height.
    uint32_t levelCount = 0;                 ///< Available half-resolution pyramid levels.
    uint32_t flags = 0;                      ///< Bit zero enabled; bit one history valid.
    float nearGuard = kOcclusionNearGuard;   ///< Minimum reliable positive clip w.
    float depthGuard = kOcclusionDepthGuard; ///< Bias toward retention in reversed depth.
    std::array<uint32_t, 2> padding{};       ///< Explicit ABI padding.
};
static_assert(sizeof(OcclusionParams) == 96);
static_assert(offsetof(OcclusionParams, sourceWidth) == 64);
static_assert(offsetof(OcclusionParams, flags) == 76);
static_assert(offsetof(OcclusionParams, nearGuard) == 80);
static_assert(offsetof(OcclusionParams, padding) == 88);
/// Source-space half-open texel rectangle and the selected mip/depth comparison input.
struct OcclusionProjection {
    std::array<int32_t, 4> rectangle{}; ///< Left, top, exclusive right, exclusive bottom.
    uint32_t level = 0;                 ///< Selected pyramid mip, meaningful for Retained.
    float zBox = 0;                     ///< Nearest reversed source depth of the eight corners.
    OcclusionOutcome outcome = OcclusionOutcome::NotTested; ///< Projection retention reason.
    bool occluded = false; ///< Exact strict depth inequality rejected the candidate.
};
/// Borrowed padded mip data for CPU parity; only active texels may be sampled.
struct OcclusionLevel {
    std::span<const float> depth; ///< Row-major float values.
    uint32_t width = 0;           ///< Allocated row stride in texels.
};
/// Builds the row-oriented ABI from the actual source rasterization matrix.
OcclusionParams makeOcclusionParams(const glm::mat4& matrix, uint32_t width, uint32_t height,
                                    uint32_t levels, bool enabled, bool valid);
/// Projects bounds with the exact shader operation order; never reads a depth pyramid.
OcclusionProjection projectOcclusionBounds(const Aabb& bounds, const OcclusionParams& params);
/// Evaluates the strict conservative test against CPU-readable pyramid levels.
OcclusionProjection testOcclusionBounds(const Aabb& bounds, const OcclusionParams& params,
                                        std::span<const OcclusionLevel> levels);
/// Returns the readable retained-candidate diagnostic.
const char* occlusionOutcomeName(OcclusionOutcome outcome);
} // namespace lmx::render
