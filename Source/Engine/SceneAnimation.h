//----------------------------------------------------------------------------------------------------------------------
/// @file SceneAnimation.h
/// @brief Declares rigid and camera animation tracks and their samplers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lmx::engine {

/// One world-space pose in a rigid track. Keys are authored, or baked from a glTF clip, in the
/// object's own world space -- no parent hierarchy is evaluated at sample time.
struct RigidKey {
    double time = 0.0;                          ///< Seconds from the clip start; keys are sorted.
    glm::vec3 translation{0.0f};                ///< World-space translation in scene units.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World-space rotation, unit length.
    glm::vec3 scale{1.0f};                      ///< World-space per-axis scale.
};

/// A time-sorted pose sequence driving one `Scene::objects` entry.
struct RigidTrack {
    uint32_t objectIndex = 0;   ///< Index into `Scene::objects`.
    std::vector<RigidKey> keys; ///< Time-sorted keys; sampling requires at least one.
    /// When true the track holds the previous key until the next key's time is reached, instead of
    /// interpolating between them.
    bool step = false;
};

/// One camera pose in a camera track. Angles follow `render::Camera`: yaw about +Y, pitch about
/// the camera's right axis, both in radians.
struct CameraKey {
    double time = 0.0;        ///< Seconds from the clip start; keys are sorted.
    glm::vec3 position{0.0f}; ///< World-space camera position.
    float yaw = 0.0f;         ///< Yaw in radians.
    float pitch = 0.0f;       ///< Pitch in radians.
};

/// Rate animation is resampled at when a clip is baked into keys, in samples per second. It
/// matches the fixed step the scene clock advances by, so a baked key lands on every played time
/// exactly.
constexpr double kAnimationBakeRate = 60.0;

/// Every track a scene plays, with the clip length the scene clock wraps against.
struct SceneAnimation {
    std::vector<RigidTrack> tracks;     ///< One track per animated object; unlisted objects rest.
    std::vector<CameraKey> cameraTrack; ///< Optional camera path; empty means the scene has none.
    double duration = 0.0;              ///< Clip length in seconds; 0 means nothing to play.
    bool loop = true;                   ///< Whether the clock wraps at `duration`.
};

/// Returns `track`'s world matrix at `time` seconds, as translate * rotate * scale. Translation
/// and scale interpolate linearly and rotation slerps between the bracketing keys; a `step` track
/// holds the earlier key instead. Times outside the key range clamp to the first or last key.
/// `track.keys` must not be empty.
glm::mat4 sampleRigidTrack(const RigidTrack& track, double time);

/// Returns the camera pose at `time` seconds: position and both angles interpolate linearly
/// between the bracketing keys and clamp outside the key range. The returned key's `time` is the
/// requested `time`. `keys` must not be empty.
CameraKey sampleCameraTrack(std::span<const CameraKey> keys, double time);

} // namespace lmx::engine
