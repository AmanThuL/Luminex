//----------------------------------------------------------------------------------------------------------------------
/// @file Temporal.cpp
/// @brief Implements the jitter sequence, per-frame camera state and motion conversions.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Temporal.h"

#include "Core/Assert.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>

namespace lmx::render {
namespace {

//======================================================================================================================
float radicalInverse(uint32_t index, uint32_t base) {
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

} // namespace

//======================================================================================================================
FrameExtents renderExtentsForScale(uint32_t outputWidth, uint32_t outputHeight, float scale) {
    LMX_ASSERT(outputWidth > 0 && outputHeight > 0,
               "renderExtentsForScale: the output extent must be non-empty");

    const auto axis = [](uint32_t output, float factor) {
        const float rounded = std::round(static_cast<float>(output) * factor);
        // A degenerate render extent has no pixels to reconstruct from, and one above the output
        // would be supersampling rather than upscaling; both ends are clamped rather than asserted
        // because a caller's scale is a continuous control.
        const float clamped = std::clamp(rounded, 1.0f, static_cast<float>(output));
        return static_cast<uint32_t>(clamped);
    };

    return FrameExtents{.renderWidth = axis(outputWidth, scale),
                        .renderHeight = axis(outputHeight, scale),
                        .outputWidth = outputWidth,
                        .outputHeight = outputHeight};
}

//======================================================================================================================
glm::vec2 jitterTexelOffset(glm::vec2 jitterPixels) {
    // Jitter is authored in NDC's +y-up pixels; texture space runs +y down, so only y flips.
    return {jitterPixels.x, -jitterPixels.y};
}

//======================================================================================================================
glm::vec2 renderSamplePosition(glm::uvec2 outputPixel, const FrameExtents& extents,
                               glm::vec2 jitterPixels) {
    LMX_ASSERT(extents.outputWidth > 0 && extents.outputHeight > 0,
               "renderSamplePosition: the output extent must be non-empty");

    const glm::vec2 ratio{
        static_cast<float>(extents.renderWidth) / static_cast<float>(extents.outputWidth),
        static_cast<float>(extents.renderHeight) / static_cast<float>(extents.outputHeight)};
    const glm::vec2 centre{static_cast<float>(outputPixel.x) + 0.5f,
                           static_cast<float>(outputPixel.y) + 0.5f};
    return centre * ratio + jitterTexelOffset(jitterPixels);
}

//======================================================================================================================
glm::vec2 haltonJitterPixels(uint32_t index) {
    // Phased from 1: the radical inverse of 0 is 0 on every base, which would place the first
    // sample of each period exactly on the pixel's corner.
    const uint32_t sample = index % kJitterSequenceLength + 1;
    return {radicalInverse(sample, 2) - 0.5f, radicalInverse(sample, 3) - 0.5f};
}

//======================================================================================================================
CameraFrameState buildCameraFrameState(const Camera& camera, const FrameExtents& extents,
                                       glm::vec2 jitterPixels) {
    LMX_ASSERT(extents.renderWidth > 0 && extents.renderHeight > 0,
               "buildCameraFrameState: the render extent must be non-empty");
    LMX_ASSERT(extents.renderWidth <= extents.outputWidth &&
                   extents.renderHeight <= extents.outputHeight,
               "buildCameraFrameState: the render extent must not exceed the output extent");

    const float width = static_cast<float>(extents.renderWidth);
    const float height = static_cast<float>(extents.renderHeight);

    CameraFrameState state;
    state.view = camera.viewMatrix();
    // The aspect ratio is the presented image's, so the frustum a scale change rasterises is the
    // one the output shows; only the sampling density moves with the render extent.
    state.projection = camera.projectionMatrix(static_cast<float>(extents.outputWidth) /
                                               static_cast<float>(extents.outputHeight));

    // A pixel spans 2/extent in NDC, so a sub-pixel offset in pixels doubles on the way in. The
    // translate is applied on the clip side of the projection, before the perspective divide,
    // which is what makes the shift constant in pixels at every depth.
    const glm::vec3 ndcOffset{2.0f * jitterPixels.x / width, 2.0f * jitterPixels.y / height, 0.0f};
    state.projectionJittered = glm::translate(glm::mat4{1.0f}, ndcOffset) * state.projection;

    state.viewProjection = state.projection * state.view;
    state.viewProjectionJittered = state.projectionJittered * state.view;
    state.inverseViewProjection = glm::inverse(state.viewProjection);
    state.jitterPixels = jitterPixels;
    state.position = camera.position;
    state.fovY = camera.fovY;
    state.nearZ = camera.nearZ;
    return state;
}

//======================================================================================================================
glm::vec2 clipToMotionUv(const glm::vec4& clip) {
    const glm::vec2 ndc = glm::vec2{clip} / clip.w;
    return ndc * glm::vec2{0.5f, -0.5f} + 0.5f;
}

//======================================================================================================================
glm::vec2 motionBetween(const glm::vec4& clipCurrent, const glm::vec4& clipPrevious) {
    return clipToMotionUv(clipCurrent) - clipToMotionUv(clipPrevious);
}

} // namespace lmx::render
