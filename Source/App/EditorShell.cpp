#include "App/EditorShell.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4ImGui.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// The only place in the project that reaches into Dear ImGui's internals, and only for
// DockBuilder -- the default-layout API lives there rather than in imgui.h. Kept to this one
// translation unit so no other file inherits an unstable header.
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace lmx::app {

namespace {

// The one animation M2 has. Slow enough that a screenshot taken at any moment still reads as a
// cube rather than as motion blur in the reader's head.
constexpr float kSpinDegreesPerSecond = 45.0f;

// Half-extent of the ground plane. Large enough that its edge falls outside the default camera's
// frustum, so the scene reads as a floor rather than as a fourth floating object.
constexpr float kGroundHalfExtent = 5.0f;

// Cubes sit on the plane: makeCube() is a unit cube centred on its own origin, so +0.5 in y puts
// its bottom face exactly on y = 0.
constexpr float kCubeCenterY = 0.5f;
constexpr float kCubeSpacingX = 1.5f;

// How many consecutive frames the Viewport panel must report the same size before the scene
// target is recreated at it. A dock-splitter drag emits a new size every frame; recreating on
// each one would mean a waitIdle (a full GPU stall) per frame of the drag. Ten frames is under
// a fifth of a second at 60 Hz -- past the end of a drag, invisible after a click.
constexpr uint32_t kResizeDebounceFrames = 10;

// Fraction of the dockspace the Inspector takes in the first-run layout.
constexpr float kInspectorDockFraction = 0.22f;

// Radians of camera rotation per pixel of relative mouse motion. Tuned by feel; a full 180 deg
// sweep is about 1250 px, roughly a screen width.
constexpr float kLookRadiansPerPixel = 0.0025f;

// Frame times above this are off the top of the Stats plot. Fixed rather than auto-scaled so the
// plot's shape means the same thing from one moment to the next; 33.3 ms is two 60 Hz frames.
constexpr float kFrameTimePlotCeilingMs = 33.3f;

// Lays the dockspace out the way the editor is meant to open: Inspector on the right, everything
// left of it the Viewport. Only ever called on a run that found no imgui.ini.
void buildDefaultLayout(ImGuiID dockspaceId) {
    // Remove-then-add rather than reuse: DockSpaceOverViewport has already created the node this
    // frame, and DockBuilderSplitNode expects to be splitting a node it owns from scratch.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // Before the split, not after: the split's sizes are derived from the node's size, and a node
    // that has not been sized yet produces ratios that do not hold once it is.
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID inspectorId = 0;
    ImGuiID viewportId = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, kInspectorDockFraction, &inspectorId,
                                &viewportId);
    ImGui::DockBuilderDockWindow("Inspector", inspectorId);
    ImGui::DockBuilderDockWindow("Viewport", viewportId);
    ImGui::DockBuilderFinish(dockspaceId);
}

// ImGui reports panel sizes in points; the scene target is sized in pixels.
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

} // namespace

