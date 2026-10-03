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
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lmx::app {

enum class EditorSubject;

/// How activation treats the scene's existing previous transforms.
enum class SceneActivationMotion : uint8_t {
    PreserveLoadedMotion, ///< Headless startup preserves the loader's previous transforms exactly.
    Reset,                ///< Editor activation collapses motion onto the scene's current pose.
};

/// Why an editor subject's pose cannot be changed.
enum class PoseLock {
    None,      ///< A movable subject permits pose edits.
    Static,    ///< Mobility is authored as static or unavailable.
    Generated, ///< The generator owns an object's placement.
    Animated,  ///< Animation owns the subject's transform.
    Measuring, ///< Measurement temporarily prevents pose edits.
};

/// Returns the stable user-facing refusal reason, empty for an unlocked pose.
std::string_view poseLockReason(PoseLock lock);

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

    /// Own flag of a document node, independent of its ancestors and CLI overrides.
    bool nodeEnabled(uint32_t node) const;
    /// Effective document flag including ancestors and the optional CLI group override.
    bool nodeEffectiveEnabled(uint32_t node) const;
    /// Own flag at an imported binding index, not a glTF source-node index.
    bool importedNodeEnabled(uint32_t imported) const;
    /// Effective imported flag including both document and source-node ancestry.
    bool importedNodeEffectiveEnabled(uint32_t imported) const;
    /// Own object flag shared by every primitive of its source node; generated flags are
    /// session-only.
    bool objectEnabled(size_t index) const;
    /// Own local-light flag; false for stale identities, unaffected by ancestor masking.
    bool localLightEnabled(engine::LightId id) const;
    /// Identifies generator-owned subjects using bindings, including subsequently added pile
    /// lights.
    bool isGenerated(EditorSubject subject, size_t index, engine::LightId id) const;
    /// Persists one document flag and reapplies descendant effective flags. Editing the CLI group
    /// clears its session override, including an explicit edit to its unchanged authored value.
    /// Invalid indices and any call during Measure fail without changing state.
    rojoRHI::Result<void> setNodeEnabled(uint32_t node, bool enabled);
    /// Persists an imported own flag and reapplies source descendants, retaining their own flags.
    /// The index names SceneBinding::importedNodes; invalid indices and Measure edits fail.
    rojoRHI::Result<void> setImportedNodeEnabled(uint32_t imported, bool enabled);
    /// Changes all primitives of a bound source node, or only the generated session subject.
    /// Invalid indices and Measure edits fail; a real transition requests a temporal reset.
    rojoRHI::Result<void> setObjectEnabled(size_t index, bool enabled);
    /// Changes a bound node's own flag, or a generated session choice. Stale IDs and Measure fail.
    rojoRHI::Result<void> setLocalLightEnabled(engine::LightId id, bool enabled);
    /// Locks authored and generated enablement while a measurement warms up, renders or drains.
    void setMeasurementActive(bool active) { m_measurementActive = active; }
    /// Reports whether persistent edits are blocked by an active measurement.
    bool measurementActive() const { return m_measurementActive; }
    /// Consumes the one-shot reset requested by changed effective content, independently of dirty.
    bool consumeTemporalReset();
    /// Adopts current persistent subject values as reset baselines after the caller atomically
    /// adopts a successfully saved document. Generated defaults and edit generation are preserved.
    void adoptDocumentResetBaseline();

    /// The borrowed scene, or null before the first activation.
    engine::Scene* activeScene() const { return m_scene; }

    /// Changes on every activation and active-scene invalidation, independently of persistent
    /// edits. Transient interactions use it to reject replaced or reactivated scene storage.
    uint64_t activationGeneration() const { return m_activationGeneration; }

    /// The active scene, editable by its owner; asserts if no scene has been activated.
    engine::Scene& scene() const;

    /// Current persistent look; controls commit a modified copy through editLook.
    const asset::SceneLook& look() const;
    /// Loaded or successfully saved look, retained independently of current edits and playback.
    const asset::SceneLook& lookDefault() const;
    /// Commits changed persistent look values and increments the active scene's edit generation.
    /// An unchanged value is a no-op; exposure feedback reconciliation remains caller-owned.
    void editLook(const asset::SceneLook& look);
    /// Replaces only the reset baseline after a successful canonical Save/Save As adoption.
    /// The caller adopts the document/hash/path together; this neither edits the look nor marks it
    /// dirty. Baseline storage lives until invalidate removes the scene's session state.
    void adoptLookResetBaseline(const asset::SceneLook& saved);
    /// Active scene's persistent-edit notification counter, starting at zero on first activation.
    /// Playback preview and baseline adoption leave it unchanged; switching preserves each count.
    uint64_t editGeneration() const;
    /// Shared notification hook for a completed persistent edit outside editLook. Preview-only
    /// changes must not call it. Dirty tracking uses this as an invalidation, not a dirty verdict.
    void notifyPersistentEdit();

    /// Explicitly requests the current view as the saved rest camera. Ordinary view/playback
    /// changes never call this. Rejects missing documents, Measure and invalid lens/pose values;
    /// a changed request increments persistent generation. The owner requires stopped playback.
    /// The requested yaw is wrapped into [-pi, pi], on the live camera too.
    rojoRHI::Result<void> setSceneCamera();
    /// Edits only the authored document camera; the viewport camera and playback stay unchanged.
    /// Pose uses world metres and radians; invalid or unavailable requests change nothing.
    rojoRHI::Result<void> setSceneCamera(const engine::SceneCamera& camera);
    /// Returns the current authored document camera, including an unsaved explicit request.
    engine::SceneCamera authoredSceneCamera() const;
    /// Replaces an explicitly saved camera with its value decoded from the saved document, so an
    /// approximated orientation compares clean afterwards. No-op without a saved camera request.
    void adoptDocumentCamera(const engine::SceneCamera& camera);

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
    /// The active look is included; renderer configuration and one-shot exposure/temporal state
    /// remain the caller's responsibility.
    render::SceneView view(std::vector<engine::DrawItem>& items, bool wireframe) const;

    /// Collapses object motion after an activation or explicit discontinuity. A camera cut alone
    /// is a temporal latch owned by the caller and does not imply resetting object motion.
    void resetMotion();

    /// Promotes the accepted frame's object poses to previous transforms. Skipped frames never
    /// call this operation; it neither samples animation nor advances the scene clock.
    void commitFrame();

    /// Authored object transform, sampled at current playback time when the object has a track.
    DecomposedTransform objectDefault(size_t index) const;

    /// True when the editable transform differs from its authored default; always false for an
    /// animation-owned object, which no editor route can move.
    bool objectChanged(size_t index) const;

    /// Shared pose permission, with measurement preceding generation, animation and mobility.
    /// Invalid subjects and missing mobility fail closed as Static.
    PoseLock objectPoseLock(size_t index) const;
    /// Shared light pose permission; generated lights retain session-only edits, even with orbits.
    /// Measurement precedes all other locks. Invalid subjects and absent mobility are Static.
    PoseLock lightPoseLock(EditorSubject subject, size_t index, engine::LightId id) const;
    /// Applies a movable object's pose to every primitive of its bound source node and resets
    /// motion. Any pose lock or invalid index fails without changing pose or edit generation.
    rojoRHI::Result<void> editObject(size_t index, const DecomposedTransform& transform);
    /// Whether an object's transform is saved by the document exporter.
    bool objectTransformPersistable(size_t index) const;

    /// Restores one movable object's authored transform through the shared pose permission.
    rojoRHI::Result<void> resetObject(size_t index);

    /// The original scene-linear light retained on first activation, before any editor changes.
    const engine::DirectionalLight& lightDefault(size_t index) const;

    /// Commits directional fields; a pose lock refuses direction changes, preserving its bits
    /// on allowed strength/enabled edits. Measure also refuses enabled changes.
    rojoRHI::Result<void> editLight(size_t index, const engine::DirectionalLight& light);

    /// Restores only the selected light; enabled changes during Measure fail without mutation.
    rojoRHI::Result<void> resetLight(size_t index);

    /// True when direction or scene-linear radiance differs from the authored light.
    bool lightChanged(size_t index) const;

    /// Whether the active document has a top-level local-light group.
    bool localLightRigAvailable() const;

    /// Whether any surviving light under the document local-light group is effectively enabled.
    bool localLightRigEnabled() const;

    /// Observes the existing session-only local-light group mask without changing authored flags.
    /// Absent before activation, before a mask is set, or after an enabled edit clears the mask.
    /// Equal-to-authored overrides remain present; only the group's own enabled edit clears one.
    std::optional<bool> localLightRigOverride() const {
        return m_scene ? m_defaults.at(m_scene).rigOverride : std::nullopt;
    }

    /// Applies a session-only group mask without changing authored child flags or saved values.
    /// A document with no local-light group is a successful no-op.
    rojoRHI::Result<void> setLocalLightRig(bool enabled);

    /// Authored light fields with orbit-owned position sampled at current time; null for stale IDs.
    std::optional<engine::LocalLight> localLightDefault(engine::LightId id) const;
    /// Whether this live light differs from its authored/current-track default.
    bool localLightChanged(engine::LightId id) const;
    /// Validates and edits one live light before prepareFrame, retaining its original reset value.
    /// The supplied enabled field is its own flag, so callers read it with localLightEnabled.
    /// Pose locks refuse position/direction changes; allowed non-pose edits preserve pose bits.
    /// Measure also rejects enabled changes; generated changes never notify persistent dirty.
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
        asset::SceneLook look;
        uint64_t editGeneration = 0;
        std::vector<DecomposedTransform> objects;
        std::array<engine::DirectionalLight, 3> lights;
        std::unordered_map<uint64_t, engine::LocalLight> localLights;
        std::vector<engine::LightId> pileLights;
        std::vector<bool> objectOwnEnabled;
        std::unordered_map<uint64_t, bool> lightOwnEnabled;
        std::optional<bool> rigOverride;
    };

    void followCameraTrack();
    void rememberLocalLightDefaults();
    void applyEnabled();
    std::vector<bool> effectiveNodes() const;
    std::vector<bool> effectiveImported(const std::vector<bool>& nodes) const;
    bool persistentObject(size_t index) const;

    bool m_measurementActive = false;
    bool m_temporalResetPending = false;
    uint64_t m_activationGeneration = 0;
    engine::Scene* m_scene = nullptr;
    engine::Camera m_camera;
    std::unordered_map<const engine::Scene*, Defaults> m_defaults;
    engine::LoadedScene* m_loaded = nullptr;
    std::unordered_map<const engine::Scene*, scenes::SessionDocumentState> m_documentStates;
};

} // namespace lmx::app
