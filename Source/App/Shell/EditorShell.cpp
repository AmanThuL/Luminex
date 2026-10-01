//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.cpp
/// @brief Implements the docked editor UI, input, and scene interaction.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Shell/AppAppearance.h"
#include "App/Shell/EditorFont.h"
#include "App/Shell/EditorThemeApply.h"

#include "App/Panels/Console/ConsolePanel.h"
#include "App/Panels/Graph/RenderGraphPanel.h"
#include "App/Panels/Inspector/InspectorPanel.h"
#include "App/Panels/Performance/PerformancePanel.h"
#include "App/Panels/Rendering/RenderingPanel.h"
#include "App/Panels/Scene/ScenePanel.h"
#include "App/Panels/Shared/ActionFeedback.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "App/Panels/Viewport/ViewportPanel.h"
#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include <rojoRHI/Metal4/Metal4ImGui.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// MarkIniSettingsDirty is an internal ImGui API used when resetting the workspace.
#include <imgui_internal.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

namespace {

// Debounce resize-driven GPU stalls until the dock splitter settles.
constexpr uint32_t kResizeDebounceFrames = 10;

// The section body ImGui hands back never includes its own header line, so the schema decision is
// reached through the same text writeWorkspaceSettings emits.
constexpr std::string_view kNoSchemaReason = "no matching workspace schema in imgui.ini";
constexpr std::string_view kMigrationReason =
    "migrating workspace schema 3 to 5; keeping panel visibility, UI scale and detached window "
    "bounds";
constexpr std::string_view kResetReason = "layout reset requested";

} // namespace

//======================================================================================================================
EditorShell::EditorShell(SDL_Window* window, scenes::SceneLibrary& library,
                         std::shared_ptr<ConsoleLog> consoleLog)
    : m_window(window), m_library(library), m_consoleModel(std::move(consoleLog)) {}

