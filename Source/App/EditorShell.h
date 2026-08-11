//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.h
/// @brief Declares the docked editor shell and its per-frame interface.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/ExposureReset.h"
#include "App/FrameRecordRing.h"
#include "App/PassTimingHistory.h"
#include "Engine/SceneLibrary.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

/// The editor shell: the Dear ImGui context, the dockspace and its two panels, the fly camera, and
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
    /// both are startup-fatal, unlike a later scene switch (buildInspector -> selectScene), which
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
    /// retired frame, while Stats rolls timings from successive retired frames into a stable
    /// summary. At this point the frame loop has not retained the current frame, so both see the
    /// newest joined record as of the previous iteration.
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
    bool poolingEnabled() const { return m_poolingEnabled; }

    /// The active scene's display name, for capture tooling. Empty until a scene is loaded.
    std::string_view activeSceneName() const {
        return m_activeScene != nullptr ? m_activeScene->name : std::string_view{};
    }

private:
    EditorShell(SDL_Window* window, engine::SceneLibrary& library);

    void buildViewport(render::Renderer& renderer);
    void buildInspector(rhi::Device& device, render::Renderer& renderer);
    void buildSceneCombo(rhi::Device& device);
    void buildLightsSection();
    void buildRenderSettingsSection();
    void buildObjectsSection();
    void updatePassTimingDisplay(float deltaSeconds, const FrameRecordRing& frameRecords);
    // A separate top-level window, not an Inspector section: the Stats panel already summarizes a
    // frame's pass timings, and this is the frame's full compiled shape -- passes, culling,
    // transitions, transient placement -- which is too much detail to nest under it.
    void buildGraphInspector(const FrameRecordRing& frameRecords);
    // device.waitIdle() then library.get(id); on failure, logs and leaves the current scene
    // active (spec §3: "error -> log + keep current scene"). On success, re-points the camera at
    // the new scene's initial pose -- the only per-scene UI state this shell carries.
    void selectScene(rhi::Device& device, engine::SceneId id);
    void updateCameraInput(float deltaSeconds);

    SDL_Window* m_window = nullptr;
    engine::SceneLibrary& m_library;
    engine::SceneId m_activeSceneId = engine::defaultSceneId();
    // Non-owning: the library owns every Scene it has built, for the device's lifetime, which
    // outlives this shell. Never null once create() has returned successfully.
    engine::Scene* m_activeScene = nullptr;

    render::Camera m_camera;
    std::vector<render::DrawItem> m_drawItems;
    // Render-settings knobs the Inspector's checkbox/combo write and Scene::view() reads. Shell
    // state, not scene state -- switching scenes does not reset either one.
    bool m_wireframe = false;
    render::ShadowFilter m_shadowFilter = render::ShadowFilter::PCF;
    // Manual exposure in stops, written onto every SceneView this shell builds. Zero is unit
    // exposure; the range matches the slider in the Render Settings section.
    float m_exposureEv = 0.0f;
    // Transient pooling, on by default exactly as the graph's own default is. Editor state rather
    // than scene state, for the same reason the two above are.
    bool m_poolingEnabled = true;

    // Auto-exposure (spec 9): opt-in, off by default so manual exposure stays the default mode.
    bool m_autoExposureEnabled = false;
    // Metering parameters, all editable in Render Settings; defaults match Renderer.h's SceneView.
    float m_exposureLowPercentile = 50.0f;
    float m_exposureHighPercentile = 95.0f;
    float m_exposureTargetGrey = 0.18f;
    float m_exposureEvMin = -8.0f;
    float m_exposureEvMax = 8.0f;
    float m_exposureCompensationEv = 0.0f;
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

    // Bloom (spec 10): enabled by default, identically in the editor and --screenshot.
    bool m_bloomEnabled = true;
    float m_bloomThreshold = 1.0f;
    float m_bloomIntensity = 0.2f;

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
    // Display-only, shown in the Stats panel. Hover is what gates input, deliberately: a look
    // should start where the cursor is, not where the last click left the focus.
    bool m_viewportFocused = false;
    // True between the right-mouse press that entered relative mouse mode and its release.
    // Latched rather than re-derived each frame: relative mode hides the cursor, so the Viewport
    // window stops reporting itself as hovered for the whole duration of the look.
    bool m_looking = false;
    // Set at create() when no imgui.ini existed; cleared by the frame that lays out the
    // dockspace. Rebuilding the default layout on a later run would throw away the re-docking
    // the ini exists to persist.
    bool m_buildDefaultLayout = false;

    // Frame times for the Stats plot. A ring: ImGui::PlotLines takes the cursor as its
    // values_offset and unrolls it, so there is no discontinuity to shuffle away.
    std::array<float, 120> m_frameTimesMs{};
    size_t m_frameTimeCursor = 0;

    // Raw measurements are collected every time a new frame retires, but publishing a fresh text
    // snapshot only four times per second keeps the sub-millisecond digits readable. Pause stops
    // both collection and publication, so the visible comparison stays fixed until resumed.
    PassTimingHistory m_passTimingHistory;
    std::vector<PassTimingSummary> m_displayedPassTimings;
    float m_passTimingRefreshSeconds = 0.0f;
    bool m_passTimingsPaused = false;
};

} // namespace lmx::app
