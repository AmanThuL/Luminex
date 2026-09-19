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
    for (const auto& track : scene.animation.lightTracks) {
        const auto id = scene.animationLightId(track.light);
        LMX_ASSERT(id.has_value(), "light orbit track light must exist");
        const auto* value = scene.light(*id);
        if (!value)
            continue; // The authored light was removed before this preview started.
        // Only the animation-owned position is captured: a track never claims the whole light, so
        // a colour/intensity/range/direction edit made during the preview is never discarded.
        auto& saved = snapshot.lights[id->slot];
        saved.id = *id;
        saved.position = value->position;
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
        // Same identity-checked scan as objects above: a removed light is never revived, and a
        // recycled slot's replacement never inherits the removed light's captured position. Every
        // other field is read fresh from the light's *current* value, so an edit made during the
        // preview to a tracked light's colour, intensity, range or direction survives Stop exactly
        // as an untracked light's edits do.
        for (const auto id : scene.localLights()) {
            const auto saved = m_snapshot->lights.find(id.slot);
            if (saved == m_snapshot->lights.end() || saved->second.id != id)
                continue;
            render::LocalLight current = *scene.light(id);
            current.position = saved->second.position;
            const auto restored = scene.updateLight(id, current);
            LMX_ASSERT(restored.has_value(),
                       "restoring a captured light's position must remain valid");
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