//======================================================================================================================
std::unique_ptr<EditorShell> EditorShell::create(SDL_Window* window, rojoRHI::Device& device,
                                                 scenes::SceneLibrary& library,
                                                 scenes::SceneId initialScene,
                                                 std::shared_ptr<ConsoleLog> consoleLog) {
    LMX_ASSERT(window != nullptr, "EditorShell::create: window must not be null");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    verifyImGuiSlotNames();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Platform viewports draw windows the RHI's single swapchain knows nothing about: the vendored
    // Metal 4 ImGui backend creates a CAMetalLayer per extra window and renders it with its own
    // command buffer on the shared device queue. main.cpp drives them after each presented frame.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    editor_style::setEditorFonts(configureEditorFonts());

    if (!ImGui_ImplSDL3_InitForMetal(window)) {
        LMX_LOG_ERROR("ImGui_ImplSDL3_InitForMetal failed: {}", SDL_GetError());
        editor_style::setEditorFonts({});
        ImGui::DestroyContext();
        return nullptr;
    }
    // ImGui's pipeline format must match the swapchain drawable.
    if (!rojoRHI::metal4::imguiInit(device, rojoRHI::Format::BGRA8Unorm)) {
        ImGui_ImplSDL3_Shutdown();
        editor_style::setEditorFonts({});
        ImGui::DestroyContext();
        return nullptr;
    }

    std::unique_ptr<EditorShell> self(new EditorShell(window, library, std::move(consoleLog)));
    self->m_baseUiStyle = std::make_unique<ImGuiStyle>(ImGui::GetStyle());

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    auto outline = render::SelectionOutline::create(device, static_cast<uint32_t>(pixelWidth),
                                                    static_cast<uint32_t>(pixelHeight));
    if (!outline) {
        LMX_LOG_ERROR("Selection presentation creation failed: {}", outline.error().message);
        return nullptr;
    }
    self->m_selectionOutline = std::move(*outline);

    // Startup needs a renderable scene; later switch failures can retain the current one. This runs
    // before any ini is loaded: LoadIniSettingsFromDisk (below) sets ImGui's SettingsLoaded flag,
    // and once that is set, ~EditorShell's DestroyContext() saves imgui.ini on its way out -- so a
    // failure return here, before that flag is ever touched, is what keeps a failed startup from
    // stamping a fresh Schema= onto legacy dock data and defeating the migration on the next
    // successful launch.
    auto scene = library.get(initialScene);
    if (!scene) {
        LMX_LOG_ERROR("EditorShell::create: initial scene '{}' failed to load: {}",
                      library.entry(initialScene).displayName, scene.error().message);
        releaseRenderGraphPanelState(self->m_renderGraphPanel);
        ImGui_ImplSDL3_Shutdown();
        rojoRHI::metal4::imguiShutdown();
        editor_style::setEditorFonts({});
        ImGui::DestroyContext();
        return nullptr;
    }
    self->m_activeSceneId = initialScene;
    self->m_session.activate(*library.loaded(initialScene), SceneActivationMotion::Reset);
    SDL_SetWindowTitle(window, (self->m_session.scene().name + " — Luminex").c_str());
    // Startup selects the scene's Camera (spec section 5); every scene provides one.
    self->m_selection = initialSelection(initialScene);
    // The startup scene is a selection like any other (spec 9): the generation counter bumps from
    // its 0-as-unset start, motion has nothing to report yet.
    onSceneSelected(self->m_temporalState, self->m_settings, initialScene);

    activateExposureLook(self->m_exposureContext, self->m_exposureResetPending, initialScene,
                         self->m_session.look());

    // Register before any settings are read so Luminex's section is routed to this handler, and
    // read the ini here rather than letting the first NewFrame() do it: the schema decision below
    // has to be settled before a frame can lay out a dockspace.
    self->registerWorkspaceSettings();
    if (io.IniFilename != nullptr) {
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }

    const std::optional<ParsedWorkspaceSettings> parsed =
        self->m_workspace.sectionSeen ? std::optional<ParsedWorkspaceSettings>(
                                            parseWorkspaceSettings(self->m_workspace.sectionText))
                                      : std::nullopt;
    const WorkspaceDecision decision = decideWorkspace(parsed);
    self->m_workspace.visibility = decision.visibility;
    self->m_workspace.uiScalePercent = decision.uiScalePercent;
    self->m_workspace.appearance.persisted = decision.appearance;
    self->m_workspace.density = decision.density;
    self->m_buildDefaultLayout = decision.kind == WorkspaceDecisionKind::BuildDefault;
    self->m_performancePanel.resetPlacement = decision.resetPerformancePlacement;
    // Schema 3 is the one known migration: it rebuilds docking but keeps the stored preferences,
    // so it logs its own reason rather than the generic no-match recovery.
    const bool migrating = self->m_buildDefaultLayout && parsed.has_value() &&
                           parsed->schemaState == WorkspaceSchemaState::Present &&
                           parsed->schemaVersion == 3;
    self->m_layoutBuildReason = migrating ? kMigrationReason : kNoSchemaReason;

    std::string_view startup =
        "workspace schema matches -- restoring the docked layout from imgui.ini";
    if (migrating) {
        startup = "workspace schema 3 found -- migrating to schema 5 and building the default "
                  "layout";
    } else if (self->m_buildDefaultLayout) {
        startup = "no matching workspace schema -- the default layout will be built";
    }
    LMX_LOG_INFO("editor shell: {} (scene '{}', {} objects)", startup, self->m_session.scene().name,
                 self->m_session.scene().objects.size());
#ifdef __APPLE__
    self->m_nativeMenu = NativeMenuBar::install();
#endif
    return self;
}

//======================================================================================================================
EditorShell::~EditorShell() {
#ifdef __APPLE__
    m_nativeMenu.reset();
#endif
    // Shutting down while relative mouse mode is still on would leave the user's cursor hidden and
    // captured with no window left to release it.
    endMouseLook();
    // The node-editor context unregisters from the ImGui context too, so it goes before both
    // backends and, like them, well before DestroyContext().
    releaseRenderGraphPanelState(m_renderGraphPanel);
    // Backends unregister from the ImGui context, so destroy the context last.
    ImGui_ImplSDL3_Shutdown();
    rojoRHI::metal4::imguiShutdown();
    editor_style::setEditorFonts({});
    ImGui::DestroyContext();
}

