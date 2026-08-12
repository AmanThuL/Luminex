//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.h
/// @brief Declares the docked editor shell and its per-frame interface.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorActions.h"
#include "App/EditorRenderSettings.h"
#include "App/EditorSelection.h"
#include "App/ExposureReset.h"
#include "App/FrameRecordRing.h"
#include "App/PerformanceModel.h"
#include "App/WorkspaceModel.h"
#include "Engine/SceneLibrary.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/// SDL is an implementation detail of the shell's input handling and of nothing else here, so the
/// header takes the window as an opaque handle and every consumer that only wants the scene
/// (Screenshot.cpp) stays free of SDL.
struct SDL_Window;

namespace lmx::app {

/// Display-space neutral clear value used by the editor scene target.
constexpr float kSceneClearGray = 0.7f;

/// Maps a Scene's initial pose (Engine/Scene.h's SceneCamera -- position/yaw/pitch/fovY/near/far,
/// deliberately not render::Camera so Engine never has to carry the fly-camera's App-only
/// moveSpeed) to a fresh render::Camera, which keeps Camera's own default moveSpeed. Shared by
/// EditorShell's startup and scene-switch paths and by Screenshot.cpp's offscreen path, so a
/// scene's screenshot and its editor view start from exactly the same pose.
render::Camera cameraFromScene(const engine::SceneCamera& sceneCamera);

/// Luminex's own section of `imgui.ini`, as the registered Dear ImGui settings handler sees it.
///
/// Dear ImGui settings handlers are plain C function pointers, so the shell hands one of these to
/// the handler as its `UserData` instead of exposing itself. Reading fills `sectionText` with the
/// section body found on disk (and sets `sectionSeen`), which the shell hands to
/// `parseWorkspaceSettings` once at startup; writing serializes `visibility` back out through
/// `writeWorkspaceSettings`, so the persisted key spelling has exactly one owner.
struct WorkspaceSettings {
    /// Whether the loaded `imgui.ini` contained Luminex's workspace section at all. False for a
    /// clean run and for an ini written before the section existed.
    bool sectionSeen = false;
    /// The section body exactly as read back, newline-terminated per line. Empty until a read.
    std::string sectionText;
    /// The live panel visibility this shell draws from and persists.
    WorkspaceVisibility visibility;
};

/// The editor shell: the Dear ImGui context, the dockspace and its five panels, the fly camera, and
/// the active engine::Scene the Inspector edits. One per process -- ImGui's context, and the
/// Metal 4 renderer glue behind it, are both process-global -- which is why this is created
/// through a factory and is neither copyable nor movable.
///
/// It owns no RHI object and no Scene. The SceneLibrary (which owns every Scene it has built, for
/// the device's lifetime) is passed in and outlives the shell; everything else the shell touches
/// (the scene target it displays, the device it resizes against) is passed in per call, which
/// keeps the destruction order the RHI requires visible in run(): the shell is declared last and
/// so torn down first, while the device is still alive for imguiShutdown() to drain against.
class EditorShell {
public:
    /// Creates the ImGui context and both backends (order per Metal4ImGui.h), then loads the
    /// requested initial scene and points the camera at its initial pose. Returns nullptr after
    /// logging, with nothing left initialised, if either ImGui backend refuses or the initial scene
    /// fails to load --
    /// both are startup-fatal, unlike a later scene switch (the Scene panel -> selectScene), which
    /// logs and keeps the previous scene active instead.
    static std::unique_ptr<EditorShell> create(SDL_Window* window, rhi::Device& device,
                                               engine::SceneLibrary& library,
                                               engine::SceneId initialScene);
    /// Releases the ImGui context and renderer integration while the device remains alive.
    ~EditorShell();

    /// Editor shells are unique owners of process-global ImGui state.
    EditorShell(const EditorShell&) = delete;
    /// Editor shells cannot replace their process-global ImGui state by assignment.
    EditorShell& operator=(const EditorShell&) = delete;

    /// Resizes the scene target to match the Viewport panel, once the panel has reported one size
    /// for kResizeDebounceFrames consecutive frames.
    ///
    /// Call it outside both an ImGui frame and a Device frame -- it drains the GPU and frees the
    /// old targets. Running it *after* the Viewport image was recorded would leave ImGui's draw
    /// data naming a texture that no longer exists, which is why the frame loop calls it at the
    /// top of the frame rather than next to the size measurement that feeds it.
    void applyPendingViewportResize(rhi::Device& device, render::Renderer& renderer);

