//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.cpp
/// @brief Implements the docked editor UI, input, and scene interaction.
//----------------------------------------------------------------------------------------------------------------------

#include "App/EditorShell.h"

#include "App/EditorFont.h"

#include "App/Panels/ActionFeedback.h"
#include "App/Panels/ConsolePanel.h"
#include "App/Panels/EditorStyle.h"
#include "App/Panels/InspectorPanel.h"
#include "App/Panels/PerformancePanel.h"
#include "App/Panels/RenderGraphPanel.h"
#include "App/Panels/ScenePanel.h"
#include "App/Panels/ViewportPanel.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4ImGui.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// DockBuilder and ImGuiSettingsHandler are internal ImGui APIs; contain the unstable include here.
#include <imgui_internal.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

namespace {

// Debounce resize-driven GPU stalls until the dock splitter settles.
constexpr uint32_t kResizeDebounceFrames = 10;

// Tuned so a roughly screen-wide drag turns the camera 180 degrees.
constexpr float kLookRadiansPerPixel = 0.0025f;

// The default topology's share of the work area: Scene and Inspector flank a central column whose
// lower quarter holds Performance, and the Viewport takes what remains.

// The side panels are never narrower than this while the Viewport still has room to spare.
constexpr float kMinSceneWidthPoints = 220.0f;
constexpr float kMinInspectorWidthPoints = 320.0f;
// When it does not, the Viewport wins and the side and lower panels give these back first.
constexpr float kMinViewportWidthPoints = 640.0f;
constexpr float kMinViewportHeightPoints = 360.0f;

// Luminex's own entry in imgui.ini, written as "[LuminexWorkspace][Workspace]". ImGui disallows
// '[' and ']' in a handler type name and hashes it to route the section back to this handler.
constexpr const char* kWorkspaceSettingsType = "LuminexWorkspace";
constexpr const char* kWorkspaceSettingsName = "Workspace";

// The section body ImGui hands back never includes its own header line, so the schema decision is
// reached through the same text writeWorkspaceSettings emits.
constexpr std::string_view kNoSchemaReason = "no matching workspace schema in imgui.ini";
constexpr std::string_view kResetReason = "layout reset requested";

//======================================================================================================================
// ImGui settings handlers are C function pointers, so each one recovers the shell's workspace
// storage from the handler's UserData rather than from a process-wide global.
WorkspaceSettings& workspaceSettingsOf(ImGuiSettingsHandler* handler) {
    LMX_ASSERT(handler != nullptr && handler->UserData != nullptr,
               "workspace settings handler: UserData must name the shell's WorkspaceSettings");
    return *static_cast<WorkspaceSettings*>(handler->UserData);
}

//======================================================================================================================
void workspaceSettingsClearAll(ImGuiContext*, ImGuiSettingsHandler* handler) {
    WorkspaceSettings& settings = workspaceSettingsOf(handler);
    settings.sectionSeen = false;
    settings.sectionText.clear();
}

//======================================================================================================================
void* workspaceSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
    if (name == nullptr || std::string_view(name) != kWorkspaceSettingsName) {
        // A future entry name under the same type; leaving the section unseen keeps an ini this
        // build cannot understand on the legacy path rather than half-reading it.
        return nullptr;
    }
    WorkspaceSettings& settings = workspaceSettingsOf(handler);
    settings.sectionSeen = true;
    settings.sectionText.clear();
    return &settings;
}

//======================================================================================================================
void workspaceSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry,
                               const char* line) {
    WorkspaceSettings& settings = *static_cast<WorkspaceSettings*>(entry);
    // ImGui strips the line terminator; parseWorkspaceSettings reads newline-separated text.
    settings.sectionText += line;
    settings.sectionText += '\n';
}

//======================================================================================================================
void workspaceSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler,
                               ImGuiTextBuffer* outBuffer) {
    const WorkspaceSettings& settings = workspaceSettingsOf(handler);
    outBuffer->appendf("[%s][%s]\n", kWorkspaceSettingsType, kWorkspaceSettingsName);
    outBuffer->append(writeWorkspaceSettings(kWorkspaceSchemaVersion, settings.visibility,
                                             settings.uiScalePercent)
                          .c_str());
    outBuffer->append("\n");
}

