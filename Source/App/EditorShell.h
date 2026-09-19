//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.h
/// @brief Declares the docked editor shell and its per-frame interface.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/AppOptions.h"
#include "App/Model/ConsoleModel.h"
#include "App/Model/DynamicResolution.h"
#include "App/Model/EditorActions.h"
#include "App/Model/EditorPlayback.h"
#include "App/Model/EditorRenderSettings.h"
#include "App/Model/EditorSelection.h"
#include "App/Model/ExposureReset.h"
#include "App/Model/FrameRecordRing.h"
#include "App/Model/LightingDisplay.h"
#include "App/Model/MeasurementRun.h"
#include "App/Model/MetricsContextRevision.h"
#include "App/Model/PerformanceModel.h"
#include "App/Model/SceneLoadState.h"
#include "App/Model/SceneSession.h"
#include "App/Model/TemporalEditorState.h"
#include "App/Model/VisibilityDisplay.h"
#include "App/Model/WorkspaceModel.h"
#include "App/Panels/PerformancePanel.h"
#include "App/Panels/RenderGraphPanel.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"
#include "Render/ResolutionController.h"
#include "Render/SelectionOutline.h"
#include "Scene/SceneLibrary.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/// SDL is an implementation detail of shell input; this header uses an opaque window handle.
struct SDL_Window;
/// ImGui style storage is private to the shell; its definition stays in the implementation.
struct ImGuiStyle;

namespace lmx::app {

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
    /// User-selected UI density, persisted independently of dock topology.
    uint32_t uiScalePercent = kDefaultUiScalePercent;
};

/// The editor shell: the Dear ImGui context, the dockspace and its four docked panels, the detached
/// Performance and Render Graph windows, the fly camera, and the active scene::Scene the Inspector
/// edits. One per process -- ImGui's context, and the Metal 4 renderer glue behind it, are both
/// process-global -- which is why this is created through a factory and is neither copyable nor
/// movable.
///
/// It owns selection presentation resources but no Scene. The SceneLibrary (which owns every Scene
/// it has built, for the device's lifetime) is passed in and outlives the shell; everything else
/// the shell touches (the scene target it displays, the device it resizes against) is passed in per
/// call, which keeps the destruction order the RHI requires visible in run(): the shell is declared
/// last and so torn down first, while the device is still alive for imguiShutdown() to drain
/// against.
class EditorShell {
public:
    /// Creates the ImGui context and both backends (order per Metal4ImGui.h), then loads the
    /// requested initial scene and points the camera at its initial pose. Returns nullptr after
    /// logging, with nothing left initialised, if either ImGui backend refuses or the initial scene
    /// fails to load --
    /// both are startup-fatal, unlike a later scene switch (the Scene panel -> selectScene), which
    /// logs and keeps the previous scene active instead.
    static std::unique_ptr<EditorShell> create(SDL_Window* window, rojoRHI::Device& device,
                                               scene::SceneLibrary& library,
                                               scene::SceneId initialScene,
                                               std::shared_ptr<ConsoleLog> consoleLog);
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
    /// False on renderer allocation failure: the caller must leave the frame loop before drawing.
    bool applyPendingViewportResize(rojoRHI::Device& device, render::Renderer& renderer);

    /// Applies queued font/control scaling before backend and ImGui NewFrame calls.
    /// Uses the unscaled base style so repeated zoom/reset operations cannot accumulate drift.
    void prepareUIFrame();

    /// Builds the whole UI for this frame and applies camera input. Between ImGui::NewFrame() and
    /// ImGui::Render(). Applies a scene request from the preceding presented frame before drawing:
    /// the device drains in-flight references before the library builds or returns the scene.
    ///
    /// `frameRecords` feeds both observability views: the Render Graph panel shows one exact
    /// retired frame, while Performance rolls timings from successive retired frames into a stable
    /// summary. At this point the frame loop has not retained the current frame, so both see the
    /// newest joined record as of the previous iteration.
    ///
    /// Heals the Scene panel's selection against the active scene before any panel draws (spec
    /// section 5): a stale scene id or out-of-range index resolves to None and the healed value is
    /// what the Inspector sees this frame.
    void buildUI(rojoRHI::Device& device, render::Renderer& renderer, float deltaSeconds,
                 const FrameRecordRing& frameRecords);

