//----------------------------------------------------------------------------------------------------------------------
/// @file EditorPlayback.cpp
/// @brief Implements reversible playback snapshots and transport transitions.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/EditorPlayback.h"

#include "Core/Assert.h"

namespace lmx::app {

//======================================================================================================================
bool EditorPlayback::matches(const SceneSession& session) const {
    const auto* scene = session.activeScene();
    return m_snapshot && scene && m_snapshot->source == scene &&
           (!m_snapshot->identity || scene->tryMesh(*m_snapshot->identity));
}

//======================================================================================================================
void EditorPlayback::capture(const SceneSession& session, bool followRail) {
    const auto& scene = session.scene();
    m_snapshot.emplace();
    auto& snapshot = *m_snapshot;
    snapshot.source = &scene;
    if (!scene.objects.empty())
        snapshot.identity = scene.objects.front().mesh;
    snapshot.camera = session.camera();
    snapshot.time = scene.animationTime;
    snapshot.followRail = followRail;
    for (const auto& track : scene.animation.tracks) {
        LMX_ASSERT(track.objectIndex < scene.objects.size(), "rigid track object must exist");
        const auto& object = scene.objects[track.objectIndex];
        auto& saved = snapshot.objects[object.id.slot];
        saved.id = object.id;
        saved.transform =
            asset::DecomposedTransform{object.position, object.eulerDegrees, object.scale};
    }
    for (const auto& track : scene.animation.emissiveTracks) {
        LMX_ASSERT(track.objectIndex < scene.objects.size(), "emissive track object must exist");
        const auto& object = scene.objects[track.objectIndex];
        auto& saved = snapshot.objects[object.id.slot];
        saved.id = object.id;
        saved.emissiveStrength = object.emissiveStrength;
    }
}

//======================================================================================================================
bool EditorPlayback::play(SceneSession& session, bool followRail) {
    if (!session.activeScene())
        return false;
    const bool current = matches(session);
    const bool changed = !playing() || !current;
    if (!current)
        capture(session, followRail);
    m_state = PlaybackState::Playing;
    return changed;
}

//======================================================================================================================
bool EditorPlayback::pause() {
    if (!playing())
        return false;
    m_state = PlaybackState::Paused;
    return true;
}

//======================================================================================================================
bool EditorPlayback::stop(SceneSession& session, bool& followRail) {
    const bool restore = matches(session);
    if (restore) {
        auto& scene = session.scene();
        session.camera() = m_snapshot->camera;
        scene.animationTime = m_snapshot->time;
        followRail = m_snapshot->followRail;
        // Scan live objects once: slot lookup stays linear for densely animated labs, while the
        // full identity prevents a removed object's recycled slot from inheriting its old pose.
        for (auto& object : scene.objects) {
            const auto saved = m_snapshot->objects.find(object.id.slot);
            if (saved == m_snapshot->objects.end() || saved->second.id != object.id)
                continue;
            if (const auto& transform = saved->second.transform) {
                object.position = transform->position;
                object.eulerDegrees = transform->eulerDegrees;
                object.scale = transform->scale;
            }
            if (saved->second.emissiveStrength)
                object.emissiveStrength = *saved->second.emissiveStrength;
        }
        session.resetMotion();
    }
    m_snapshot.reset();
    m_state = PlaybackState::Stopped;
    return restore;
}

//======================================================================================================================
bool EditorPlayback::step(SceneSession& session, bool followRail) {
    if (!session.activeScene())
        return false;
    if (!matches(session))
        capture(session, followRail);
    m_state = PlaybackState::Paused;
    session.stepAnimation();
    session.advanceEditorFrame(false, followRail, false);
    return true;
}

} // namespace lmx::app