// The point sizes the default topology gives the three panels that flank the Viewport.
struct DefaultLayoutExtents {
    float sceneWidth = 0.0f;
    float inspectorWidth = 0.0f;
    float performanceHeight = 0.0f;
};

//======================================================================================================================
// The default proportions raised to the side minima, then relaxed by however much the Viewport's
// own minimum content region still needs: the flanking panels narrow proportionally rather than
// squeezing the image the workspace exists to show.
//
// The minima are applied before that relaxation rather than behind a work-area size test, so a
// 1280x720 window satisfies them even though its work area is shorter than 720 points once the menu
// bar is subtracted. A work area with room for both -- anything at least 1280 points wide -- always
// gets them, and a narrower one loses them to the Viewport in proportion either way.
DefaultLayoutExtents defaultLayoutExtents(float workWidth, float workHeight) {
    DefaultLayoutExtents extents;
    extents.sceneWidth = workWidth >= 1500.0f ? 240.0f : kMinSceneWidthPoints;
    extents.inspectorWidth = workWidth >= 1500.0f ? 340.0f : kMinInspectorWidthPoints;
    extents.performanceHeight = workHeight >= 900.0f ? 300.0f : 210.0f;

    const float sideBudget = std::max(workWidth - kMinViewportWidthPoints, 0.0f);
    const float sideWanted = extents.sceneWidth + extents.inspectorWidth;
    if (sideWanted > sideBudget && sideWanted > 0.0f) {
        const float scale = sideBudget / sideWanted;
        extents.sceneWidth *= scale;
        extents.inspectorWidth *= scale;
    }
    extents.performanceHeight =
        std::min(extents.performanceHeight, std::max(workHeight - kMinViewportHeightPoints, 0.0f));
    return extents;
}

//======================================================================================================================
// DockBuilderSplitNode requires a ratio strictly inside (0, 1), and a minimized window reports a
// zero-sized work area, so a share of the node being split is clamped rather than trusted.
float splitFraction(float extent, float available) {
    if (!(available > 0.0f)) {
        return 0.5f;
    }
    return std::clamp(extent / available, 0.05f, 0.95f);
}

//======================================================================================================================
// Builds the four docked panels of the default topology: Scene left, Inspector right, Performance
// below the Viewport, and the Viewport in what remains. The fifth panel, Render Graph, is never
// docked -- it lives in its own OS window.
void buildDefaultLayout(ImGuiID dockspaceId) {
    const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    const DefaultLayoutExtents extents = defaultLayoutExtents(work.x, work.y);

    // DockSpaceOverViewport already created this node; DockBuilder needs a fresh owned node.
    // Removing it also undocks every window it held, which is what keeps a repeated reset from
    // accumulating nodes or leaving a second copy of a panel docked elsewhere.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // Split ratios derive from the current node size, so size it first.
    ImGui::DockBuilderSetNodeSize(dockspaceId, work);

    // Each ratio is a share of the node being split, and that node shrinks as the splits proceed.
    ImGuiID centerId = dockspaceId;
    ImGuiID performanceId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down,
                                splitFraction(extents.performanceHeight, work.y), &performanceId,
                                &centerId);
    ImGuiID sceneId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, splitFraction(extents.sceneWidth, work.x),
                                &sceneId, &centerId);
    ImGuiID inspectorId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right,
                                splitFraction(extents.inspectorWidth, work.x - extents.sceneWidth),
                                &inspectorId, &centerId);

    ImGui::DockBuilderDockWindow(kScenePanelWindowName, sceneId);
    ImGui::DockBuilderDockWindow(kInspectorPanelWindowName, inspectorId);
    ImGui::DockBuilderDockWindow(kPerformancePanelWindowName, performanceId);
    ImGui::DockBuilderDockWindow(kConsolePanelWindowName, performanceId);
    // Render Graph is deliberately absent: its window class forbids docking into an unclassed
    // node, so it always owns its own OS window and there is no dock node to place it in.
    ImGui::DockBuilderDockWindow(kViewportPanelWindowName, centerId);
    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace

