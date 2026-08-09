#pragma once
#include "Engine/SceneLibrary.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// SDL is an implementation detail of the shell's input handling and of nothing else here, so the
// header takes the window as an opaque handle and every consumer that only wants the scene
// (Screenshot.cpp) stays free of SDL.
struct SDL_Window;

namespace lmx::app {

constexpr float kSceneClearGray = 0.7f;

// Maps a Scene's initial pose (Engine/Scene.h's SceneCamera -- position/yaw/pitch/fovY/near/far,
// deliberately not render::Camera so Engine never has to carry the fly-camera's App-only
// moveSpeed) to a fresh render::Camera, which keeps Camera's own default moveSpeed. Shared by
// EditorShell's startup and scene-switch paths and by Screenshot.cpp's offscreen path, so a
// scene's screenshot and its editor view start from exactly the same pose.
render::Camera cameraFromScene(const engine::SceneCamera& sceneCamera);

// The editor shell: the Dear ImGui context, the dockspace and its two panels, the fly camera, and
// the active engine::Scene the Inspector edits. One per process -- ImGui's context, and the
// Metal 4 renderer glue behind it, are both process-global -- which is why this is created
// through a factory and is neither copyable nor movable.
//
// It owns no RHI object and no Scene. The SceneLibrary (which owns every Scene it has built, for
// the device's lifetime) is passed in and outlives the shell; everything else the shell touches
// (the scene target it displays, the device it resizes against) is passed in per call, which
// keeps the destruction order the RHI requires visible in run(): the shell is declared last and
// so torn down first, while the device is still alive for imguiShutdown() to drain against.
class EditorShell {
public:
    // Creates the ImGui context and both backends (order per Metal4ImGui.h), then loads the
    // requested initial scene and points the camera at its initial pose. Returns nullptr after
    // logging, with nothing left initialised, if either ImGui backend refuses or the initial scene
    // fails to load --
    // both are startup-fatal, unlike a later scene switch (buildInspector -> selectScene), which
    // logs and keeps the previous scene active instead.
    static std::unique_ptr<EditorShell> create(SDL_Window* window, rhi::Device& device,
                                               engine::SceneLibrary& library,
                                               engine::SceneId initialScene);
    ~EditorShell();

    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    // Resizes the scene target to match the Viewport panel, once the panel has reported one size
    // for kResizeDebounceFrames consecutive frames.
    //
    // Call it outside both an ImGui frame and a Device frame -- it drains the GPU and frees the
    // old targets. Running it *after* the Viewport image was recorded would leave ImGui's draw
    // data naming a texture that no longer exists, which is why the frame loop calls it at the
    // top of the frame rather than next to the size measurement that feeds it.
    void applyPendingViewportResize(rhi::Device& device, render::Renderer& renderer);

    // Builds the whole UI for this frame and applies camera input. Between ImGui::NewFrame() and
    // ImGui::Render(). Takes the device because selecting a new scene this frame drains the GPU
    // (device.waitIdle()) before the library builds or hands back the scene.
    void buildUI(rhi::Device& device, render::Renderer& renderer, float deltaSeconds);

    // This frame's scene, valid until the next call -- it spans a draw list this shell owns.
    // Build the UI first: the Inspector edits the active scene's objects and lights that this
    // SceneView is derived from.
    render::SceneView sceneView();

    const render::Camera& camera() const { return m_camera; }

    // The active scene's display name, for capture tooling. Empty until a scene is loaded.
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
};

} // namespace lmx::app
