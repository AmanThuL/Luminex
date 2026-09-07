//----------------------------------------------------------------------------------------------------------------------
/// @file Temporal.h
/// @brief Declares the per-frame temporal contract: extents, jitter, camera state and motion.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "RHI/Format.h"
#include "Render/Camera.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <limits>

namespace lmx::render {

/// The two pixel extents a frame renders at. They are equal today and declared separately so a
/// later upscaling stage can diverge them without changing consumers. Motion vectors, jitter and
/// the projection's aspect ratio are all relative to the *render* extent.
struct FrameExtents {
    uint32_t renderWidth = 0;  ///< Width in pixels of the target the scene rasterises into.
    uint32_t renderHeight = 0; ///< Height in pixels of the target the scene rasterises into.
    uint32_t outputWidth = 0;  ///< Width in pixels of the presented image.
    uint32_t outputHeight = 0; ///< Height in pixels of the presented image.
};

/// Number of jitter samples before the sequence repeats.
constexpr uint32_t kJitterSequenceLength = 16;

/// Returns sample `index` of the Halton(2, 3) sequence recentred on the pixel: each axis is the
/// radical inverse minus a half, in pixels of the render extent. The sequence is phased from 1,
/// so `index` 0 evaluates the radical inverses at 1 and the degenerate sample at the pixel's
/// corner never occurs; every value lies strictly inside (-0.5, 0.5) on both axes.
/// The index wraps at kJitterSequenceLength, so callers may pass a free-running frame counter.
glm::vec2 haltonJitterPixels(uint32_t index);

/// The camera transforms one frame is rendered with, derived once and shared by every pass.
/// Motion vectors are built from the *unjittered* pair; only rasterisation uses the jittered pair.
struct CameraFrameState {
    glm::mat4 view{1.0f};                   ///< Right-handed world-to-view transform.
    glm::mat4 projection{1.0f};             ///< Reversed infinite-far view-to-clip transform.
    glm::mat4 projectionJittered{1.0f};     ///< projection with the sub-pixel NDC offset applied.
    glm::mat4 viewProjection{1.0f};         ///< projection * view.
    glm::mat4 viewProjectionJittered{1.0f}; ///< projectionJittered * view.
    glm::mat4 inverseViewProjection{1.0f};  ///< Inverse of viewProjection, clip back to world.
    glm::vec2 jitterPixels{0.0f};           ///< Offset in render-extent pixels, +x/+y as in NDC.
    glm::vec3 position{0.0f};               ///< Camera position in world space.
    float fovY = 0.0f;                      ///< Vertical field of view in radians.
    float nearZ = 0.0f;                     ///< Positive near-plane distance in world units.
};

/// Builds the frame's camera state. The aspect ratio comes from the render extent, whose height
/// must be non-zero. A jitter of (0, 0) leaves the jittered matrices identical to the plain ones.
CameraFrameState buildCameraFrameState(const Camera& camera, const FrameExtents& extents,
                                       glm::vec2 jitterPixels);

/// How a draw's motion is produced.
enum class MotionClass : uint8_t {
    Rigid,  ///< Reprojected through the item's previous model matrix.
    Invalid ///< Writes the kMotionInvalid sentinel; history must not be reprojected.
};

/// Storage format of the `lmx.render.motion` target: a signed two-channel UV delta.
constexpr rhi::Format kMotionFormat = rhi::Format::RG16Float;

/// Per-component sentinel written when motion is undefined. Consumers test it with `isinf`; zero
/// is a legitimate motion value and must never stand in for "unknown".
constexpr float kMotionInvalid = std::numeric_limits<float>::infinity();

/// Converts an unjittered clip position to the motion target's texture-space UV:
/// `ndc.xy * (0.5, -0.5) + 0.5`, so +y runs down the image over the render extent.
glm::vec2 clipToMotionUv(const glm::vec4& clip);

/// Returns `uvCurrent - uvPrevious` for a surface point, from unjittered clip positions of this
/// frame and the previous one. A consumer fetches history at `uv - motion`.
glm::vec2 motionBetween(const glm::vec4& clipCurrent, const glm::vec4& clipPrevious);

} // namespace lmx::render