//======================================================================================================================
EditorShell::EditorShell(SDL_Window* window, scene::SceneLibrary& library,
                         std::shared_ptr<ConsoleLog> consoleLog)
    : m_window(window), m_library(library), m_consoleModel(std::move(consoleLog)) {}

//======================================================================================================================
std::unique_ptr<EditorShell> EditorShell::create(SDL_Window* window, rhi::Device& device,
                                                 scene::SceneLibrary& library,
                                                 scene::SceneId initialScene,
                                                 std::shared_ptr<ConsoleLog> consoleLog) {
    LMX_ASSERT(window != nullptr, "EditorShell::create: window must not be null");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Platform viewports draw windows the RHI's single swapchain knows nothing about: the vendored
    // Metal 4 ImGui backend creates a CAMetalLayer per extra window and renders it with its own
    // command buffer on the shared device queue. main.cpp drives them after each presented frame.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    configureEditorFont();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FramePadding = ImVec2(8.0f, 5.0f);
    ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 8.0f);
    ImGui::GetStyle().WindowPadding = ImVec2(12.0f, 12.0f);

    if (!ImGui_ImplSDL3_InitForMetal(window)) {
        LMX_LOG_ERROR("ImGui_ImplSDL3_InitForMetal failed: {}", SDL_GetError());
        ImGui::DestroyContext();
        return nullptr;
    }
    // ImGui's pipeline format must match the swapchain drawable.
    if (!rhi::metal4::imguiInit(device, rhi::Format::BGRA8Unorm)) {
        ImGui_ImplSDL3_Shutdown();
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
        rhi::metal4::imguiShutdown();
        ImGui::DestroyContext();
        return nullptr;
    }
    self->m_activeSceneId = initialScene;
    self->m_session.activate(**scene, SceneActivationMotion::Reset);
    // Startup selects the scene's Camera (spec section 5); every scene provides one.
    self->m_selection = initialSelection(initialScene);
    // The startup scene is a selection like any other (spec 9): the generation counter bumps from
    // its 0-as-unset start, motion has nothing to report yet.
    onSceneSelected(self->m_temporalState, self->m_settings, initialScene);

    ExposureResetContext initial = self->m_exposureContext;
    initial.sceneId = initialScene;
    self->m_exposureResetPending = shouldResetExposure(self->m_exposureContext, initial);
    self->m_exposureContext = initial;

    // Register before any settings are read so Luminex's section is routed to this handler, and
    // read the ini here rather than letting the first NewFrame() do it: the schema decision below
    // has to be settled before a frame can lay out a dockspace.
    ImGuiSettingsHandler workspaceHandler;
    workspaceHandler.TypeName = kWorkspaceSettingsType;
    workspaceHandler.TypeHash = ImHashStr(kWorkspaceSettingsType);
    workspaceHandler.ClearAllFn = workspaceSettingsClearAll;
    workspaceHandler.ReadOpenFn = workspaceSettingsReadOpen;
    workspaceHandler.ReadLineFn = workspaceSettingsReadLine;
    workspaceHandler.WriteAllFn = workspaceSettingsWriteAll;
    workspaceHandler.UserData = &self->m_workspace;
    ImGui::AddSettingsHandler(&workspaceHandler);
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
    self->m_buildDefaultLayout = decision.kind == WorkspaceDecisionKind::BuildDefault;
    self->m_layoutBuildReason = kNoSchemaReason;

    LMX_LOG_INFO("editor shell: {} (scene '{}', {} objects)",
                 self->m_buildDefaultLayout
                     ? "no matching workspace schema -- the default layout will be built"
                     : "workspace schema matches -- restoring the docked layout from imgui.ini",
                 self->m_session.scene().name, self->m_session.scene().objects.size());
    return self;
}

