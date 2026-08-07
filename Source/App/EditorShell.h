#pragma once
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// SDL is an implementation detail of the shell's input handling and of nothing else here, so the
// header takes the window as an opaque handle and every consumer that only wants the scene
// (Screenshot.cpp) stays free of SDL.
struct SDL_Window;

namespace lmx::app {

// One editable thing in the scene. Deliberately a flat struct with public fields and no scene
// graph: the Inspector edits it in place, `makeDrawItems` turns it into an
// `lmx::render::DrawItem`, and that is the entire model M2 has (ADR 0004, spec §3).
struct SceneObject {
    std::string name; // Inspector display
    glm::vec3 position{0.0f};
    glm::vec3 eulerDegrees{0.0f};
    float scale = 1.0f;
    glm::vec4 baseColor{1.0f};
    const render::Mesh* mesh = nullptr; // non-owning; the meshes outlive the scene
    bool rotating = false;              // spins about +Y at 45 deg/s when set

    // Composed T * R_spin * R_euler * S, uniform scale only -- Mesh.slang transforms normals by
    // the model matrix's upper 3x3, which is only a valid normal transform without shear.
    glm::mat4 modelMatrix(float timeSeconds) const;
};

// The meshes `makeDefaultScene` points at, created together: their proportions (a ground plane
// wide enough to fall outside the default frustum, unit cubes standing on it) are a property of
// the pair, not of either one.
struct SceneMeshes {
    render::Mesh cube;
    render::Mesh plane;
};
rhi::Result<SceneMeshes> createSceneMeshes(rhi::Device& device);

// The M2 scene: a gray ground plane and three lit cubes, the middle one spinning. One definition,
// shared by the editor and the `--screenshot` path -- the screenshot is only evidence about the
// app if both draw the same thing (the rule M1's TriangleAssets existed to enforce).
std::vector<SceneObject> makeDefaultScene(const render::Mesh& cube, const render::Mesh& plane);

// The pose `makeDefaultScene` was composed for: back and above the origin, tilted down far
// enough to frame the plane and all three cubes.
render::Camera makeDefaultCamera();

// Refills `out` with this frame's draw list. Takes the destination by reference rather than
// returning a vector because the editor calls it every frame; the screenshot path passes a
// local. Objects without a mesh are skipped -- DrawItem::mesh may not be null.
void makeDrawItems(std::span<const SceneObject> scene, float timeSeconds,
                   std::vector<render::DrawItem>& out);

// The editor shell: the Dear ImGui context, the dockspace and its two panels, the fly camera and
// the scene the Inspector edits. One per process -- ImGui's context, and the Metal 4 renderer
// glue behind it, are both process-global -- which is why this is created through a factory and
// is neither copyable nor movable.
//
// It owns no RHI object. Everything it touches (the scene target it displays, the device it
// resizes against) is passed in per call, which keeps the destruction order the RHI requires
// visible in `run()`: the shell is declared last and so torn down first, while the device is
// still alive for imguiShutdown() to drain against.
class EditorShell {
public:
    // Creates the ImGui context and both backends, in the order Metal4ImGui.h documents. Returns
    // nullptr after logging, with nothing left initialised, if either backend refuses.
    static std::unique_ptr<EditorShell> create(SDL_Window* window, rhi::Device& device,
                                               std::vector<SceneObject> scene);
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
    // ImGui::Render().
    void buildUI(render::Renderer& renderer, float deltaSeconds);

    // This frame's draw list, valid until the next call. Build the UI first: the Inspector edits
    // the scene these items are derived from.
    std::span<const render::DrawItem> drawItems(float timeSeconds);

    const render::Camera& camera() const { return m_camera; }

private:
    EditorShell(SDL_Window* window, std::vector<SceneObject> scene);

    void buildViewport(render::Renderer& renderer);
    void buildInspector(render::Renderer& renderer);
    void updateCameraInput(float deltaSeconds);

    SDL_Window* m_window = nullptr;
    render::Camera m_camera;
    std::vector<SceneObject> m_scene;
    std::vector<render::DrawItem> m_drawItems;

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
