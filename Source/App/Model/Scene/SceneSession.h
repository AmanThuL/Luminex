//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSession.h
/// @brief Declares shared scene activation, playback, and borrowed frame views.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/Scene.h"
#include "Engine/View/Camera.h"
#include "Render/Renderer/SceneView.h"
#include "Scenes/SceneDocuments.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lmx::app {

/// How activation treats the scene's existing previous transforms.
enum class SceneActivationMotion : uint8_t {
    PreserveLoadedMotion, ///< Headless startup preserves the loader's previous transforms exactly.
    Reset,                ///< Editor activation collapses motion onto the scene's current pose.
};

/// A borrowed active scene and its application-owned camera, independent of windowing and input.
///
/// The scene library and its GPU resources outlive this session. Activation restores the authored
/// camera, applies the explicit initial-motion policy, and preserves playback time and pose.
/// No operation waits on the device: the owner drains outstanding work before replacing a scene.
/// Playback preparation runs only for frames the owner will declare; commitFrame runs only after
/// graph execution accepts the frame, after the headless wait when using CPU readback.
class SceneSession {
public:
    /// Selects an already-loaded scene and restores its initial camera. Editor activation resets
    /// motion; headless startup preserves the loader's existing previous transforms exactly.
    void activate(engine::Scene& scene, SceneActivationMotion motion);
    /// Activates a complete document snapshot and retains its separate authored flags.
    void activate(engine::LoadedScene& loaded, SceneActivationMotion motion);
    /// Invalidates all pointer-keyed defaults before SceneLibrary replaces an old snapshot. If
    /// active, detaches it; the caller clears selection and activates the replacement afterward.
    void invalidate(const engine::Scene& scene);
    /// Returns the active complete snapshot, or null for CPU-only scene fixtures.
    engine::LoadedScene* loadedScene() const { return m_loaded; }
    /// Authored document and imported flags; CLI group overrides leave these values untouched.
    const scenes::SessionDocumentState& documentState() const;

    /// The borrowed scene, or null before the first activation.
    engine::Scene* activeScene() const { return m_scene; }

    /// The active scene, editable by its owner; asserts if no scene has been activated.
    engine::Scene& scene() const;

    /// The camera edited by the owner, including fly input and lens changes.
    engine::Camera& camera() { return m_camera; }
    /// The camera used when declaring passes for this session.
    const engine::Camera& camera() const { return m_camera; }

    /// Advances playing tracks by one bake-rate step and follows a camera track when requested.
    /// An active fly-camera override suppresses follow without pausing animation. Paused playback
    /// may still follow the current track pose; lens state is never changed by track sampling.
    void advanceEditorFrame(bool playing, bool followCamera, bool flyCameraOverride);

    /// Preserves the authored pose on frame zero; each later frame advances tracked animation by
    /// one bake-rate step, including clip wrapping. Frames are supplied once in increasing order.
    /// A camera track is followed even on frame zero, preserving its authored first pose.
    void prepareScreenshotFrame(uint32_t frame);

    /// Samples at absolute `frame / asset::kAnimationBakeRate` seconds, including warmup frames.
    /// The scene clock does not wrap: track sampling clamps after its last key, as capture does.
    void prepareSequenceFrame(uint32_t frame);

    /// Advances and samples exactly one bake-rate step, even on a scene without tracks. Used for
    /// an explicit paused step; ordinary playback checks for tracks before stepping.
    void stepAnimation();

    /// Rewinds to time zero, samples its object pose, and resets object motion for the
    /// discontinuity. The owner also raises its temporal reset latch; this operation does not
    /// consume that latch or follow the camera track before the owner's ordinary frame preparation.
    void rewindAnimation();

    /// Uploads this frame's changed scene rows after Device::beginFrame has paced the slot.
    rojoRHI::Result<void> prepareFrame(uint64_t frameNumber);

    /// Read-only diagnostics for the active scene's most recently prepared table slot.
    engine::SceneTableStats tableStats() const;