//======================================================================================================================
EditorShell::~EditorShell() {
    // Shutting down while relative mouse mode is still on would leave the user's cursor hidden and
    // captured with no window left to release it.
    endMouseLook();
    // The node-editor context unregisters from the ImGui context too, so it goes before both
    // backends and, like them, well before DestroyContext().
    releaseRenderGraphPanelState(m_renderGraphPanel);
    // Backends unregister from the ImGui context, so destroy the context last.
    ImGui_ImplSDL3_Shutdown();
    rhi::metal4::imguiShutdown();
    ImGui::DestroyContext();
}

//======================================================================================================================
bool EditorShell::applyPendingViewportResize(rhi::Device& device, render::Renderer& renderer) {
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
    rhi::metal4::imguiForgetTexture(renderer.colorTarget());
    if (auto resized = renderer.resize(m_viewportWidth, m_viewportHeight); !resized) {
        // Renderer::resize can replace some targets before a later allocation fails. Do not
        // declare another frame against mixed extents, even if the requested size now matches.
        LMX_LOG_ERROR("viewport resize to {}x{} failed; stopping rendering: {}", m_viewportWidth,
                      m_viewportHeight, resized.error().message);
        return false;
    }
    rhi::metal4::imguiForgetTexture(m_selectionOutline->target());
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
    const uint32_t percent = m_workspace.uiScalePercent;
    if (m_appliedUiScalePercent == percent) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();
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
    style.MouseCursorScale = m_baseUiStyle->MouseCursorScale * scale;
    m_appliedUiScalePercent = percent;
    LMX_LOG_INFO("editor UI scale: {}%", percent);
}

//======================================================================================================================
void EditorShell::setUiScale(uint32_t percent) {
    percent = normalizedUiScalePercent(percent);
    if (m_workspace.uiScalePercent != percent) {
        m_workspace.uiScalePercent = percent;
        ImGui::MarkIniSettingsDirty();
    }
}

//======================================================================================================================
void EditorShell::updateUiScaleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    // ImGui's macOS behavior maps physical Command to its logical Ctrl modifier.
    const bool command = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
    const bool control = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    if (!command || control || io.KeyAlt || io.WantTextInput || io.AppFocusLost || m_looking ||
        ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, true));
    } else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
               ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
        setUiScale(stepUiScalePercent(m_workspace.uiScalePercent, false));
    } else if (ImGui::IsKeyPressed(ImGuiKey_0, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
        setUiScale(kDefaultUiScalePercent);
    }
}

//======================================================================================================================
void EditorShell::buildUI(rhi::Device& device, render::Renderer& renderer, float deltaSeconds,
                          const FrameRecordRing& frameRecords) {
    applyPendingScene(device);
    const RetainedFrame* newestTimed = frameRecords.newestTimedFrame();
    observeRetiredTemporal(m_temporalState, newestTimed);
    std::optional<PerformanceFrameSample> sample;
    m_performanceModel.setContextEpoch(metricsContextEpoch());
    if (newestTimed != nullptr && newestTimed->metrics) {
        const FrameMetricsMetadata& metrics = *newestTimed->metrics;
        sample = PerformanceFrameSample{
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
    m_performanceModel.tick(deltaSeconds, sample ? &*sample : nullptr);
    const float renderScaleBeforeDynamicResolution = m_settings.renderScale;
    applyDynamicResolution(m_dynamicResolutionState, m_resolutionController, m_settings,
                           newestTimed);
    if (dynamicResolutionActive(m_settings) &&
        m_settings.renderScale != renderScaleBeforeDynamicResolution) {
        LMX_LOG_INFO("render scale {:.2f} -> {:.2f} after {:.2f} ms",
                     renderScaleBeforeDynamicResolution, m_settings.renderScale,
                     m_dynamicResolutionState.lastObservedMilliseconds);
    }

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
        m_layoutBuildReason = kResetReason;
    }

    updateUiScaleShortcuts();

    // Before the dockspace, so the work area the topology is built into excludes the menu bar.
    buildMainMenu();

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_buildDefaultLayout) {
        m_buildDefaultLayout = false;
        buildDefaultLayout(dockspaceId);
        m_focusDefaultPerformance = true;
        LMX_LOG_INFO("editor workspace: built the default panel layout ({})", m_layoutBuildReason);
    }

    buildPanels(device, renderer, frameRecords);
    // Input consumes this frame's hover state and Inspector edits.
    updateCameraInput(deltaSeconds);
}

