//----------------------------------------------------------------------------------------------------------------------
/// @file SceneAnimation.cpp
/// @brief Implements rigid, camera, and emissive track sampling and playback queries.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Model/SceneAnimation.h"

#include "Core/Diagnostics/Assert.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>

namespace lmx::asset {

namespace {

/// The bracketing key indices for `time` plus the blend weight between them. Times outside the
/// range collapse onto one key with weight zero, which is what makes sampling clamp at both ends.
struct KeySpan {
    size_t earlier = 0;
    size_t later = 0;
    double weight = 0.0;
};

//======================================================================================================================
template <typename Key>
KeySpan bracket(std::span<const Key> keys, double time) {
    LMX_ASSERT(!keys.empty(), "sampling an animation track requires at least one key");
    const size_t last = keys.size() - 1;
    if (time <= keys.front().time) {
        return {.earlier = 0, .later = 0, .weight = 0.0};
    }
    if (time >= keys[last].time) {
        return {.earlier = last, .later = last, .weight = 0.0};
    }
    size_t earlier = 0;
    while (earlier + 1 < last && keys[earlier + 1].time <= time) {
        ++earlier;
    }
    const double span = keys[earlier + 1].time - keys[earlier].time;
    // Coincident keys have no interval to blend across; the earlier one wins.
    const double weight = span > 0.0 ? (time - keys[earlier].time) / span : 0.0;
    return {.earlier = earlier, .later = earlier + 1, .weight = weight};
}

} // namespace

//======================================================================================================================
bool hasAnimationTracks(const SceneAnimation& animation) {
    return !animation.tracks.empty() || !animation.cameraTrack.empty() ||
           !animation.emissiveTracks.empty() || !animation.lightTracks.empty();
}

//======================================================================================================================
glm::mat4 sampleRigidTrack(const RigidTrack& track, double time) {
    const std::span<const RigidKey> keys(track.keys);
    const KeySpan span = bracket(keys, time);
    const RigidKey& earlier = keys[span.earlier];
    const RigidKey& later = keys[span.later];

    glm::vec3 translation = earlier.translation;
    glm::quat rotation = earlier.rotation;
    glm::vec3 scale = earlier.scale;
    if (!track.step) {
        const auto weight = static_cast<float>(span.weight);
        translation = glm::mix(earlier.translation, later.translation, weight);
        scale = glm::mix(earlier.scale, later.scale, weight);
        rotation = glm::slerp(earlier.rotation, later.rotation, weight);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.0f), translation);
    model *= glm::mat4_cast(glm::normalize(rotation));
    return glm::scale(model, scale);
}

//======================================================================================================================
CameraKey sampleCameraTrack(std::span<const CameraKey> keys, double time) {
    const KeySpan span = bracket(keys, time);
    const CameraKey& earlier = keys[span.earlier];
    const CameraKey& later = keys[span.later];
    const auto weight = static_cast<float>(span.weight);
    return {.time = time,
            .position = glm::mix(earlier.position, later.position, weight),
            .yaw = glm::mix(earlier.yaw, later.yaw, weight),
            .pitch = glm::mix(earlier.pitch, later.pitch, weight)};
}

//======================================================================================================================
float sampleEmissiveTrack(const EmissiveTrack& track, double time) {
    LMX_ASSERT(!track.keys.empty(), "sampling an emissive track requires at least one key");
    const std::span<const EmissiveKey> keys(track.keys);
    size_t index = 0;
    while (index + 1 < keys.size() && keys[index + 1].time <= time) {
        ++index;
    }
    return keys[index].strength;
}

//======================================================================================================================
glm::vec3 sampleOrbit(const LightOrbitTrack& track, float seconds) {
    LMX_ASSERT(glm::length(track.axis) > 0.0f, "LightOrbitTrack.axis must be nonzero");
    const glm::vec3 axis = glm::normalize(track.axis);
    // A deterministic basis perpendicular to axis: world up, unless axis is nearly parallel to it,
    // where world +X keeps the cross product well-conditioned instead of collapsing toward zero.
    const glm::vec3 reference =
        std::abs(axis.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(reference, axis));
    const glm::vec3 v = glm::cross(axis, u);
    float angle = track.phase;
    if (track.period > 0.0f) {
        // Wrapping onto one period before the multiply keeps the angle's float precision constant
        // regardless of how large `seconds` has grown, instead of coarsening across a long session.
        const float wrapped = std::fmod(seconds, track.period);
        angle += static_cast<float>(2.0 * glm::pi<double>() * wrapped / track.period);
    }
    return track.centre + track.radius * (std::cos(angle) * u + std::sin(angle) * v);
}

} // namespace lmx::asset