//======================================================================================================================
bool EditorShell::applyPendingViewportResize(rojoRHI::Device& device, render::Renderer& renderer) {
    if (m_viewportWidth == 0 || m_viewportHeight == 0) {
        return true;
    }
    if (m_viewportWidth == renderer.width() && m_viewportHeight == renderer.height()) {
        return true;
    }
    if (m_stableFrames < kResizeDebounceFrames) {
        return true;
    }

    // In-flight encoders and residency sets retain the old targets; drain before replacement.
    device.waitIdle();
    // Remove the old target from ImGui's persistent residency set before freeing it.
    rojoRHI::metal4::imguiForgetTexture(renderer.colorTarget());
    if (auto resized = renderer.resize(m_viewportWidth, m_viewportHeight); !resized) {
        // Renderer::resize can replace some targets before a later allocation fails. Do not
        // declare another frame against mixed extents, even if the requested size now matches.
        LMX_LOG_ERROR("viewport resize to {}x{} failed; stopping rendering: {}", m_viewportWidth,
                      m_viewportHeight, resized.error().message);
        return false;
    }
    rojoRHI::metal4::imguiForgetTexture(m_selectionOutline->target());
    if (auto result = m_selectionOutline->resize(renderer.width(), renderer.height()); !result) {
        LMX_LOG_ERROR("Selection presentation resize failed: {}", result.error().message);
        m_showSelectionOutline = false;
    }
    // A resize is a reset trigger (spec 9): the histogram's binning covered a differently-sized
    // image last frame, so the feedback loop restarts from the manual EV.
    ExposureResetContext candidate = m_exposureContext;
    candidate.width = m_viewportWidth;
    candidate.height = m_viewportHeight;
    if (shouldResetExposure(m_exposureContext, candidate)) {
        m_exposureResetPending = true;
    }
    m_exposureContext = candidate;
    LMX_LOG_INFO("scene target resized to {}x{} px", m_viewportWidth, m_viewportHeight);
    return true;
}