//======================================================================================================================
void EditorShell::buildMainMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        const auto requested = drawSceneMenu(SceneMenuContext{
            .library = m_library, .activeSceneId = m_activeSceneId, .loading = m_sceneLoading});
        if (requested && *requested != m_activeSceneId) {
            m_sceneLoading.request(*requested);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            m_actions.requestQuit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        const auto visibilityItem = [this](const char* label, EditorPanel panel) {
            bool visible = m_workspace.visibility.isVisible(panel);
            if (ImGui::MenuItem(label, nullptr, &visible)) {
                setPanelVisible(panel, visible);
            }
        };
        visibilityItem(kScenePanelWindowName, EditorPanel::Scene);
        visibilityItem(kViewportPanelWindowName, EditorPanel::Viewport);
        visibilityItem(kInspectorPanelWindowName, EditorPanel::Inspector);
        visibilityItem(kPerformancePanelWindowName, EditorPanel::Performance);
        visibilityItem(kRenderGraphPanelWindowName, EditorPanel::RenderGraph);
        visibilityItem(kConsolePanelWindowName, EditorPanel::Console);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::BeginMenu("UI Scale")) {
            const uint32_t current = m_workspace.uiScalePercent;
            if (ImGui::MenuItem("Zoom Out", "Cmd+-", false, current > kUiScalePresets.front())) {
                setUiScale(stepUiScalePercent(current, false));
            }
            if (ImGui::MenuItem("Zoom In", "Cmd++", false, current < kUiScalePresets.back())) {
                setUiScale(stepUiScalePercent(current, true));
            }
            if (ImGui::MenuItem("Reset UI Scale", "Cmd+0")) {
                setUiScale(kDefaultUiScalePercent);
            }
            ImGui::Separator();
            for (const uint32_t percent : kUiScalePresets) {
                const std::string label = std::to_string(percent) + "%";
                if (ImGui::MenuItem(label.c_str(), nullptr, current == percent)) {
                    setUiScale(percent);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Default Layout")) {
            m_actions.requestResetLayout();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Capture Next GPU Frame", "C", false,
                            m_actions.captureAvailable() &&
                                m_actions.captureResult().status != ActionStatus::Pending)) {
            m_actions.requestCapture();
        }
        drawActionFeedback("capture", m_actions.captureResult());
        ImGui::EndMenu();
    }
    const uint32_t current = m_workspace.uiScalePercent;
    const float buttonPadding = ImGui::GetStyle().FramePadding.x * 2.0f;
    const std::string resetLabel = std::to_string(current) + "%##ResetUiZoom";
    const float controlsWidth = ImGui::CalcTextSize("-+").x +
                                ImGui::CalcTextSize(resetLabel.c_str(), nullptr, true).x +
                                buttonPadding * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float rightAlignedX =
        ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - controlsWidth;
    // Tiny detached/narrow arrangements still have the Layout menu and keyboard shortcuts.
    if (rightAlignedX >= ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(rightAlignedX);
        ImGui::BeginDisabled(current <= kUiScalePresets.front());
        if (ImGui::SmallButton("-##UiZoomOut")) {
            setUiScale(stepUiScalePercent(current, false));
        }
        ImGui::EndDisabled();
        editorTooltip("Zoom out the editor UI (Cmd+-). Fonts and controls shrink; camera settings "
                      "stay unchanged. Minimum 75%.");
        ImGui::SameLine();
        if (ImGui::SmallButton(resetLabel.c_str())) {
            setUiScale(kDefaultUiScalePercent);
        }
        editorTooltip("Current editor UI scale. Click to reset to 100% (Cmd+0). Saved with the "
                      "workspace; Reset Default Layout keeps this preference.");
        ImGui::SameLine();
        ImGui::BeginDisabled(current >= kUiScalePresets.back());
        if (ImGui::SmallButton("+##UiZoomIn")) {
            setUiScale(stepUiScalePercent(current, true));
        }
        ImGui::EndDisabled();
        editorTooltip("Zoom in the editor UI (Cmd++ or Cmd+=). Maximum 150%. The detached Render "
                      "Graph uses the same UI scale.");
    }
    ImGui::EndMainMenuBar();
}

