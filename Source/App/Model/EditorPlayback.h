//----------------------------------------------------------------------------------------------------------------------
/// @file EditorPlayback.h
/// @brief Declares reversible editor playback over a borrowed scene session.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/SceneSession.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace lmx::app {

/// Whether the editor has an active animation preview and advances it each frame.
enum class PlaybackState : uint8_t {
    Stopped, ///< No preview snapshot; editing establishes the next starting pose.
    Playing, ///< Preview advances from the existing scene time.
    Paused   ///< Preview retains its snapshot without advancing automatically.
};

/// Restores the camera, clock, rail preference and animation-owned fields when a preview stops.
/// The session's scene must remain alive until stopped; stop before activating another scene.
/// Independent object, material and light edits survive Stop. Removed object identities are never
/// recreated, and a snapshot from a different active scene is discarded without restoring it.
class EditorPlayback {
public:
    /// Current transport state, initially Stopped.
    PlaybackState state() const { return m_state; }
    /// Whether Play or Step has established a preview that can be stopped.
    bool active() const { return m_state != PlaybackState::Stopped; }
    /// Whether ordinary frame preparation should advance animation.
    bool playing() const { return m_state == PlaybackState::Playing; }

    /// Captures the starting edit state when stopped and begins playback without rewinding or
    /// sampling. Resuming a paused preview keeps its original snapshot. Returns a state change;
    /// no active scene returns false. A different active scene begins a fresh preview.
    bool play(SceneSession& session, bool followRail);

    /// Pauses a playing preview without changing scene values or replacing its snapshot.
    /// Returns true only for a Playing-to-Paused transition.
    bool pause();

    /// Restores the preview's camera, time, rail preference and still-live animated fields, then
    /// collapses motion and clears the snapshot. Returns true when a matching scene was restored;
    /// a stopped or mismatched preview returns false. The caller owns the temporal reset latch.
    bool stop(SceneSession& session, bool& followRail);

    /// Captures the starting state if needed, advances exactly one animation step, follows the
    /// camera rail when requested and leaves the preview paused. Returns false without a scene.
    bool step(SceneSession& session, bool followRail);

private:
    struct ObjectSnapshot {
        scene::InstanceId id;
        std::optional<asset::DecomposedTransform> transform;
        std::optional<float> emissiveStrength;
    };
    struct Snapshot {
        const scene::Scene* source = nullptr;
        std::optional<scene::MeshId> identity;
        render::Camera camera;
        double time = 0;
        bool followRail = false;
        std::unordered_map<uint32_t, ObjectSnapshot> objects;
    };

    bool matches(const SceneSession& session) const;
    void capture(const SceneSession& session, bool followRail);

    PlaybackState m_state = PlaybackState::Stopped;
    std::optional<Snapshot> m_snapshot;
};

} // namespace lmx::app
