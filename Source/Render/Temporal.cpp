//----------------------------------------------------------------------------------------------------------------------
/// @file Temporal.cpp
/// @brief Implements the jitter sequence, per-frame camera state and motion conversions.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Temporal.h"

#include "Core/Assert.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

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

    const float width = static_cast<float>(extents.renderWidth);
    const float height = static_cast<float>(extents.renderHeight);

    CameraFrameState state;
    state.view = camera.viewMatrix();
    state.projection = camera.projectionMatrix(width / height);

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