//======================================================================================================================
void EditorShell::buildPanels(rhi::Device& device, render::Renderer& renderer,
                              const FrameRecordRing& frameRecords) {
    // Every panel is drawn only while visible, and hands its window close button back through the
    // same storage the Window menu writes, so the two can never disagree.
    if (m_workspace.visibility.isVisible(EditorPanel::Scene)) {
        bool open = true;
        drawScenePanel(open, ScenePanelContext{.activeSceneId = m_activeSceneId,
                                               .activeScene = m_session.scene(),
                                               .selection = m_selection,
                                               .filter = m_sceneFilter});
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
                                       .activeSceneName = m_session.scene().name,
                                       .camera = m_session.camera(),
                                       .scene = m_session.scene(),
                                       .settings = m_settings,
                                       .exposureContext = m_exposureContext,
                                       .exposureResetPending = m_exposureResetPending,
                                       .session = m_session,
                                       .temporalState = m_temporalState,
                                       .selection = m_selection,
                                       .actions = m_actions,
                                       .sceneId = m_activeSceneId});
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

    if (m_workspace.visibility.isVisible(EditorPanel::Inspector)) {
        bool open = true;
        // Healed here, immediately before the draw that reads it, so a stale scene id or
        // out-of-range index from any source never reaches the panel (spec section 5).
        m_selection = resolveSelection(m_selection, m_activeSceneId, m_session.scene());
        drawInspectorPanel(
            open, InspectorPanelContext{.selection = m_selection,
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
                                        .selectionHiddenByFilter = selectionHiddenByFilter(
                                            m_session.scene(), m_selection, m_sceneFilter),
                                        .sceneFilter = &m_sceneFilter});
        setPanelVisible(EditorPanel::Inspector, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::Performance)) {
        bool open = true;
        m_performanceModel.setContextEpoch(metricsContextEpoch());
        if (m_focusDefaultPerformance) {
            ImGui::SetNextWindowFocus();
            m_focusDefaultPerformance = false;
        }
        drawPerformancePanel(open, m_performanceModel);
        setPanelVisible(EditorPanel::Performance, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::Console)) {
        // Add only the new tab to an existing workspace. Existing dock nodes and selected tabs
        // remain untouched; a previously saved Console position wins over this first-use hint.
        if (const auto* settings =
                ImGui::FindWindowSettingsByID(ImHashStr(kPerformancePanelWindowName));
            settings != nullptr && settings->DockId != 0) {
            ImGui::SetNextWindowDockID(settings->DockId, ImGuiCond_FirstUseEver);
        } else if (const auto* performance = ImGui::FindWindowByName(kPerformancePanelWindowName);
                   performance != nullptr && performance->DockId != 0) {
            ImGui::SetNextWindowDockID(performance->DockId, ImGuiCond_FirstUseEver);
        }
        bool open = true;
        drawConsolePanel(open, m_consoleModel);
        setPanelVisible(EditorPanel::Console, open);
    }

    if (m_workspace.visibility.isVisible(EditorPanel::RenderGraph)) {
        bool open = true;
        drawRenderGraphPanel(open, m_renderGraphPanel, frameRecords);
        setPanelVisible(EditorPanel::RenderGraph, open);
    }
}

//======================================================================================================================
void EditorShell::setPanelVisible(EditorPanel panel, bool visible) {
    if (m_workspace.visibility.isVisible(panel) == visible) {
        return;
    }
    m_workspace.visibility.setVisible(panel, visible);
    // Nothing moved a window, so ImGui has no reason of its own to rewrite the ini; without this
    // the new visibility would be lost on exit.
    ImGui::MarkIniSettingsDirty();
}

//======================================================================================================================
void EditorShell::primeTemporal(const AppOptions& options) {
    m_settings.temporalEnabled = options.temporal != TemporalMode::Off;
    m_settings.jitterEnabled = options.temporal != TemporalMode::Off;
    m_settings.reconstruction = temporalReconstructionMode(options.temporal);
    m_settings.temporalDebugView = options.temporalView;
    m_settings.renderScale = options.renderScale;
}