//======================================================================================================================
void EditorShell::prepareUIFrame() {
    const double now = static_cast<double>(SDL_GetTicksNS()) / 1.0e9;
    const auto target = resolveTheme(m_workspace.appearance.effective(), m_systemTheme);
    const bool themeChanged = !m_appliedTheme || *m_appliedTheme != target;
    const bool motionReduced = reduceMotion();
    if (themeChanged || (m_themeTransitionPending && motionReduced)) {
        const bool firstFrame = !m_appliedTheme;
        m_themeTransition.start(m_themeTransition.sample(now), themePalette(target), now,
                                firstFrame || motionReduced);
        m_appliedTheme = target;
        m_themeTransitionPending = true;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    if (m_themeTransitionPending) {
        m_activePalette = m_themeTransition.sample(now);
        applyImGuiColors(*m_baseUiStyle, m_activePalette);
        applyImGuiColors(style, m_activePalette);
        editor_style::setActivePalette(m_activePalette);
        m_themeTransitionPending = m_themeTransition.active(now);
    }

    const uint32_t percent = m_workspace.uiScalePercent;
    if (m_appliedUiScalePercent == percent && m_appliedDensity == m_workspace.density)
        return;
    const auto metrics = densityMetrics(m_workspace.density);
    m_baseUiStyle->FrameRounding = kShape.control;
    m_baseUiStyle->GrabRounding = kShape.control;
    m_baseUiStyle->TabRounding = kShape.control;
    m_baseUiStyle->ScrollbarRounding = kShape.control;
    m_baseUiStyle->PopupRounding = kShape.popup;
    m_baseUiStyle->WindowRounding = 0.0f;
    m_baseUiStyle->ChildRounding = 0.0f;
    m_baseUiStyle->WindowBorderSize = kShape.border;
    m_baseUiStyle->ChildBorderSize = kShape.border;
    m_baseUiStyle->PopupBorderSize = kShape.border;
    m_baseUiStyle->FrameBorderSize = kShape.border;
    m_baseUiStyle->ImageBorderSize = kShape.border;
    m_baseUiStyle->TabBorderSize = kShape.border;
    m_baseUiStyle->TabBarBorderSize = kShape.border;
    m_baseUiStyle->DragDropTargetBorderSize = kShape.border;
    m_baseUiStyle->SeparatorTextBorderSize = kShape.border;
    m_baseUiStyle->DockingSeparatorSize = kShape.dockGutter;
    m_baseUiStyle->TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    m_baseUiStyle->FramePadding = {metrics.framePaddingX, metrics.framePaddingY};
    m_baseUiStyle->ItemSpacing = {metrics.itemSpacingX, metrics.itemSpacingY};
    m_baseUiStyle->WindowPadding = {metrics.windowPadding, metrics.windowPadding};
    const float dpiScale = style.FontScaleDpi;
    style = *m_baseUiStyle;
    const float scale = static_cast<float>(percent) / 100.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
    style.FontScaleDpi = dpiScale;
    // ScaleAllSizes truncates integer metrics. Keep thin borders and the software cursor visible
    // at compact scales, while always deriving them from the same unscaled base.
    style.WindowBorderSize = m_baseUiStyle->WindowBorderSize;
    style.ChildBorderSize = m_baseUiStyle->ChildBorderSize;
    style.PopupBorderSize = m_baseUiStyle->PopupBorderSize;
    style.FrameBorderSize = m_baseUiStyle->FrameBorderSize;
    style.ImageBorderSize = m_baseUiStyle->ImageBorderSize;
    style.TabBorderSize = m_baseUiStyle->TabBorderSize;
    style.TabBarBorderSize = m_baseUiStyle->TabBarBorderSize;
    style.DragDropTargetBorderSize = m_baseUiStyle->DragDropTargetBorderSize;
    style.SeparatorTextBorderSize = m_baseUiStyle->SeparatorTextBorderSize;
    style.MouseCursorScale = m_baseUiStyle->MouseCursorScale * scale;
    m_appliedUiScalePercent = percent;
    m_appliedDensity = m_workspace.density;
    LMX_LOG_INFO("editor UI scale: {}%", percent);
}

//======================================================================================================================
void EditorShell::buildUI(rojoRHI::Device& device, render::Renderer& renderer, float deltaSeconds,
                          const FrameRecordRing& frameRecords) {
    refreshDocumentDirty();
    const RetainedFrame* newestTimed = frameRecords.newestTimedFrame();
    observeRetiredTemporal(m_temporalState, newestTimed);
    std::optional<PerformanceFrameSample> sample;
    m_performanceModel.setContextEpoch(metricsContextEpoch());
    if (newestTimed != nullptr && newestTimed->metrics) {
        const FrameMetricsMetadata& metrics = *newestTimed->metrics;
        sample = PerformanceFrameSample{
            .renderingTimings = {.classifyMilliseconds = metrics.classifyMilliseconds,
                                 .prepareMilliseconds = metrics.prepareMilliseconds},
            .frameId = newestTimed->record.frameId,
            .contextEpoch = metrics.contextEpoch,
            .timings = newestTimed->timings,
            .objectCount = metrics.objectCount,
            .drawCount = metrics.drawCount,
            .viewportLogicalWidth = metrics.viewportLogicalWidth,
            .viewportLogicalHeight = metrics.viewportLogicalHeight,
            .sceneTargetPixelWidth = metrics.outputPixelWidth,
            .sceneTargetPixelHeight = metrics.outputPixelHeight,
            .renderPixelWidth = metrics.renderPixelWidth,
            .renderPixelHeight = metrics.renderPixelHeight,
            .transientRequestedBytes = newestTimed->record.debug.memory.requested,
            .transientHighWaterBytes = newestTimed->record.debug.memory.highWater,
            .transientAliasSavingsBytes = newestTimed->record.debug.memory.aliasSavings,
        };
    }
    const float renderScaleBeforeDynamicResolution = m_settings.renderScale;
    applyDynamicResolution(m_dynamicResolutionState, m_resolutionController, m_settings,
                           newestTimed);
    if (dynamicResolutionActive(m_settings) &&
        m_settings.renderScale != renderScaleBeforeDynamicResolution) {
        m_lastControllerScaleChange = ScaleChange{renderScaleBeforeDynamicResolution,
                                                  m_settings.renderScale, ImGui::GetTime()};
        LMX_LOG_INFO("render scale {:.2f} -> {:.2f} after {:.2f} ms",
                     renderScaleBeforeDynamicResolution, m_settings.renderScale,
                     m_dynamicResolutionState.lastObservedMilliseconds);
    }

    if (sample) {
        const auto presentation = temporalPresentation(
            m_temporalState, m_settings, renderer.temporalStatus(),
            device.capabilities().temporalScaler, renderer.width(), renderer.height());
        if (m_temporalState.liveTimedPassSumMilliseconds && !presentation.waitingForDeclaration)
            sample->renderingTimings.compatibleGpu =
                PerformanceTimingReading{m_temporalState.liveMeasurementFrame,
                                         *m_temporalState.liveTimedPassSumMilliseconds};
        if (m_dynamicResolutionState.lastMeasurementFrame != 0)
            sample->renderingTimings.controllerInput =
                PerformanceTimingReading{m_dynamicResolutionState.lastMeasurementFrame,
                                         m_dynamicResolutionState.lastObservedMilliseconds};
    }
    m_performanceModel.tick(deltaSeconds, sample ? &*sample : nullptr);

    // Healed before any panel draws (spec section 5): a stale scene id or out-of-range index from
    // a prior frame resolves to None here, so the Inspector never sees an invalid reference.
    m_selection = resolveSelection(m_selection, m_activeSceneId, m_session.scene());

    // The application window losing OS focus must end a look in progress (spec section 8): the
    // relative-mode cursor and whatever keys are still latched down are no longer this app's to
    // interpret. ImGui's SDL3 backend turns SDL_EVENT_WINDOW_FOCUS_LOST into one frame of
    // io.AppFocusLost, which NewFrame() has already queued by the time this runs.
    if (ImGui::GetIO().AppFocusLost) {
        endMouseLook();
    }

    // Consumed here, at the frame boundary before anything is submitted: rebuilding the topology
    // partway through a frame would remove dock nodes that frame's windows are still drawing into.
    if (m_actions.consumeResetLayout()) {
        // Reset moves every panel out from under the cursor, so a look in progress ends with it.
        endMouseLook();
        m_workspace.visibility = resetWorkspaceVisibility();
        ImGui::MarkIniSettingsDirty();
        m_buildDefaultLayout = true;
        m_performancePanel.resetPlacement = true;
        m_layoutBuildReason = kResetReason;
    }

    updateUiScaleShortcuts();

    // Before the dockspace, so the work area the topology is built into excludes the menu bar.
    finishMeasurementPlayback();
    buildMainMenu(renderer, device);
    consumeFrameSelection(renderer);

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_buildDefaultLayout) {
        m_buildDefaultLayout = false;
        buildDefaultLayout(dockspaceId);
        LMX_LOG_INFO("editor workspace: built the default panel layout ({})", m_layoutBuildReason);
    }

    ImGui::BeginDisabled(m_documentWorkflow.step() != WorkflowStep::Idle);
    buildPanels(device, renderer, frameRecords);
    ImGui::EndDisabled();
    // Input consumes this frame's hover state and Inspector edits.
    updateCameraInput(deltaSeconds);
    updateEditorShortcuts(renderer);
    if (const auto reason =
            reconcileDebugView(m_settings, effectiveReconstruction(renderer, device)))
        m_notices.post({ActionStatus::Unavailable, *reason, {}}, ImGui::GetTime());
    refreshDocumentDirty();
    buildDocumentWorkflow();
    postCaptureNotice();
    editor_style::drawNotice(m_notices, ImGui::GetTime());
    drawStyleGalleryPanel(m_styleGallery);
    updateNativeMenu(renderer, device);
    consumeNativeMenuCommands(renderer, true);
}

