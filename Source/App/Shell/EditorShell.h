//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.h
/// @brief Declares the docked editor shell and its per-frame interface.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Capture/EditorActions.h"
#include "App/Model/Capture/NoticeQueue.h"
#include "App/Model/Console/ConsoleModel.h"
#include "App/Model/Graph/FrameRecordRing.h"
#include "App/Model/Options/AppOptions.h"
#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/Performance/MetricsContextRevision.h"
#include "App/Model/Performance/PerformanceModel.h"
#include "App/Model/Rendering/Lighting/LightingDisplay.h"
#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Rendering/Settings/ExposureReset.h"
#include "App/Model/Rendering/Temporal/DynamicResolution.h"
#include "App/Model/Rendering/Temporal/TemporalEditorState.h"
#include "App/Model/Rendering/Visibility/VisibilityDisplay.h"
#include "App/Model/Scene/DocumentDialogMailbox.h"
#include "App/Model/Scene/DocumentWorkflow.h"
#include "App/Model/Scene/EditorPlayback.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneLoadState.h"
#include "App/Model/Scene/SceneSession.h"
#include "App/Model/Scene/SceneTreeState.h"
#include "App/Model/Session/DocumentWatch.h"
#include "App/Model/Session/SessionApply.h"
#include "App/Model/Session/SessionEdits.h"
#include "App/Model/Session/SessionListener.h"
#include "App/Model/Session/SessionLog.h"
#include "App/Model/Session/SessionMailbox.h"
#include "App/Model/Session/SessionProposal.h"
#include "App/Model/Workspace/ActivityModel.h"
#include "App/Model/Workspace/MenuModel.h"
#include "App/Model/Workspace/WorkspaceModel.h"
#include "App/Panels/Gallery/StyleGalleryPanel.h"
#include "App/Panels/Graph/RenderGraphPanel.h"
#include "App/Panels/Performance/PerformancePanel.h"
#include "App/Panels/Session/SessionPanel.h"
#include "App/Shell/ChildRun.h"
#include "App/Shell/NativeMenu.h"
#include "Engine/View/Camera.h"
#include "Render/Passes/SelectionOutline/SelectionOutline.h"
#include "Render/Passes/Temporal/ResolutionController.h"
#include "Render/Renderer/Renderer.h"
#include "Scenes/SceneLibrary.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
    /// User-selected UI scale, persisted independently of dock topology.
    uint32_t uiScalePercent = kDefaultUiScalePercent;
    AppearanceState appearance;             ///< Persisted preference and optional session override.
    Density density = Density::Comfortable; ///< Persisted spacing preference.
};