//======================================================================================================================
rhi::Result<void> EditorShell::prepareSceneFrame(uint64_t frameNumber) {
    return m_session.prepareFrame(frameNumber);
}

//======================================================================================================================
render::SceneView EditorShell::sceneView() {
    render::SceneView view =
        m_session.view(m_drawItems, m_settings.shadowFilter, m_settings.wireframe);
    // Exposure is a shell knob rather than scene data, so it is applied after the scene has
    // described itself -- the same way the wireframe and shadow-filter settings are.
    view.exposureEv = m_settings.exposureEv;
    view.autoExposureEnabled = m_settings.autoExposureEnabled;
    // exposureReset is left at SceneView's default (false); main.cpp sets it from
    // consumeExposureReset() before declaring passes.
    view.exposureLowPercentile = m_settings.exposureLowPercentile;
    view.exposureHighPercentile = m_settings.exposureHighPercentile;
    view.exposureTargetGrey = m_settings.exposureTargetGrey;
    view.exposureEvMin = m_settings.exposureEvMin;
    view.exposureEvMax = m_settings.exposureEvMax;
    view.exposureCompensationEv = m_settings.exposureCompensationEv;
    view.exposureAdaptUpStopsPerSecond = m_settings.exposureAdaptUpStopsPerSecond;
    view.exposureAdaptDownStopsPerSecond = m_settings.exposureAdaptDownStopsPerSecond;
    view.bloomEnabled = m_settings.bloomEnabled;
    view.bloomThreshold = m_settings.bloomThreshold;
    view.bloomIntensity = m_settings.bloomIntensity;
    view.temporal.enabled = m_settings.temporalEnabled;
    view.temporal.jitterEnabled = m_settings.jitterEnabled;
    view.temporal.reconstruction = m_settings.reconstruction;
    view.temporal.debugView = m_settings.temporalDebugView;
    view.temporal.renderScale = m_settings.renderScale;
    view.temporal.sceneGeneration = m_temporalState.sceneGeneration;
    // Consumed here rather than left for main.cpp: a cut is a one-shot camera event, not a render
    // setting, so its latch belongs next to the generation counter it is unrelated to but shares a
    // lifetime with (both are TemporalEditorState.h).
    view.temporal.cameraCut = consumeCameraCut(m_temporalState);
    return view;
}

//======================================================================================================================
render::GraphTexture EditorShell::declareSelection(render::RenderGraph& graph,
                                                   rhi::CommandList& commands,
                                                   render::GraphTexture display,
                                                   const render::SceneView& view,
                                                   const render::Renderer& renderer) {
    if (m_selectionOutline->target().width() != renderer.width() ||
        m_selectionOutline->target().height() != renderer.height() || !m_showSelectionOutline ||
        !m_viewportUsable || m_selection.subject != EditorSubject::Object ||
        m_selection.index >= view.items.size()) {
        return display;
    }
    return m_selectionOutline->declare(graph, commands, display, m_session.camera(), view,
                                       m_selection.index, m_viewportBackingScale);
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
    m_session.advanceEditorFrame(m_settings.animationPlaying, m_settings.followCameraTrack,
                                 m_looking);
}

//======================================================================================================================
uint64_t EditorShell::metricsContextEpoch() {
    const uint64_t key = m_temporalState.sceneGeneration * 16 +
                         static_cast<uint64_t>(m_settings.reconstruction) * 2 +
                         (m_settings.temporalEnabled ? 1 : 0);
    return m_metricsContextRevision.observe(key);
}