glm::mat4 SceneObject::modelMatrix(float timeSeconds) const {
    glm::mat4 model = glm::translate(glm::mat4{1.0f}, position);
    if (rotating) {
        model = glm::rotate(model, glm::radians(kSpinDegreesPerSecond * timeSeconds),
                            glm::vec3{0.0f, 1.0f, 0.0f});
    }
    // Y then X then Z, applied in that order to the object -- the convention the Inspector's
    // three drag fields are labelled against. Composed with glm::rotate rather than one of glm's
    // euler helpers so the order is readable here rather than in a header somewhere.
    model = glm::rotate(model, glm::radians(eulerDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, glm::vec3{scale});
}

rhi::Result<SceneMeshes> createSceneMeshes(rhi::Device& device) {
    SceneMeshes meshes;
    auto cube = render::createMesh(device, render::makeCube(), "lmx.app.cube");
    if (!cube) {
        return std::unexpected(cube.error());
    }
    meshes.cube = std::move(*cube);
    auto plane =
        render::createMesh(device, render::makePlane(kGroundHalfExtent), "lmx.app.groundPlane");
    if (!plane) {
        return std::unexpected(plane.error());
    }
    meshes.plane = std::move(*plane);
    return meshes;
}

std::vector<SceneObject> makeDefaultScene(const render::Mesh& cube, const render::Mesh& plane) {
    // The plane's own vertex colour is already light gray (0.8); the base colour only takes it
    // down far enough that the lit cubes read as the brighter things in the frame.
    return {
        SceneObject{.name = "Ground", .baseColor = {0.7f, 0.7f, 0.72f, 1.0f}, .mesh = &plane},
        SceneObject{.name = "Red",
                    .position = {-kCubeSpacingX, kCubeCenterY, 0.0f},
                    .baseColor = {0.9f, 0.2f, 0.2f, 1.0f},
                    .mesh = &cube},
        SceneObject{.name = "Gold",
                    .position = {0.0f, kCubeCenterY, 0.0f},
                    .baseColor = {0.9f, 0.7f, 0.2f, 1.0f},
                    .mesh = &cube,
                    .rotating = true},
        SceneObject{.name = "Blue",
                    .position = {kCubeSpacingX, kCubeCenterY, 0.0f},
                    .baseColor = {0.2f, 0.4f, 0.9f, 1.0f},
                    .mesh = &cube},
    };
}

render::Camera makeDefaultCamera() {
    render::Camera camera;
    // Set explicitly rather than left to the class defaults: this pose is a property of the
    // *scene* above -- back far enough for the outer cubes, high enough and tilted down far
    // enough for the plane to read as ground.
    camera.position = {0.0f, 2.5f, 7.0f};
    camera.yaw = 0.0f;
    camera.pitch = -0.25f;
    return camera;
}

void makeDrawItems(std::span<const SceneObject> scene, float timeSeconds,
                   std::vector<render::DrawItem>& out) {
    out.clear();
    out.reserve(scene.size());
    for (const SceneObject& object : scene) {
        if (object.mesh == nullptr) {
            continue;
        }
        out.push_back({.mesh = object.mesh,
                       .model = object.modelMatrix(timeSeconds),
                       .baseColor = object.baseColor});
    }
}

EditorShell::EditorShell(SDL_Window* window, std::vector<SceneObject> scene)
    : m_window(window), m_camera(makeDefaultCamera()), m_scene(std::move(scene)) {}

std::unique_ptr<EditorShell> EditorShell::create(SDL_Window* window, rhi::Device& device,
                                                 std::vector<SceneObject> scene) {
    LMX_ASSERT(window != nullptr, "EditorShell::create: window must not be null");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // ImGuiConfigFlags_ViewportsEnable stays OFF, deliberately: a platform viewport is a second
    // OS window with its own swapchain, and this RHI models exactly one.
    ImGui::StyleColorsDark();

    // io.IniFilename keeps its default, "imgui.ini" relative to the process CWD -- which for
    // `xmake run App` is the build target directory, so the file lands next to the binary and a
    // clean build wipes it. Accepted for M2 rather than adding a config-directory lookup: losing
    // the ini costs exactly one rebuild of the default layout below, and keeping it inside the
    // (gitignored) build tree keeps both the repo and the user's home directory clean.
    const bool hadIniFile =
        io.IniFilename != nullptr && std::filesystem::exists(std::filesystem::path(io.IniFilename));

    if (!ImGui_ImplSDL3_InitForMetal(window)) {
        LMX_LOG_ERROR("ImGui_ImplSDL3_InitForMetal failed: {}", SDL_GetError());
        ImGui::DestroyContext();
        return nullptr;
    }
    // Explicit rather than defaulted: this format must equal the swapchain's, because the UI pass
    // renders into the drawable and ImGui builds its pipeline against what it is told here.
    if (!rhi::metal4::imguiInit(device, rhi::Format::BGRA8Unorm)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return nullptr;
    }

    // Not make_unique: the constructor is private so that a shell can only exist once both
    // backends are up (the same rule Renderer::create follows).
    std::unique_ptr<EditorShell> self(new EditorShell(window, std::move(scene)));
    self->m_buildDefaultLayout = !hadIniFile;
    LMX_LOG_INFO("editor shell: {} ({} objects)",
                 hadIniFile ? "restoring the docked layout from imgui.ini"
                            : "no imgui.ini -- building the default docked layout",
                 self->m_scene.size());
    return self;
}

EditorShell::~EditorShell() {
    // Order per Metal4ImGui.h: the renderer glue drains the device itself, so nothing here has to
    // waitIdle first, and the context goes last because both backends unregister from it. The
    // device this ran against is still alive -- run() declares the shell last, so it dies first.
    ImGui_ImplSDL3_Shutdown();
    rhi::metal4::imguiShutdown();
    ImGui::DestroyContext();
}

void EditorShell::applyPendingViewportResize(rhi::Device& device, render::Renderer& renderer) {
    // A collapsed panel reports nothing; a zero-sized render target is not a thing that exists.
    // Skipping leaves the last good target in place, which is what the stretched image shows.
    if (m_viewportWidth == 0 || m_viewportHeight == 0) {
        return;
    }
    if (m_viewportWidth == renderer.width() && m_viewportHeight == renderer.height()) {
        return;
    }
    if (m_stableFrames < kResizeDebounceFrames) {
        return;
    }

    // The GPU must be idle before the old targets are freed: frames still in flight hold them in
    // their residency set and their encoders. No deferred-release machinery is added for this --
    // a dock drag does not justify it (spec §2), and the debounce above means the stall happens
    // once per resize rather than once per frame of one.
    device.waitIdle();
    // Before the resize frees it, and only meaningful because the drain above already happened:
    // ImGui's backend added this texture to its own residency set the first time it drew the
    // Viewport image and never removes one, so without this every resize strands a full-viewport
    // texture for the process lifetime.
    rhi::metal4::imguiForgetTexture(renderer.colorTarget());
    if (auto resized = renderer.resize(m_viewportWidth, m_viewportHeight); !resized) {
        // Not fatal: resize() leaves the previous, still-valid pair in place on failure, so the
        // editor keeps running with a stretched image rather than going down. The debounce is
        // restarted rather than left satisfied, so a failing size costs one waitIdle and one
        // failed allocation instead of both on every frame from here on.
        m_stableFrames = 0;
        LMX_LOG_ERROR("viewport resize to {}x{} failed: {}", m_viewportWidth, m_viewportHeight,
                      resized.error().message);
        return;
    }
    LMX_LOG_INFO("scene target resized to {}x{} px", m_viewportWidth, m_viewportHeight);
}

void EditorShell::buildUI(render::Renderer& renderer, float deltaSeconds) {
    m_frameTimesMs[m_frameTimeCursor] = deltaSeconds * 1000.0f;
    m_frameTimeCursor = (m_frameTimeCursor + 1) % m_frameTimesMs.size();

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_buildDefaultLayout) {
        m_buildDefaultLayout = false;
        buildDefaultLayout(dockspaceId);
    }

    buildViewport(renderer);
    buildInspector(renderer);
    // Last, so it sees this frame's hover state and this frame's Inspector edits: dragging the
    // camera position field and then flying from where it landed is one continuous motion.
    updateCameraInput(deltaSeconds);
}