/// The editor shell: the Dear ImGui context, the dockspace and its four docked panels, the detached
/// Performance and Render Graph windows, the fly camera, and the active engine::Scene the Inspector
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
                                               scenes::SceneLibrary& library,
                                               scenes::SceneId initialScene,
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

    /// Saves a menu appearance choice, clearing a session override; applies between frames.
    void setAppearance(Appearance appearance);
    /// Updates the observed system theme; Auto resolves it on the next frame.
    void onSystemThemeChanged(SystemTheme theme);
    /// Seeds a session-only appearance override before the first UI frame.
    void primeAppearance(std::optional<Appearance> appearance);
    /// Returns the current preference, including a session override, for native window themes.
    Appearance effectiveAppearance() const { return m_workspace.appearance.effective(); }
    /// Returns the current UI canvas clear in encoded SDR sRGB, with straight alpha.
    std::array<float, 4> uiClearColor() const;

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
    render::GraphTexture declareSelection(render::RenderGraph& graph,
                                          rojoRHI::CommandList& commands,
                                          render::GraphTexture display,
                                          const render::SceneView& view,
                                          const render::Renderer& renderer);

    /// This frame's scene, valid until the next call -- it spans a draw list this shell owns.
    /// Build the UI first: controls edit the active scene's look, objects and lights that this
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
    const engine::Camera& camera() const { return m_session.camera(); }

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

    /// Executes one model-produced command on the UI thread. Callers enforce menu availability
    /// or shortcut policy; capture shortcuts may request unavailable recovery feedback.
    /// Frame Selected queues an intent consumed in buildUI with that frame's renderer and
    /// selection.
    void runMenuCommand(MenuCommand command, uint32_t argument = 0);

    /// Routes menu, OS Quit and main-window close through the same unsaved-changes workflow.
    /// An outstanding native dialog must answer before this can publish a quit action.
    void requestQuit();
    /// Consumes ready document work and native responses before drawable acquisition, even when
    /// no frame can render. Dirty confirmation remains pending until buildUI can present it.
    void pumpDocuments();
    /// Polls the open document and processes operator Session review clicks before document work.
    void pumpSession(double now);
    /// Uses the session output path while an approved GPU capture owns the shared capture intent.
    std::string captureOutputPath(std::string_view ordinaryPath) const;
    /// Rechecks a queued session trace destination at the acquired-drawable boundary.
    bool sessionCaptureTargetAvailable() const;
    /// Opens the run-local socket after startup or an operator Listen action.
    bool startSessionListener();
    /// Publishes native menu state before SDL polls AppKit events; a no-op outside macOS.
    void updateNativeMenu(const render::Renderer& renderer, const rojoRHI::Device& device);
    /// Drains native actions in order and consumes framing with the supplied renderer.
    /// afterPanels resolves keyboard intents using this frame's widget ownership.
    void consumeNativeMenuCommands(const render::Renderer& renderer, bool afterPanels = false);

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
    EditorShell(SDL_Window* window, scenes::SceneLibrary& library,
                std::shared_ptr<ConsoleLog> consoleLog);

    // Submitted before the dockspace so the work area the topology is built into already excludes
    // the menu bar. Menu items only read visibility and raise intents.
    void buildMainMenu(const render::Renderer& renderer, const rojoRHI::Device& device);
    /// Resolves the reconstruction the renderer runs for the current request: a vendor request
    /// falls back to Native TAA without device support or after the scaler's creation failed.
    render::ReconstructionMode effectiveReconstruction(const render::Renderer& renderer,
                                                       const rojoRHI::Device& device) const;
    MenuContext menuContext(const render::Renderer& renderer, const rojoRHI::Device& device);
    void consumeFrameSelection(const render::Renderer& renderer);
    void resetCamera();
    void frameSelected(const render::Renderer& renderer);
    void updateEditorShortcuts(const render::Renderer& renderer);
    ShortcutContext shortcutContext() const;
    void postCaptureNotice();
    void buildPlaybackTransport();
    void stopPlayback();
    void finishMeasurementPlayback();
    void registerWorkspaceSettings();
    void buildDefaultLayout(uint32_t dockspaceId);
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
    void refreshDocumentDirty(bool force = false);
    void requestDocumentAction(DocumentAction action, std::optional<scenes::SceneId> target = {});
    bool executeDocumentWork(const PendingDocumentWork& work);
    void buildDocumentWorkflow();
    void startDocumentDialog();
    bool saveDocument(const std::filesystem::path& path, bool saveAs);
    void setSceneCamera();
    // Builds a fresh snapshot before discarding the active one; failure retains scene and
    // selection.
    bool selectScene(scenes::SceneId id);
    bool selectSessionScene(scenes::SceneId id);
    bool acceptFileProposal(uint64_t id);
    FileStamp currentDocumentStamp();
    void recordSessionReview(std::string command, std::string arguments, std::string outcome,
                             std::string client = {});
    void drainSessionBridge();
    void runSessionApprovals();
    void finishSessionStep(uint64_t approval, bool ok, SessionError error = SessionError::Failed,
                           std::string message = {});
    std::expected<bool, std::pair<SessionError, std::string>>
    executeSessionStep(const ApprovalStep& step, uint64_t approval);
    std::expected<std::filesystem::path, std::string> sessionOutputPath(std::string_view name,
                                                                        bool exporting = false);
    bool writeSessionFile(const std::filesystem::path& path, std::string_view contents);
    std::expected<std::vector<SessionEvidence>, std::string>
    sessionOutputEvidence(uint64_t approval, bool required);
    void stopSessionWork();
    void cancelAwaitingSessionApprovals(std::string_view reason);
    void updateCameraInput(float deltaSeconds);
    uint64_t metricsContextEpoch();
    bool startMeasurement(rojoRHI::Device& device, const render::Renderer& renderer,
                          bool sessionOwned = false);
    void exportMeasurement();

    SDL_Window* m_window = nullptr;