//======================================================================================================================
FrameMetricsMetadata EditorShell::frameMetrics(const render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    const auto& extents = renderer.temporalStatus().extents;
    return {
        .contextEpoch = metricsContextEpoch(),
        .objectCount = static_cast<uint32_t>(m_session.scene().objects.size()),
        .drawCount = static_cast<uint32_t>(m_drawItems.size()),
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
void EditorShell::observeDeclaration(const render::Renderer& renderer, uint64_t frameId) {
    observeDeclaredTemporal(m_temporalState, m_settings, renderer.temporalStatus(), frameId);
}

//======================================================================================================================
void EditorShell::commitFrame() {
    m_session.commitFrame();
}

//======================================================================================================================
void EditorShell::applyPendingScene(rhi::Device& device) {
    const auto requested = m_sceneLoading.consumeRequest();
    if (!requested) {
        return;
    }
    const scene::SceneId requestedFrom = m_activeSceneId;
    const bool switched = selectScene(device, *requested);
    const SceneSwitchOutcome outcome =
        sceneSwitchOutcome(switched, requestedFrom, *requested, m_selection, m_sceneFilter);
    m_selection = outcome.selection;
    m_sceneFilter = outcome.filter;
}

//======================================================================================================================
bool EditorShell::selectScene(rhi::Device& device, scene::SceneId id) {
    if (id == m_activeSceneId) {
        return false;
    }
    // In-flight frames may still reference the current scene's meshes and textures.
    device.waitIdle();
    auto scene = m_library.get(id);
    if (!scene) {
        // A failed switch leaves the current scene renderable.
        LMX_LOG_ERROR("scene '{}' failed to load: {}", m_library.entry(id).displayName,
                      scene.error().message);
        m_sceneLoading.fail(id, scene.error().message);
        return false;
    }
    m_activeSceneId = id;
    m_session.activate(**scene, SceneActivationMotion::Reset);
    // The new scene has no motion to report yet, and its generation differs from whatever the
    // renderer last saw (TemporalEditorState.h), which is what tells the temporal history to reset
    // rather than reproject the previous scene's pixels onto this one's geometry.
    onSceneSelected(m_temporalState, m_settings, id);
    // A scene switch is a reset trigger (spec 9): the previous scene's metering has nothing to say
    // about the new one's content.
    ExposureResetContext candidate = m_exposureContext;
    candidate.sceneId = id;
    if (shouldResetExposure(m_exposureContext, candidate)) {
        m_exposureResetPending = true;
    }
    m_exposureContext = candidate;
    LMX_LOG_INFO("scene switched to '{}' ({} objects)", m_session.scene().name,
                 m_session.scene().objects.size());
    return true;
}

//======================================================================================================================
void EditorShell::updateCameraInput(float deltaSeconds) {
    // Drain SDL motion every frame so pre-look cursor travel cannot accumulate into a jump.
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    SDL_GetRelativeMouseState(&relativeX, &relativeY);

    if (ImGui::GetIO().WantTextInput) {
        endMouseLook();
        return;
    }
    if (!m_looking) {
        if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            SDL_SetWindowRelativeMouseMode(m_window, true);
        }
        // Ignore the entry-frame delta because it predates relative mode.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        endMouseLook();
        return;
    }

    // Screen Y grows downward while camera pitch grows upward.
    m_session.camera().look(relativeX * kLookRadiansPerPixel, -relativeY * kLookRadiansPerPixel);

    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec3 move{0.0f};
    move.z += keys[SDL_SCANCODE_W] ? 1.0f : 0.0f;
    move.z -= keys[SDL_SCANCODE_S] ? 1.0f : 0.0f;
    move.x += keys[SDL_SCANCODE_D] ? 1.0f : 0.0f;
    move.x -= keys[SDL_SCANCODE_A] ? 1.0f : 0.0f;
    move.y += keys[SDL_SCANCODE_E] ? 1.0f : 0.0f;
    move.y -= keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f;
    if (move != glm::vec3{0.0f}) {
        // Normalize diagonal movement to preserve speed.
        m_session.camera().move(glm::normalize(move) *
                                (m_session.camera().moveSpeed * deltaSeconds));
    }
}

//======================================================================================================================
void EditorShell::endMouseLook() {
    if (!m_looking) {
        return;
    }
    m_looking = false;
    SDL_SetWindowRelativeMouseMode(m_window, false);
    // Drop whatever relative motion SDL accumulated up to this point; carrying it into the next
    // look would turn the camera by everything the cursor did in between.
    SDL_GetRelativeMouseState(nullptr, nullptr);
}

} // namespace lmx::app