//======================================================================================================================
void EditorShell::buildPanels(rojoRHI::Device& device, render::Renderer& renderer,
                              const FrameRecordRing& frameRecords) {
    m_visibilityDisplay.publishReadings(ImGui::GetTime());
    m_lightingDisplay.publishReadings(ImGui::GetTime());
    ImGui::BeginDisabled(m_measurement.active());
    // Every panel is drawn only while visible, and hands its window close button back through the
    // same storage the Window menu writes, so the two can never disagree.
    std::optional<bool> documentSelectionHidden;
    if (m_workspace.visibility.isVisible(EditorPanel::Scene)) {
        bool open = true;
        bool frameSelectionRequested = false;
        documentSelectionHidden = drawScenePanel(
            open, ScenePanelContext{.activeSceneId = m_activeSceneId,
                                    .activeScene = m_session.scene(),
                                    .selection = m_selection,
                                    .filter = m_sceneFilter,
                                    .frameSelectionRequested = frameSelectionRequested,
                                    .visibilityDisplay = m_visibilityDisplay,
                                    .visibilityStatus = m_visibilityDisplay.status(),
                                    .sceneGeneration = m_temporalState.sceneGeneration,
                                    .loadedScene = m_session.loadedScene(),
                                    .session = &m_session,
                                    .dirty = m_documentDirty,
                                    .treeState = m_sceneTree});
        if (frameSelectionRequested)
            frameSelected(renderer);
        setPanelVisible(EditorPanel::Scene, open);
    }

    // Whether the Viewport is both visible and expanded this frame -- the condition under which a
    // look in progress may continue (spec section 8: closing or collapsing it must end one).
    bool viewportUsable = false;
    if (m_workspace.visibility.isVisible(EditorPanel::Viewport)) {
        bool open = true;
        const ViewportPanelResult result = drawViewportPanel(
            open, ViewportPanelContext{.renderer = renderer,
                                       .outlineTarget = m_selectionOutline->target(),
                                       .showOutline = m_showSelectionOutline,
                                       .scene = m_session.scene(),
                                       .settings = m_settings,
                                       .temporalState = m_temporalState,
                                       .selection = m_selection,
                                       .effectiveReconstruction =
                                           effectiveReconstruction(renderer, device),
                                       .visibilityDisplay = &m_visibilityDisplay});
        setPanelVisible(EditorPanel::Viewport, open);
        m_viewportHovered = result.hovered;
        m_viewportFocused = result.focused;
        viewportUsable = result.measured;
        m_viewportBackingScale = result.backingScale;
        if (result.measured) {
            m_viewportWidth = result.width;
            m_viewportHeight = result.height;
            if (m_viewportWidth == m_stableWidth && m_viewportHeight == m_stableHeight) {
                ++m_stableFrames;
            } else {
                m_stableWidth = m_viewportWidth;
                m_stableHeight = m_viewportHeight;
                m_stableFrames = 0;
            }
        }
    } else {
        // A hidden Viewport measures nothing, so the last extent stands and the debounce neither
        // advances nor asks for a resize to a size no panel is showing.
        m_viewportHovered = false;
        m_viewportFocused = false;
    }
    m_viewportUsable = viewportUsable;
    if (!viewportUsable) {
        // Closed (visibility false) or collapsed (visible but Begin reported nothing to measure):
        // either way there is no image left to look over. A no-op when no look is in progress.
        endMouseLook();
    }

    m_selection = resolveSelection(m_selection, m_activeSceneId, m_session.scene());
    bool selectionHidden = false;
    if (const auto* loaded = m_session.loadedScene()) {
        if (documentSelectionHidden) {
            selectionHidden = *documentSelectionHidden;
        } else if (!m_sceneFilter.empty()) {
            const auto& tree = m_sceneTree.view(m_activeSceneId.key,
                                                {.loaded = *loaded,
                                                 .state = m_session.documentState(),
                                                 .session = &m_session,
                                                 .filter = m_sceneFilter,
                                                 .sceneGeneration = m_temporalState.sceneGeneration,
                                                 .editGeneration = m_session.editGeneration()});
            selectionHidden = sceneTreeSelectionHidden(tree.rows, m_selection, m_sceneFilter);
        }
    } else {
        selectionHidden = selectionHiddenByFilter(m_session.scene(), m_selection, m_sceneFilter);
    }
    const auto inspectorContext =
        InspectorPanelContext{.selection = m_selection,
                              .session = m_session,
                              .renderer = renderer,
                              .settings = m_settings,
                              .exposureContext = m_exposureContext,
                              .exposureResetPending = m_exposureResetPending,
                              .temporalState = m_temporalState,
                              .dynamicResolutionState = m_dynamicResolutionState,
                              .temporalSupport = device.capabilities().temporalScaler,
                              .viewportWidth = m_viewportWidth,
                              .viewportHeight = m_viewportHeight,
                              .viewportVisible = viewportUsable,
                              .documentDirty = m_documentDirty,
                              .selectionHiddenByFilter = selectionHidden,
                              .visibilityDisplay = &m_visibilityDisplay,
                              .sceneFilter = &m_sceneFilter,
                              .openPerformance =
                                  [this] {
                                      setPanelVisible(EditorPanel::Performance, true);
                                      m_performancePanel.requestFocus = true;
                                      m_performancePanel.requestLiveTab = true;
                                  },
                              .lightingDisplay = &m_lightingDisplay};
    if (m_workspace.visibility.isVisible(EditorPanel::Inspector)) {
        bool open = true;
        drawInspectorPanel(open, inspectorContext);
        setPanelVisible(EditorPanel::Inspector, open);
    }
    if (m_workspace.visibility.isVisible(EditorPanel::Rendering)) {
        bool open = true;
        drawRenderingPanel(open, inspectorContext);
        setPanelVisible(EditorPanel::Rendering, open);
    }

    ImGui::EndDisabled();
    if (m_workspace.visibility.isVisible(EditorPanel::Performance)) {
        bool open = true;
        m_performanceModel.setContextEpoch(metricsContextEpoch());
        MeasurementPanelContext measurement{m_measurement, m_measurementWarmup, m_measurementFrames,
                                            m_measurementExportPath, m_measurementFeedback};
        measurement.startDisabledReason =
            m_playback.active() ? "Stop scene playback before starting a measurement."
            : m_settings.dynamicResolutionEnabled
                ? "Turn off dynamic resolution before starting a fixed-plan measurement."
                : "";
        drawPerformancePanel(open, m_performanceModel, m_performancePanel, &measurement);
        switch (measurement.action) {
        case MeasurementAction::Start:
            startMeasurement(device, renderer);
            break;
        case MeasurementAction::Stop:
            stopPlayback();
            break;
        case MeasurementAction::Export:
            exportMeasurement();
            break;
        case MeasurementAction::None:
            break;
        }
        setPanelVisible(EditorPanel::Performance, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::PerformanceSummary)) {
        bool open = true;
        if (drawPerformanceSummary(open, m_performanceModel)) {
            setPanelVisible(EditorPanel::Performance, true);
            m_performancePanel.requestFocus = true;
            m_performancePanel.requestLiveTab = true;
        }
        setPanelVisible(EditorPanel::PerformanceSummary, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::Console)) {
        bool open = true;
        drawConsolePanel(open, m_consoleModel);
        setPanelVisible(EditorPanel::Console, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::RenderGraph)) {
        bool open = true;
        drawRenderGraphPanel(open, m_renderGraphPanel, frameRecords, m_notices);
        setPanelVisible(EditorPanel::RenderGraph, open);
    }
}