#ifdef __APPLE__
    std::unique_ptr<NativeMenuBar> m_nativeMenu;
#endif
    scenes::SceneLibrary& m_library;
    scenes::SceneId m_activeSceneId = scenes::defaultSceneId();
    // Borrows the scene owned by m_library and holds its camera. Active after create succeeds.
    SceneSession m_session;
    EditorPlayback m_playback;
    bool m_measurementOwnsPlayback = false;
    // The single selected subject shared by the Scene panel and the Inspector, plus the Scene
    // panel's case-insensitive filter text (spec sections 5-6). Editor-local navigation state --
    // never serialized, never passed to Render or the RHI. Initialized by initialSelection() at
    // create() and updated together via sceneSwitchOutcome() on every requested scene switch,
    // including the failed-switch retain path; healed with resolveSelection() once per frame,
    // before panels draw, so a stale scene id or out-of-range index never reaches the Inspector.
    EditorSelection m_selection;
    std::string m_sceneFilter;
    SceneTreeState m_sceneTree; ///< Hierarchy collapse choices and cached tree.
    SceneLoadState m_sceneLoading;
    DocumentWorkflow m_documentWorkflow;
    std::shared_ptr<DocumentDialogMailbox> m_documentDialog =
        std::make_shared<DocumentDialogMailbox>();
    const engine::Scene* m_dirtyScene = nullptr;
    uint64_t m_dirtyGeneration = 0;
    bool m_documentDirty = false;
    std::string m_documentExportError;
    MetricsContextRevision m_metricsContextRevision;

    std::vector<engine::DrawItem> m_drawItems;
    std::unique_ptr<render::SelectionOutline> m_selectionOutline;
    bool m_showSelectionOutline = true;
    bool m_frameSelectionRequested = false;
    bool m_viewportUsable = false;
    float m_viewportBackingScale = 1.0f;
    // Renderer configuration survives scene switches; authored look values belong to the scene.
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
    // Activation, automatic-exposure enable and resize latch feedback reset until consumption.
    bool m_exposureResetPending = false;
    ExposureResetContext m_exposureContext;
    // Scene-generation counter, camera-cut latch, and TemporalLab's once-only defaults
    // (TemporalEditorState.h). onSceneSelected() is called both by create() (the initial scene) and
    // by selectScene() (every later switch), so the very first frame already reports a real
    // generation rather than 0-as-unset.
    TemporalEditorState m_temporalState;

    // The dynamic-resolution controller (render::ResolutionController.h) and the shell-local state
    // applyDynamicResolution() needs to tell an off->on edge and an already-observed frame apart
    // from one buildUI() to the next (Source/App/Model/Rendering/Temporal/DynamicResolution.h).
    render::ResolutionController m_resolutionController;
    DynamicResolutionState m_dynamicResolutionState;
    std::optional<ScaleChange> m_lastControllerScaleChange;

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
    Density m_appliedDensity = Density::Comfortable;
    SystemTheme m_systemTheme = SystemTheme::Unknown;
    std::optional<ThemeKind> m_appliedTheme;
    ThemeTransition m_themeTransition;
    ThemePalette m_activePalette = kDarkPalette;
    bool m_themeTransitionPending = false;
    // Set at create() when the ini named no matching workspace schema, and again when a layout
    // reset is consumed; cleared by the frame that lays out the dockspace. Rebuilding the default
    // layout on a run whose schema did match would throw away the re-docking the ini exists to
    // persist.
    bool m_buildDefaultLayout = false;
    PerformancePanelState m_performancePanel;
    StyleGalleryPanelState m_styleGallery;
    // Why the pending build was scheduled, for the one line logged when it actually happens.
    std::string_view m_layoutBuildReason;
    // Raised by the main menu and by the keyboard shortcuts, consumed by whoever owns the
    // operation: the frame loop for quit and capture, this shell for a layout reset.
    EditorActions m_actions;
    NoticeQueue m_notices;
    ActionResult m_lastCaptureNotice;

    // The coherent, pausable, clearable performance snapshot behind the Performance panel. Fed one
    // PerformanceFrameSample each buildUI when a GPU frame has newly retired; owns all of the
    // panel's timing, resolution, count, and transient-memory state so the panel itself holds none.
    PerformanceModel m_performanceModel;
    ConsoleModel m_consoleModel;
    ProposalQueue m_sessionProposals;
    SessionAttribution m_sessionAttribution;
    SessionAttribution m_settingAttribution;
    DocumentWatch m_documentWatch;
    DocumentProbe m_documentProbe;
    std::filesystem::path m_watchedPath;
    FileStamp m_loadedStamp;
    FileStamp m_watchedStamp;
    std::string m_watchedHash;
    std::string m_watchReadError;
    std::optional<uint64_t> m_fileAcceptId;
    std::optional<SessionPanelResult> m_sessionPanelAction;
    SessionLog m_sessionLog;
    SessionApprovals m_sessionApprovals;
    ApprovalClickGuard m_sessionApprovalGuard;
    CardListClickGuard m_sessionProposalGuard;
    std::unordered_map<uint64_t, uint64_t> m_sessionApprovalConnections;
    std::unordered_map<uint64_t, std::pair<SessionError, std::string>> m_sessionApprovalFailures;
    std::unordered_map<uint64_t, std::vector<std::string>> m_sessionApprovalOutputs;
    std::unordered_map<uint64_t, std::vector<std::string>> m_sessionStepOutputs;
    std::filesystem::path m_sessionOutputDirectory;
    std::filesystem::path m_sessionOutputName;
    std::optional<uint64_t> m_sessionMeasurementApproval;
    std::optional<uint64_t> m_sessionCaptureApproval;
    std::optional<uint64_t> m_sessionChildApproval;
    std::optional<ChildRun> m_sessionChild;
    std::filesystem::path m_sessionChildLog;
    std::filesystem::path m_sessionChildOutput;
    bool m_sessionChildSequence = false;
    std::optional<uint64_t> m_pendingSessionMeasurementStart;
    uint32_t m_sessionMeasurementWarmup = 0;
    uint32_t m_sessionMeasurementFrames = 0;
    std::filesystem::path m_sessionJobOutput;
    scenes::GeneratorOverrides m_sessionGeneratorOverrides;
    bool m_sessionJobCancelled = false;
    uint32_t m_sessionHzbLevels = 0;
    render::ReconstructionMode m_sessionEffectiveReconstruction =
        render::ReconstructionMode::NativeTaa;
    std::shared_ptr<SessionMailbox> m_sessionMailbox;
    std::unique_ptr<SessionListener> m_sessionListener;
    uint64_t m_sessionConnection = 0;
    bool m_sessionHello = false;
    SessionTier m_sessionTier = SessionTier::ReadOnly;
    std::string m_sessionClient;
    std::string m_sessionRecordClient;
    uint64_t m_sessionExpandedProposal = 0;
    std::string m_sessionPathFeedback;
    SessionDockPlacement m_sessionDockPlacement;

    // The Render Graph canvas's session state: the node-editor context, the shape it is laid out
    // for, and the selected node. Owned here rather than by the panel because the context has to
    // outlive any one draw -- it holds what the user dragged, zoomed, and panned -- and has to be
    // released before ImGui::DestroyContext(), a boundary only this shell sees.
    RenderGraphPanelState m_renderGraphPanel;
};

} // namespace lmx::app