    /// Seeds rendering and local-light startup options before the first frame; returns a rig
    /// activation error without starting the frame loop if the requested rig cannot be added.
    rojoRHI::Result<void> primeTemporal(const AppOptions& options);

    /// Appends editor-only selection presentation; returns scene display unchanged without a cue.
    render::GraphTexture declareSelection(render::RenderGraph& graph, rojoRHI::CommandList& commands,
                                          render::GraphTexture display,
                                          const render::SceneView& view,
                                          const render::Renderer& renderer);

    /// This frame's scene, valid until the next call -- it spans a draw list this shell owns.
    /// Build the UI first: the Inspector edits the active scene's objects and lights that this
    /// SceneView is derived from. exposureReset is left at its default (false); main.cpp sets it
    /// from consumeExposureReset() before declaring the frame's passes.
    render::SceneView sceneView();

    /// Uploads active scene tables after frame pacing and animation, before obtaining the view.
    rojoRHI::Result<void> prepareSceneFrame(uint64_t frameNumber);

    /// True exactly once per reset trigger (spec 9): first frame, scene switch, auto-exposure
    /// enable, and resize. Consuming clears the flag, so main.cpp calling this once a frame is
    /// what turns "a reset happened" into "the next frame's SceneView says so."
    bool consumeExposureReset();

    /// Advances the active scene's animation clock and camera-track follow for the frame about to
    /// be declared. Call once per frame, after buildUI and before declarePasses, and only when the
    /// frame will actually be declared (drawable acquired) -- a skipped frame calls neither this
    /// nor commitFrame().
    ///
    /// When the top transport is Playing and the active scene has rigid or
    /// camera tracks, steps `Scene::animationTime` by a fixed 1/60 s and resamples every track at
    /// the new time (`Scene::advanceAnimation` + `Scene::animate`). Independently, when
    /// the preview is active, `followCameraTrack` is set, the scene has a camera track, and the
    /// user is not mid fly-camera look (holding the right mouse button), overwrites the fly
    /// camera's position/yaw/pitch from `sampleCameraTrack` at the (possibly just-advanced)
    /// animation time -- fovY/nearZ/farZ are left alone, since the track carries no lens state.
    void advanceFrameAnimation();

    /// Promotes the active scene's motion to "previous" for next frame's reprojection
    /// (`Scene::commitFrame()`). Call once per frame, after the frame's render graph has executed
    /// successfully; a skipped frame calls neither this nor advanceFrameAnimation().
    void commitFrame();

    /// Records that `frame` is being declared at the dynamic-resolution controller's current
    /// scale, while the controller is active -- dynamic resolution and the temporal path both on.
    /// Skipped otherwise: with either off the frame does not run at a scale the controller chose,
    /// so attributing it to one would judge the controller by a picture it never asked for. Call
    /// once per frame, after `device.beginFrame()`, with the device's own frame number.
    void controllerDeclared(uint64_t frame);

    /// Retains the current declaration's temporal event and effective status for editor observers.
    void observeDeclaration(const render::Renderer& renderer, uint64_t frameId);

    /// Captures declaration-time dimensions and counts for the exact frame retained by the loop.
    FrameMetricsMetadata frameMetrics(const render::Renderer& renderer);

