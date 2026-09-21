//----------------------------------------------------------------------------------------------------------------------
/// @file SceneAnimation.h
/// @brief Declares rigid, camera, and emissive animation tracks and their samplers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lmx::asset {

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

/// One camera pose in a camera track. Angles follow `engine::Camera`: yaw about +Y, pitch about
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

/// One held emissive-strength value in an `EmissiveTrack`.
struct EmissiveKey {
    double time = 0.0;     ///< Seconds from the clip start; keys are sorted.
    float strength = 1.0f; ///< Multiplier applied to the object's authored emissive colour.
};

/// A time-sorted, step-held emissive-strength sequence driving one `Scene::objects` entry.
struct EmissiveTrack {
    uint32_t objectIndex = 0;      ///< Index into `Scene::objects`.
    std::vector<EmissiveKey> keys; ///< Time-sorted keys; sampling requires at least one.
};

/// One closed-form orbit driving a scene local light's position. `light` indexes the scene's
/// authored lights in creation order (the order `Scene::addLight` was called), not a scene
/// `LightId` directly: ADR 0025 layers `asset` below `engine` below `render`, enforced by
/// `Tools/module_contract.json`, so `Scene` itself owns the mapping from this index back to the
/// `LightId` it assigned (`Scene::animationLightId`). That index space freezes at
/// `Scene::finalize`: `light` must reference a light added *before* finalize, since tracks are
/// authored before a scene plays; a light added afterward (a runtime rig toggle, say) has no
/// index and is static by contract. Only the light's position moves; direction, colour, intensity
/// and range are untouched.
struct LightOrbitTrack {
    uint32_t light = 0;               ///< Pre-finalize creation-order index (see comment above).
    glm::vec3 centre{0.0f};           ///< World-space orbit centre, metres.
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; ///< Orbit-plane normal; the input need not be unit length.
    float radius = 0.0f;              ///< Orbit radius, metres.
    float phase = 0.0f;               ///< Starting angle in radians at seconds == 0.
    float period = 0.0f; ///< Seconds per revolution; <= 0 means the light does not move.
};

/// Every track a scene plays, with the clip length the scene clock wraps against.
struct SceneAnimation {
    std::vector<RigidTrack> tracks;     ///< One track per animated object; unlisted objects rest.
    std::vector<CameraKey> cameraTrack; ///< Optional camera path; empty means the scene has none.
    std::vector<EmissiveTrack>
        emissiveTracks; ///< One track per object with animated emissive strength.
    std::vector<LightOrbitTrack> lightTracks; ///< One track per local light orbiting a closed path.
    double duration = 0.0;                    ///< Clip length in seconds; 0 means nothing to play.
    bool loop = true;                         ///< Whether the clock wraps at `duration`.
};

/// Whether the clip has any rigid, camera, emissive, or light-orbit tracks for playback to advance.
bool hasAnimationTracks(const SceneAnimation& animation);

/// Returns `track`'s world matrix at `time` seconds, as translate * rotate * scale. Translation
/// and scale interpolate linearly and rotation slerps between the bracketing keys; a `step` track
/// holds the earlier key instead. Times outside the key range clamp to the first or last key.
/// `track.keys` must not be empty.
glm::mat4 sampleRigidTrack(const RigidTrack& track, double time);

/// Returns the camera pose at `time` seconds: position and both angles interpolate linearly
/// between the bracketing keys and clamp outside the key range. The returned key's `time` is the
/// requested `time`. `keys` must not be empty.
CameraKey sampleCameraTrack(std::span<const CameraKey> keys, double time);

/// Returns `track`'s emissive strength at `time` seconds: the last key with `time <= t`, held
/// (step) until the next key, and clamped to the first or last key outside the range.
/// `track.keys` must not be empty.
float sampleEmissiveTrack(const EmissiveTrack& track, double time);

/// Returns `track`'s position at `seconds`: `centre + radius * (cos(a) * u + sin(a) * v)`, where
/// `a = phase + 2*pi*seconds/period` and `(u, v)` is a deterministic orthonormal basis
/// perpendicular to the normalized `axis` (world up, or world +X when `axis` is nearly parallel to
/// it). `period <= 0` fixes `a = phase`, so the light does not move over time. `track.axis` must
/// be nonzero; its length otherwise does not matter.
glm::vec3 sampleOrbit(const LightOrbitTrack& track, float seconds);

} // namespace lmx::asset