    /// Builds the whole UI for this frame and applies camera input. Between ImGui::NewFrame() and
    /// ImGui::Render(). Takes the device because selecting a new scene this frame drains the GPU
    /// (device.waitIdle()) before the library builds or hands back the scene.
    ///
    /// `frameRecords` feeds both observability views: the Render Graph panel shows one exact
    /// retired frame, while Performance rolls timings from successive retired frames into a stable
    /// summary. At this point the frame loop has not retained the current frame, so both see the
    /// newest joined record as of the previous iteration.
    ///
    /// Heals the Scene panel's selection against the active scene before any panel draws (spec
    /// section 5): a stale scene id or out-of-range index resolves to None and the healed value is
    /// what the Inspector sees this frame.
    void buildUI(rhi::Device& device, render::Renderer& renderer, float deltaSeconds,
                 const FrameRecordRing& frameRecords);

    /// This frame's scene, valid until the next call -- it spans a draw list this shell owns.
    /// Build the UI first: the Inspector edits the active scene's objects and lights that this
    /// SceneView is derived from. exposureReset is left at its default (false); main.cpp sets it
    /// from consumeExposureReset() before declaring the frame's passes.
    render::SceneView sceneView();

    /// True exactly once per reset trigger (spec 9): first frame, scene switch, auto-exposure
    /// enable, and resize. Consuming clears the flag, so main.cpp calling this once a frame is
    /// what turns "a reset happened" into "the next frame's SceneView says so."
    bool consumeExposureReset();

    /// Returns the camera currently controlled by the editor viewport.
    const render::Camera& camera() const { return m_camera; }

    /// Whether the frame's render graph may let transients whose lifetimes do not overlap share
    /// memory. Edited by the Render Settings checkbox; the picture is the same either way, so what
    /// it changes is the frame's transient high-water mark and its alias savings.
    bool poolingEnabled() const { return m_settings.poolingEnabled; }

    /// The action intents the main menu raises, for the frame loop to consume at the boundary that
    /// already owns each operation. Menu drawing never quits SDL, waits on the device, or begins a
    /// capture itself, and the keyboard shortcuts raise the same intents, so a menu request and a
    /// key press coalesce into one pending occurrence rather than two.
    ///
    /// Reset Default Layout is the exception the shell consumes itself, at the start of the next
    /// frame -- rebuilding the dock topology mid-submission would tear down nodes the frame is
    /// still drawing into.
    EditorActions& actions() { return m_actions; }

    /// The active scene's display name, for capture tooling. Empty until a scene is loaded.
    std::string_view activeSceneName() const {
        return m_activeScene != nullptr ? m_activeScene->name : std::string_view{};
    }

private:
    EditorShell(SDL_Window* window, engine::SceneLibrary& library);

    // Submitted before the dockspace so the work area the topology is built into already excludes
    // the menu bar. Menu items only read visibility and raise intents.
    void buildMainMenu();
    // Draws every visible panel in dock order and folds each window's close button back into
    // m_workspace.visibility. Panels draw the state this shell owns; they keep no copy of it.
    void buildPanels(rhi::Device& device, render::Renderer& renderer,
                     const FrameRecordRing& frameRecords);
    // Leaves SDL relative mouse mode, restores the cursor, clears the look latch, and discards the
    // motion accumulated while looking, so a later look cannot start with a jump. Safe to call when
    // no look is in progress.
    void endMouseLook();
    // The single write path for panel visibility, shared by the Window menu and by a panel's own
    // close button, and the only place that tells ImGui the ini needs rewriting for a change that
    // moved no window.
    void setPanelVisible(EditorPanel panel, bool visible);
    // device.waitIdle() then library.get(id); on failure, logs and leaves the current scene
    // active (spec §3: "error -> log + keep current scene"). On success, re-points the camera at
    // the new scene's initial pose -- the only per-scene UI state this shell carries. Returns
    // whether the active scene actually changed (false for a reselect of the already-active scene
    // and for a failed load), which buildPanels feeds to sceneSwitchOutcome to decide the
    // selection and filter to store (spec section 5).
    bool selectScene(rhi::Device& device, engine::SceneId id);
    void updateCameraInput(float deltaSeconds);