    /// Joins exact retired GPU timings to a pending interactive measurement.
    void retireMeasurement(uint64_t frameId, std::span<const rojoRHI::PassTiming> timings);
    /// Joins the exact retired local-light diagnostics to their declared measurement frame.
    void retireMeasurementLighting(const render::LightingStatus& status);
    /// Publishes queued GPU visibility using saved declaration identities and exact measurement
    /// joins.
    void retireVisibility(render::Renderer& renderer);
    /// Consumes completed lighting frames once for coherent readings and immediate check warnings.
    void retireLighting(render::Renderer& renderer);
    /// Records the just-submitted frame using its exact declaration and CPU timing scopes.
    void recordMeasurementFrame(uint64_t frameId, double waitMs, double encodeMs,
                                const render::CompiledFrameRecord& record);
    /// Measurement frames serialize retirement to preserve every GPU sample.
    bool measurementNeedsRetirementWait() const;

    /// Returns the camera currently controlled by the editor viewport.
    const render::Camera& camera() const { return m_session.camera(); }

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
        return m_session.activeScene() != nullptr ? m_session.scene().name : std::string_view{};
    }

    /// Seeds dynamic resolution ahead of the frame loop, for automation that needs it on without
    /// an Inspector toggle (`LMX_DYNAMIC_RESOLUTION_BUDGET_MS`). Only meaningful before the first
    /// `buildUI()` call -- afterward the Inspector checkbox and slider own both fields.
    void primeDynamicResolution(bool enabled, float gpuBudgetMilliseconds) {
        m_settings.dynamicResolutionEnabled = enabled;
        m_settings.gpuBudgetMilliseconds = gpuBudgetMilliseconds;
    }