std::span<const render::DrawItem> EditorShell::drawItems(float timeSeconds) {
    makeDrawItems(m_scene, timeSeconds, m_drawItems);
    return m_drawItems;
}

void EditorShell::buildViewport(render::Renderer& renderer) {
    // Zero padding: an editor viewport with a border inside its own window reads as a bug, and
    // the padding would also make the reported content size disagree with what is displayed.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    if (visible) {
        m_viewportHovered = ImGui::IsWindowHovered();
        m_viewportFocused = ImGui::IsWindowFocused();

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        m_viewportWidth = toPixels(available.x, io.DisplayFramebufferScale.x);
        m_viewportHeight = toPixels(available.y, io.DisplayFramebufferScale.y);

        if (m_viewportWidth == m_stableWidth && m_viewportHeight == m_stableHeight) {
            ++m_stableFrames;
        } else {
            m_stableWidth = m_viewportWidth;
            m_stableHeight = m_viewportHeight;
            m_stableFrames = 0;
        }

        if (available.x > 0.0f && available.y > 0.0f) {
            // Drawn at the panel's size whatever the target's size is, so a resize in progress
            // stretches the last good frame instead of tearing a hole in the layout.
            ImGui::Image(rhi::metal4::imguiTextureID(renderer.colorTarget()), available);
        }
    } else {
        // Collapsed: no content region to measure, and nothing to hover.
        m_viewportHovered = false;
        m_viewportFocused = false;
    }
    ImGui::End();
}