    /// Fills caller-owned items and borrows them in the returned view. The view, items, camera, and
    /// scene resources must stay alive and unmodified through pass declaration and graph execution.
    /// Render settings and one-shot exposure/temporal state remain the caller's responsibility.
    render::SceneView view(std::vector<engine::DrawItem>& items, render::ShadowFilter filter,
                           bool wireframe) const;

    /// Collapses object motion after an activation or explicit discontinuity. A camera cut alone
    /// is a temporal latch owned by the caller and does not imply resetting object motion.
    void resetMotion();

    /// Promotes the accepted frame's object poses to previous transforms. Skipped frames never
    /// call this operation; it neither samples animation nor advances the scene clock.
    void commitFrame();

    /// Authored object transform, sampled at current playback time when the object has a track.
    DecomposedTransform objectDefault(size_t index) const;

    /// True when the editable transform differs from its authored/current-track default.
    bool objectChanged(size_t index) const;

    /// Applies an editor transform at the current playback time and collapses this object's motion.
    /// Playing tracks replace the edit at the next sample; no other object is modified.
    void editObject(size_t index, const DecomposedTransform& transform);

    /// Restores one object's authored/current-track transform, preserving other objects and time.
    void resetObject(size_t index);

    /// The original scene-linear light retained on first activation, before any editor changes.
    const engine::DirectionalLight& lightDefault(size_t index) const;

    /// Restores only the selected light, retaining all other light and object edits.
    void resetLight(size_t index);

    /// True when direction or scene-linear radiance differs from the authored light.
    bool lightChanged(size_t index) const;

    /// Whether the active document has a top-level local-light group.
    bool localLightRigAvailable() const;

    /// Whether any surviving light under the document local-light group is effectively enabled.
    bool localLightRigEnabled() const;

    /// Applies a session-only group mask without changing authored child flags or saved values.
    /// A document with no local-light group is a successful no-op.
    rojoRHI::Result<void> setLocalLightRig(bool enabled);

    /// Authored light fields with orbit-owned position sampled at current time; null for stale IDs.
    std::optional<engine::LocalLight> localLightDefault(engine::LightId id) const;
    /// Whether this live light differs from its authored/current-track default.
    bool localLightChanged(engine::LightId id) const;
    /// Validates and edits one live light before prepareFrame, retaining its original reset value.
    rojoRHI::Result<void> editLocalLight(engine::LightId id, const engine::LocalLight& light);
    /// Restores all authored fields and current orbit position; stale/foreign IDs return
    /// InvalidDesc.
    rojoRHI::Result<void> resetLocalLight(engine::LightId id);
    /// Whether the active scene has a LightLab population; the first generator owns this control.
    bool lightLabPileAvailable() const;
    /// Number of this session's currently live pile lights; unrelated additions are excluded.
    uint32_t lightLabPileCount() const;
    /// Largest requested pile preserving every current non-pile light under the 4096 live cap.
    uint32_t lightLabPileCapacity() const;
    /// Replaces only the first LightLab generator's pile before prepareFrame. Added lights are
    /// static; oversized requests fail transactionally. Authored lights, every grid and other
    /// generator populations retain their identities and bindings.
    rojoRHI::Result<void> setLightLabPile(uint32_t count);

private:
    struct Defaults {
        std::vector<DecomposedTransform> objects;
        std::array<engine::DirectionalLight, 3> lights;
        std::unordered_map<uint64_t, engine::LocalLight> localLights;
        std::vector<engine::LightId> pileLights;
    };

    void followCameraTrack();
    void rememberLocalLightDefaults();

    engine::Scene* m_scene = nullptr;
    engine::Camera m_camera;
    std::unordered_map<const engine::Scene*, Defaults> m_defaults;
    engine::LoadedScene* m_loaded = nullptr;
    std::unordered_map<const engine::Scene*, scenes::SessionDocumentState> m_documentStates;
};

} // namespace lmx::app