private:
    EditorShell(SDL_Window* window, scene::SceneLibrary& library,
                std::shared_ptr<ConsoleLog> consoleLog);

    // Submitted before the dockspace so the work area the topology is built into already excludes
    // the menu bar. Menu items only read visibility and raise intents.
    void buildMainMenu();
    void buildPlaybackTransport(rojoRHI::Device& device, const render::Renderer& renderer);
    void showMeasurement();
    void stopPlayback();
    void finishMeasurementPlayback();
    // Queues a bounded UI-density preference for the next frame and persistence.
    void setUiScale(uint32_t percent);
    // Global shortcuts exclude text editing, active widgets, popups and camera look.
    void updateUiScaleShortcuts();
    // Draws every visible panel in dock order and folds each window's close button back into
    // m_workspace.visibility. Panels draw the state this shell owns; they keep no copy of it.
    void buildPanels(rojoRHI::Device& device, render::Renderer& renderer,
                     const FrameRecordRing& frameRecords);
    // Leaves SDL relative mouse mode, restores the cursor, clears the look latch, and discards the
    // motion accumulated while looking, so a later look cannot start with a jump. Safe to call when
    // no look is in progress.
    void endMouseLook();
    // The single write path for panel visibility, shared by the Window menu and by a panel's own
    // close button, and the only place that tells ImGui the ini needs rewriting for a change that
    // moved no window.
    void setPanelVisible(EditorPanel panel, bool visible);
    // Consumes the preceding presented frame's request, preserving selection/filter on failure.
    void applyPendingScene(rojoRHI::Device& device);
    // Drains in-flight scene references and loads one requested catalog entry, retaining an
    // actionable failure for ScenePanel while the current scene remains renderable.
    bool selectScene(rojoRHI::Device& device, scene::SceneId id);
    void updateCameraInput(float deltaSeconds);
    uint64_t metricsContextEpoch();
    void startMeasurement(rojoRHI::Device& device, const render::Renderer& renderer);
    void exportMeasurement();

    SDL_Window* m_window = nullptr;
    scene::SceneLibrary& m_library;
    scene::SceneId m_activeSceneId = scene::defaultSceneId();
    // Borrows the scene owned by m_library and holds its camera. Active after create succeeds.
    SceneSession m_session;
    EditorPlayback m_playback;
    bool m_measureOnPlay = false;
    bool m_measurementOwnsPlayback = false;
    bool m_revealMeasurement = false;
    // The single selected subject shared by the Scene panel and the Inspector, plus the Scene
    // panel's case-insensitive filter text (spec sections 5-6). Editor-local navigation state --
    // never serialized, never passed to Render or the RHI. Initialized by initialSelection() at
    // create() and updated together via sceneSwitchOutcome() on every requested scene switch,
    // including the failed-switch retain path; healed with resolveSelection() once per frame,
    // before panels draw, so a stale scene id or out-of-range index never reaches the Inspector.
    EditorSelection m_selection;
    std::string m_sceneFilter;
    SceneLoadState m_sceneLoading;
    MetricsContextRevision m_metricsContextRevision;

    std::vector<render::DrawItem> m_drawItems;
    std::unique_ptr<render::SelectionOutline> m_selectionOutline;
    bool m_showSelectionOutline = true;
    bool m_viewportUsable = false;
    float m_viewportBackingScale = 1.0f;
    // Render knobs the Inspector writes and Scene::view() reads. Shell state, not scene state --
    // switching scenes does not reset any of them.
    EditorRenderSettings m_settings;
    VisibilityDisplay m_visibilityDisplay;
    LightingDisplay m_lightingDisplay;
    bool m_lightingFailureLogged = false;
    bool m_visibilityFailureLogged = false;
    MeasurementRun m_measurement;
    uint32_t m_measurementWarmup = 32;
    uint32_t m_measurementFrames = 256;
    std::string m_measurementExportPath;
    std::string m_measurementFeedback;
    render::VisibilityStatus m_measurementVisibility;
    render::LightingStatus m_measurementLighting;
    render::TemporalStatus m_measurementTemporal;
    uint32_t m_labInstances = 4096;
    uint32_t m_labOccluders = 0;
    uint32_t m_labLights = 256;
    uint32_t m_labLightPile = 0;
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
    // Scene-generation counter, camera-cut latch, and TemporalLab's once-only defaults
    // (TemporalEditorState.h). onSceneSelected() is called both by create() (the initial scene) and
    // by selectScene() (every later switch), so the very first frame already reports a real
    // generation rather than 0-as-unset.
    TemporalEditorState m_temporalState;

    // The dynamic-resolution controller (render::ResolutionController.h) and the shell-local state
    // applyDynamicResolution() needs to tell an off->on edge and an already-observed frame apart
    // from one buildUI() to the next (Source/App/Model/DynamicResolution.h).
    render::ResolutionController m_resolutionController;
    DynamicResolutionState m_dynamicResolutionState;

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
    std::unique_ptr<ImGuiStyle> m_baseUiStyle;
    uint32_t m_appliedUiScalePercent = 0;
    // Set at create() when the ini named no matching workspace schema, and again when a layout
    // reset is consumed; cleared by the frame that lays out the dockspace. Rebuilding the default
    // layout on a run whose schema did match would throw away the re-docking the ini exists to
    // persist.
    bool m_buildDefaultLayout = false;
    PerformancePanelState m_performancePanel;
    // Why the pending build was scheduled, for the one line logged when it actually happens.
    std::string_view m_layoutBuildReason;
    // Raised by the main menu and by the keyboard shortcuts, consumed by whoever owns the
    // operation: the frame loop for quit and capture, this shell for a layout reset.
    EditorActions m_actions;

    // The coherent, pausable, clearable performance snapshot behind the Performance panel. Fed one
    // PerformanceFrameSample each buildUI when a GPU frame has newly retired; owns all of the
    // panel's timing, resolution, count, and transient-memory state so the panel itself holds none.
    PerformanceModel m_performanceModel;
    ConsoleModel m_consoleModel;

    // The Render Graph canvas's session state: the node-editor context, the shape it is laid out
    // for, and the selected node. Owned here rather than by the panel because the context has to
    // outlive any one draw -- it holds what the user dragged, zoomed, and panned -- and has to be
    // released before ImGui::DestroyContext(), a boundary only this shell sees.
    RenderGraphPanelState m_renderGraphPanel;
};

} // namespace lmx::app