void EditorShell::buildInspector(render::Renderer& renderer) {
    if (ImGui::Begin("Inspector")) {
        const ImGuiIO& io = ImGui::GetIO();

        if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(io.Framerate),
                        io.Framerate > 0.0f ? 1000.0 / static_cast<double>(io.Framerate) : 0.0);
            // Hover/focus are shown because they are what decides where input goes: a fly camera
            // that will not fly is almost always a viewport that is not hovered.
            ImGui::Text("viewport %u x %u px%s%s", m_viewportWidth, m_viewportHeight,
                        m_viewportHovered ? "  hovered" : "", m_viewportFocused ? "  focused" : "");
            ImGui::Text("scene target %u x %u px", renderer.width(), renderer.height());
            ImGui::PlotLines("##frameTimes", m_frameTimesMs.data(),
                             static_cast<int>(m_frameTimesMs.size()),
                             static_cast<int>(m_frameTimeCursor), "frame time (ms)", 0.0f,
                             kFrameTimePlotCeilingMs, ImVec2(0.0f, 60.0f));
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("position", &m_camera.position.x, 0.05f);
            // Degrees at the surface, radians underneath: the Camera's contract is radians, and
            // a UI that shows them would be unreadable.
            float yawDegrees = glm::degrees(m_camera.yaw);
            if (ImGui::DragFloat("yaw", &yawDegrees, 0.5f)) {
                m_camera.yaw = glm::radians(yawDegrees);
            }
            float pitchDegrees = glm::degrees(m_camera.pitch);
            // Short of ±90: at the poles forward() and world-up are parallel and right()
            // degenerates, which is the same limit Camera::look() clamps to.
            if (ImGui::DragFloat("pitch", &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f",
                                 ImGuiSliderFlags_AlwaysClamp)) {
                m_camera.pitch = glm::radians(pitchDegrees);
            }
            float fovDegrees = glm::degrees(m_camera.fovY);
            if (ImGui::DragFloat("fov Y", &fovDegrees, 0.5f, 30.0f, 110.0f, "%.1f",
                                 ImGuiSliderFlags_AlwaysClamp)) {
                m_camera.fovY = glm::radians(fovDegrees);
            }
            ImGui::DragFloat("move speed", &m_camera.moveSpeed, 0.1f, 0.5f, 50.0f, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp);
            // Edited in place: Renderer::clearColor is a plain per-frame knob, not state with an
            // invariant to protect.
            ImGui::ColorEdit4("clear color", renderer.clearColor);
        }

        if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < m_scene.size(); ++i) {
                SceneObject& object = m_scene[i];
                // Pushed by index, not by name: two objects sharing a name would otherwise share
                // every widget's ID and so every widget's state.
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNodeEx(object.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat3("position", &object.position.x, 0.05f);
                    ImGui::DragFloat3("rotation", &object.eulerDegrees.x, 1.0f);
                    ImGui::DragFloat("scale", &object.scale, 0.01f, 0.1f, 10.0f, "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::ColorEdit4("base color", &object.baseColor.x);
                    ImGui::Checkbox("rotating", &object.rotating);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void EditorShell::updateCameraInput(float deltaSeconds) {
    // Drained every frame whether or not it is used: SDL accumulates relative motion regardless,
    // so reading it only at the start of a look would apply everything the mouse did beforehand
    // as one jump.
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    SDL_GetRelativeMouseState(&relativeX, &relativeY);

    if (!m_looking) {
        if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            // Relative mode hides and un-anchors the cursor, so a look can keep turning past the
            // edge of the window -- and, on release, the cursor is back where it was.
            SDL_SetWindowRelativeMouseMode(m_window, true);
        }
        // Returning on the entering frame too, and that is the point: the deltas read above are
        // from *before* relative mode existed -- ordinary cursor travel on the way to the click.
        // Applying them would snap the camera by however far the mouse had moved this frame. The
        // look starts from the next frame, with deltas that mean what this code thinks they mean.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_looking = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        // Symmetrically: the release frame's deltas are the last of the look, but the camera has
        // already been handed back to the cursor. Dropping them costs nothing visible.
        return;
    }

    // Everything past here is gated on the look button being held, which subsumes the
    // io.WantCaptureKeyboard rule: with the button up the App never reads the keyboard at all, so
    // a focused Inspector field can never lose a keystroke to the camera.

    // Screen y grows downward, camera pitch grows upward.
    m_camera.look(relativeX * kLookRadiansPerPixel, -relativeY * kLookRadiansPerPixel);

    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec3 move{0.0f};
    move.z += keys[SDL_SCANCODE_W] ? 1.0f : 0.0f;
    move.z -= keys[SDL_SCANCODE_S] ? 1.0f : 0.0f;
    move.x += keys[SDL_SCANCODE_D] ? 1.0f : 0.0f;
    move.x -= keys[SDL_SCANCODE_A] ? 1.0f : 0.0f;
    move.y += keys[SDL_SCANCODE_E] ? 1.0f : 0.0f;
    move.y -= keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f;
    if (move != glm::vec3{0.0f}) {
        // Normalised so that holding two keys is not faster than holding one.
        m_camera.move(glm::normalize(move) * (m_camera.moveSpeed * deltaSeconds));
    }
}

} // namespace lmx::app