    SDL_Window* m_window = nullptr;
    engine::SceneLibrary& m_library;
    engine::SceneId m_activeSceneId = engine::defaultSceneId();
    // Non-owning: the library owns every Scene it has built, for the device's lifetime, which
    // outlives this shell. Never null once create() has returned successfully.
    engine::Scene* m_activeScene = nullptr;

    // The Scene panel's single selection and its case-insensitive filter text (spec section 5-6).
    // Editor-local navigation state -- never serialized, never passed to Render or the RHI.
    // Initialized by initialSelection() at create() and updated together via sceneSwitchOutcome()
    // on every requested scene switch, including the failed-switch retain path; healed with
    // resolveSelection() once per frame, before panels draw, so a stale reference from a prior
    // frame never reaches the Inspector.
    EditorSelection m_selection;
    std::string m_sceneFilter;

    render::Camera m_camera;
    std::vector<render::DrawItem> m_drawItems;
    // Render knobs the Inspector writes and Scene::view() reads. Shell state, not scene state --
    // switching scenes does not reset any of them.
    EditorRenderSettings m_settings;
    // Set by create() (first frame), selectScene() (scene switch), the auto-exposure checkbox's
    // off->on transition, and a completed applyPendingViewportResize() (resize) -- each of those
    // four sites decides via shouldResetExposure() (ExposureReset.h) rather than its own inline
    // condition, so the trigger rules live in one pure, unit-tested place. create() always sets it
    // true (m_exposureContext starts with sceneId unset, so the pure function agrees), which is why
    // the default here does not have to. consumeExposureReset() reads and clears it, which is what
    // makes each trigger fire exactly once rather than on every frame the condition still holds.
    bool m_exposureResetPending = false;
    // The state shouldResetExposure() last compared against, updated at each of the four trigger
    // sites after the decision is made. Starts with sceneId unset, which is what makes the very
    // first call at create() read as "first frame" without a separate flag to keep in sync.
    ExposureResetContext m_exposureContext;

    // Viewport panel size in *pixels*. ImGui works in points; the scene target has to be sized in
    // the backing store's units or the image is upscaled on a Retina display, exactly as an
    // unscaled swapchain would be. Zero until the first buildUI and while the panel is collapsed,
    // which is why every consumer checks before using it.
    uint32_t m_viewportWidth = 0;
    uint32_t m_viewportHeight = 0;
    // The size the debounce is counting, and how many consecutive frames it has held.
    uint32_t m_stableWidth = 0;
    uint32_t m_stableHeight = 0;
    uint32_t m_stableFrames = 0;

    bool m_viewportHovered = false;
    // Display-only, shown in the Performance panel. Hover is what gates input, deliberately: a look
    // should start where the cursor is, not where the last click left the focus.
    bool m_viewportFocused = false;
    // True between the right-mouse press that entered relative mouse mode and its release.
    // Latched rather than re-derived each frame: relative mode hides the cursor, so the Viewport
    // window stops reporting itself as hovered for the whole duration of the look.
    bool m_looking = false;
    // Panel visibility plus the settings-handler storage that persists it in imgui.ini. The
    // handler reaches this through a pointer create() installs as its UserData.
    WorkspaceSettings m_workspace;
    // Set at create() when the ini named no matching workspace schema, and again when a layout
    // reset is consumed; cleared by the frame that lays out the dockspace. Rebuilding the default
    // layout on a run whose schema did match would throw away the re-docking the ini exists to
    // persist.
    bool m_buildDefaultLayout = false;
    // Why the pending build was scheduled, for the one line logged when it actually happens.
    std::string_view m_layoutBuildReason;
    // Raised by the main menu and by the keyboard shortcuts, consumed by whoever owns the
    // operation: the frame loop for quit and capture, this shell for a layout reset.
    EditorActions m_actions;

    // The coherent, pausable, clearable performance snapshot behind the Performance panel. Fed one
    // PerformanceFrameSample each buildUI when a GPU frame has newly retired; owns all of the
    // panel's timing, resolution, count, and transient-memory state so the panel itself holds none.
    PerformanceModel m_performanceModel;
};

} // namespace lmx::app