//======================================================================================================================
rojoRHI::Result<void> EditorShell::primeTemporal(const AppOptions& options) {
    m_settings.localLightMode = options.localLightMode;
    m_settings.lightCheck = options.lightCheck;
    m_settings.lightDebugView = options.lightDebugView;
    m_settings.temporalEnabled = options.temporal != TemporalMode::Off;
    m_settings.jitterEnabled = options.temporal != TemporalMode::Off;
    m_settings.reconstruction = temporalReconstructionMode(options.temporal);
    m_settings.temporalDebugView = options.temporalView;
    m_settings.renderScale = options.renderScale;
    m_labLights = options.labLights;
    m_labLightPile = options.labLightPile;
    m_labInstances = options.labInstances;
    m_labOccluders = options.labOccluders;
    m_settings.visibilityEnabled = options.visibilityEnabled;
    m_settings.submission = options.submission;
    m_settings.classifyMode = options.classifyMode;
    m_settings.classifyCheck = options.classifyCheck;
    m_settings.occlusionEnabled = options.occlusionEnabled;
    m_settings.occlusionCheck = options.occlusionCheck;
    m_settings.hzbDebugLevel = options.hzbDebugLevel;
    if (options.localLightRigOverride && m_session.localLightRigAvailable()) {
        return m_session.setLocalLightRig(*options.localLightRigOverride);
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> EditorShell::prepareSceneFrame(uint64_t frameNumber) {
    return m_session.prepareFrame(frameNumber);
}

//======================================================================================================================
render::SceneView EditorShell::sceneView() {
    render::SceneView view = m_session.view(m_drawItems, m_settings.wireframe);
    view.localLightMode = m_settings.localLightMode;
    view.lightCheck = m_settings.lightCheck;
    view.lightDebugView = m_settings.lightDebugView;
    view.visibilityEnabled = m_settings.visibilityEnabled;
    view.submission = m_settings.submission;
    view.classifyMode = m_settings.classifyMode;
    view.classifyCheck = m_settings.classifyCheck;
    view.occlusionEnabled = m_settings.occlusionEnabled;
    view.occlusionCheck = m_settings.occlusionCheck;
    view.hzbDebugLevel = m_settings.hzbDebugLevel;
    view.temporal.enabled = m_settings.temporalEnabled;
    view.temporal.jitterEnabled = m_settings.jitterEnabled;
    view.temporal.reconstruction = m_settings.reconstruction;
    view.temporal.debugView = m_settings.temporalDebugView;
    view.temporal.renderScale = m_settings.renderScale;
    view.temporal.sceneGeneration = m_temporalState.sceneGeneration;
    // Consumed here rather than left for main.cpp: a cut is a one-shot camera event, not a render
    // setting, so its latch belongs next to the generation counter it is unrelated to but shares a
    // lifetime with (both are TemporalEditorState.h).
    syncSessionTemporalReset(m_temporalState, m_session);
    view.temporal.cameraCut = consumeCameraCut(m_temporalState);
    return view;
}

//======================================================================================================================
render::GraphTexture EditorShell::declareSelection(render::RenderGraph& graph,
                                                   rojoRHI::CommandList& commands,
                                                   render::GraphTexture display,
                                                   const render::SceneView& view,
                                                   const render::Renderer& renderer) {
    if (m_selectionOutline->target().width() != renderer.width() ||
        m_selectionOutline->target().height() != renderer.height() || !m_showSelectionOutline ||
        !m_viewportUsable || m_selection.subject != EditorSubject::Object ||
        m_selection.index >= view.items.size()) {
        return display;
    }
    const auto& result = renderer.visibilityStatus().scene;
    const bool visible =
        m_settings.classifyMode == render::ClassifyMode::Gpu ||
        m_selection.index >= result.candidates.size() ||
        result.candidates[m_selection.index].state != render::VisibilityState::Rejected;
    return m_selectionOutline->declare(graph, commands, view,
                                       {.display = display,
                                        .camera = m_session.camera(),
                                        .selectedDraw = static_cast<uint32_t>(m_selection.index),
                                        .backingScale = m_viewportBackingScale,
                                        .visible = visible});
}

//======================================================================================================================
bool EditorShell::consumeExposureReset() {
    const bool pending = m_exposureResetPending;
    m_exposureResetPending = false;
    return pending;
}

//======================================================================================================================
void EditorShell::controllerDeclared(uint64_t frame) {
    if (dynamicResolutionActive(m_settings)) {
        m_resolutionController.declared(frame);
    }
}

//======================================================================================================================
void EditorShell::advanceFrameAnimation() {
    if (m_measurement.active()) {
        if (const auto frame = m_measurement.nextFrame())
            m_session.prepareSequenceFrame(frame->sequenceFrame);
        return;
    }
    m_session.advanceEditorFrame(m_playback.playing(),
                                 m_playback.active() && m_settings.followCameraTrack, m_looking);
}

//======================================================================================================================
uint64_t EditorShell::metricsContextEpoch() {
    uint64_t key =
        m_temporalState.sceneGeneration * 65536 + (m_settings.occlusionEnabled ? 512 : 0) +
        (m_settings.occlusionCheck ? 1024 : 0) +
        static_cast<uint64_t>(m_settings.hzbDebugLevel + 1) * 2048 +
        static_cast<uint64_t>(m_settings.classifyMode) * 128 +
        (m_settings.classifyCheck ? 256 : 0) + static_cast<uint64_t>(m_settings.submission) * 16 +
        (m_settings.visibilityEnabled ? 8 : 0) +
        static_cast<uint64_t>(m_settings.reconstruction) * 2 + (m_settings.temporalEnabled ? 1 : 0);
    // Lighting modes can share a pass inventory while performing different amounts of work.
    const auto mix = [&](uint64_t value) { key = (key ^ value) * 1099511628211ULL; };
    mix(static_cast<uint64_t>(m_settings.localLightMode));
    mix(static_cast<uint64_t>(m_settings.lightDebugView));
    mix(m_settings.lightCheck);
    mix(m_session.scene().enabledLightCount());
    mix(m_session.localLightRigEnabled());
    mix(m_session.lightLabPileCount());
    return m_metricsContextRevision.observe(key);
}

//======================================================================================================================
FrameMetricsMetadata EditorShell::frameMetrics(const render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    const auto& extents = renderer.temporalStatus().extents;
    return {
        .classifyMilliseconds = renderer.visibilityStatus().classifyMs,
        .prepareMilliseconds = renderer.visibilityStatus().prepareMs,
        .contextEpoch = metricsContextEpoch(),
        .objectCount = static_cast<uint32_t>(m_session.scene().objects.size()),
        .drawCount = renderer.visibilityStatus().submission.sceneCommands +
                     renderer.visibilityStatus().submission.shadowCommands +
                     (m_session.scene().skySphere && m_session.scene().skyCubemap ? 1u : 0u),
        .viewportLogicalWidth =
            static_cast<uint32_t>(m_viewportWidth / std::max(io.DisplayFramebufferScale.x, 1.0f)),
        .viewportLogicalHeight =
            static_cast<uint32_t>(m_viewportHeight / std::max(io.DisplayFramebufferScale.y, 1.0f)),
        .renderPixelWidth = m_settings.temporalEnabled ? extents.renderWidth : renderer.width(),
        .renderPixelHeight = m_settings.temporalEnabled ? extents.renderHeight : renderer.height(),
        .outputPixelWidth = renderer.width(),
        .outputPixelHeight = renderer.height()};
}

//======================================================================================================================
void EditorShell::commitFrame() {
    m_session.commitFrame();
}

//======================================================================================================================
render::ReconstructionMode
EditorShell::effectiveReconstruction(const render::Renderer& renderer,
                                     const rojoRHI::Device& device) const {
    return render::resolveReconstruction(
               m_settings.reconstruction, device.capabilities().temporalScaler,
               renderer.temporalStatus().vendorFallback == render::VendorFallback::CreationFailed)
        .mode;
}

} // namespace lmx::app
