#include "App/EditorShell.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4ImGui.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// DockBuilder is an internal ImGui API; contain the unstable include here.
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>

namespace lmx::app {

namespace {

// Debounce resize-driven GPU stalls until the dock splitter settles.
constexpr uint32_t kResizeDebounceFrames = 10;

constexpr float kInspectorDockFraction = 0.22f;

// Tuned so a roughly screen-wide drag turns the camera 180 degrees.
constexpr float kLookRadiansPerPixel = 0.0025f;

// A fixed ceiling keeps the frame-time graph comparable over time.
constexpr float kFrameTimePlotCeilingMs = 33.3f;

constexpr float kMinLightDirectionLength = 1e-5f;

//======================================================================================================================
// Builds the first-run layout when no persisted ImGui layout exists.
void buildDefaultLayout(ImGuiID dockspaceId) {
    // DockSpaceOverViewport already created this node; DockBuilder needs a fresh owned node.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // Split ratios derive from the current node size, so size it first.
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID inspectorId = 0;
    ImGuiID viewportId = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, kInspectorDockFraction, &inspectorId,
                                &viewportId);
    ImGui::DockBuilderDockWindow("Inspector", inspectorId);
    ImGui::DockBuilderDockWindow("Viewport", viewportId);
    ImGui::DockBuilderFinish(dockspaceId);
}

//======================================================================================================================
// ImGui reports panel sizes in points; the scene target is sized in pixels.
uint32_t toPixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(points * scale, 0.0f) + 0.5f);
}

} // namespace

//======================================================================================================================
render::Camera cameraFromScene(const engine::SceneCamera& sceneCamera) {
    render::Camera camera;
    camera.position = sceneCamera.position;
    camera.yaw = sceneCamera.yaw;
    camera.pitch = sceneCamera.pitch;
    camera.fovY = sceneCamera.fovY;
    camera.nearZ = sceneCamera.nearZ;
    camera.farZ = sceneCamera.farZ;
    return camera;
}

//======================================================================================================================
EditorShell::EditorShell(SDL_Window* window, engine::SceneLibrary& library)
    : m_window(window), m_library(library) {}

//======================================================================================================================
std::unique_ptr<EditorShell> EditorShell::create(SDL_Window* window, rhi::Device& device,
                                                 engine::SceneLibrary& library,
                                                 engine::SceneId initialScene) {
    LMX_ASSERT(window != nullptr, "EditorShell::create: window must not be null");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Platform viewports need additional OS windows and swapchains; this RHI owns one.
    ImGui::StyleColorsDark();

    const bool hadIniFile =
        io.IniFilename != nullptr && std::filesystem::exists(std::filesystem::path(io.IniFilename));

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

    std::unique_ptr<EditorShell> self(new EditorShell(window, library));

    // Startup needs a renderable scene; later switch failures can retain the current one.
    auto scene = library.get(initialScene);
    if (!scene) {
        LMX_LOG_ERROR("EditorShell::create: initial scene '{}' failed to load: {}",
                      library.entry(initialScene).displayName, scene.error().message);
        ImGui_ImplSDL3_Shutdown();
        rhi::metal4::imguiShutdown();
        ImGui::DestroyContext();
        return nullptr;
    }
    self->m_activeSceneId = initialScene;
    self->m_activeScene = *scene;
    self->m_camera = cameraFromScene(self->m_activeScene->initialCamera);

    self->m_buildDefaultLayout = !hadIniFile;
    LMX_LOG_INFO("editor shell: {} (scene '{}', {} objects)",
                 hadIniFile ? "restoring the docked layout from imgui.ini"
                            : "no imgui.ini -- building the default docked layout",
                 self->m_activeScene->name, self->m_activeScene->objects.size());
    return self;
}

//======================================================================================================================
EditorShell::~EditorShell() {
    // Backends unregister from the ImGui context, so destroy the context last.
    ImGui_ImplSDL3_Shutdown();
    rhi::metal4::imguiShutdown();
    ImGui::DestroyContext();
}

//======================================================================================================================
void EditorShell::applyPendingViewportResize(rhi::Device& device, render::Renderer& renderer) {
    if (m_viewportWidth == 0 || m_viewportHeight == 0) {
        return;
    }
    if (m_viewportWidth == renderer.width() && m_viewportHeight == renderer.height()) {
        return;
    }
    if (m_stableFrames < kResizeDebounceFrames) {
        return;
    }

    // In-flight encoders and residency sets retain the old targets; drain before replacement.
    device.waitIdle();
    // Remove the old target from ImGui's persistent residency set before freeing it.
    rhi::metal4::imguiForgetTexture(renderer.colorTarget());
    if (auto resized = renderer.resize(m_viewportWidth, m_viewportHeight); !resized) {
        // Keep the prior targets and restart the debounce to avoid retrying every frame.
        m_stableFrames = 0;
        LMX_LOG_ERROR("viewport resize to {}x{} failed: {}", m_viewportWidth, m_viewportHeight,
                      resized.error().message);
        return;
    }
    LMX_LOG_INFO("scene target resized to {}x{} px", m_viewportWidth, m_viewportHeight);
}

//======================================================================================================================
void EditorShell::buildUI(rhi::Device& device, render::Renderer& renderer, float deltaSeconds) {
    m_frameTimesMs[m_frameTimeCursor] = deltaSeconds * 1000.0f;
    m_frameTimeCursor = (m_frameTimeCursor + 1) % m_frameTimesMs.size();

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_buildDefaultLayout) {
        m_buildDefaultLayout = false;
        buildDefaultLayout(dockspaceId);
    }

    buildViewport(renderer);
    buildInspector(device, renderer);
    // Input consumes this frame's hover state and Inspector edits.
    updateCameraInput(deltaSeconds);
}

//======================================================================================================================
render::SceneView EditorShell::sceneView() {
    return m_activeScene->view(m_drawItems, m_shadowFilter, m_wireframe);
}

//======================================================================================================================
void EditorShell::buildViewport(render::Renderer& renderer) {
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
            // Stretch the last good target while a resize is pending.
            ImGui::Image(rhi::metal4::imguiTextureID(renderer.colorTarget()), available);
        }
    } else {
        m_viewportHovered = false;
        m_viewportFocused = false;
    }
    ImGui::End();
}

//======================================================================================================================
void EditorShell::buildSceneCombo(rhi::Device& device) {
    const std::span<const engine::SceneEntry> entries = m_library.entries();
    if (ImGui::BeginCombo("Scene", m_library.entry(m_activeSceneId).displayName.data())) {
        for (size_t i = 0; i < entries.size(); ++i) {
            const engine::SceneEntry& entry = entries[i];
            ImGui::PushID(static_cast<int>(i));
            if (!entry.available) {
                ImGui::BeginDisabled();
            }
            // Show availability hints inline because disabled entries cannot be hovered reliably.
            const std::string label =
                entry.hint.empty() ? std::string(entry.displayName)
                                   : std::string(entry.displayName) + " (" + entry.hint + ")";
            if (ImGui::Selectable(label.c_str(), entry.id == m_activeSceneId)) {
                selectScene(device, entry.id);
            }
            if (!entry.available) {
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

//======================================================================================================================
void EditorShell::buildLightsSection() {
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < std::size(m_activeScene->lights); ++i) {
            render::DirectionalLight& light = m_activeScene->lights[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Light %d", static_cast<int>(i));
            ImGui::ColorEdit3("strength", &light.strength.x);
            glm::vec3 direction = light.direction;
            if (ImGui::DragFloat3("direction", &direction.x, 0.01f)) {
                // Reject zero directions before normalization; they would poison shadow and N.L
                // math.
                if (glm::length(direction) > kMinLightDirectionLength) {
                    light.direction = glm::normalize(direction);
                }
            }
            ImGui::PopID();
        }
    }
}

//======================================================================================================================
void EditorShell::buildRenderSettingsSection() {
    if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe", &m_wireframe);
        int filterIndex = static_cast<int>(m_shadowFilter);
        constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
        if (ImGui::Combo("Shadow filter", &filterIndex, kFilterNames,
                         static_cast<int>(std::size(kFilterNames)))) {
            m_shadowFilter = static_cast<render::ShadowFilter>(filterIndex);
        }
    }
}

//======================================================================================================================
void EditorShell::buildObjectsSection() {
    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < m_activeScene->objects.size(); ++i) {
            engine::SceneObject& object = m_activeScene->objects[i];
            // Index IDs keep duplicate object names from sharing widget state.
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNodeEx(object.name.c_str())) {
                ImGui::DragFloat3("position", &object.position.x, 0.05f);
                ImGui::DragFloat3("rotation", &object.eulerDegrees.x, 1.0f);
                ImGui::DragFloat3("scale", &object.scale.x, 0.01f, 0.01f, 100.0f, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

//======================================================================================================================
void EditorShell::selectScene(rhi::Device& device, engine::SceneId id) {
    if (id == m_activeSceneId) {
        return;
    }
    // In-flight frames may still reference the current scene's meshes and textures.
    device.waitIdle();
    auto scene = m_library.get(id);
    if (!scene) {
        // A failed switch leaves the current scene renderable.
        LMX_LOG_ERROR("scene '{}' failed to load: {}", m_library.entry(id).displayName,
                      scene.error().message);
        return;
    }
    m_activeSceneId = id;
    m_activeScene = *scene;
    // Camera pose is scene-local; render settings remain editor-local.
    m_camera = cameraFromScene(m_activeScene->initialCamera);
    LMX_LOG_INFO("scene switched to '{}' ({} objects)", m_activeScene->name,
                 m_activeScene->objects.size());
}

//======================================================================================================================
void EditorShell::buildInspector(rhi::Device& device, render::Renderer& renderer) {
    if (ImGui::Begin("Inspector")) {
        const ImGuiIO& io = ImGui::GetIO();

        buildSceneCombo(device);

        if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(io.Framerate),
                        io.Framerate > 0.0f ? 1000.0 / static_cast<double>(io.Framerate) : 0.0);
            ImGui::Text("viewport %u x %u px%s%s", m_viewportWidth, m_viewportHeight,
                        m_viewportHovered ? "  hovered" : "", m_viewportFocused ? "  focused" : "");
            ImGui::Text("scene target %u x %u px", renderer.width(), renderer.height());
            ImGui::PlotLines("##frameTimes", m_frameTimesMs.data(),
                             static_cast<int>(m_frameTimesMs.size()),
                             static_cast<int>(m_frameTimeCursor), "frame time (ms)", 0.0f,
                             kFrameTimePlotCeilingMs, ImVec2(0.0f, 60.0f));
            // Every pass of the newest retired frame, in the order the graph ran them. The list is
            // empty until a frame retires, which is a fact about the counters rather than a gap.
            for (const rhi::PassTiming& timing : device.passTimings()) {
                ImGui::Text("%s: %.2f ms", timing.label.c_str(), timing.gpuMilliseconds);
            }
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("position", &m_camera.position.x, 0.05f);
            // Present angles in degrees while Camera stores radians.
            float yawDegrees = glm::degrees(m_camera.yaw);
            if (ImGui::DragFloat("yaw", &yawDegrees, 0.5f)) {
                m_camera.yaw = glm::radians(yawDegrees);
            }
            float pitchDegrees = glm::degrees(m_camera.pitch);
            // Avoid the poles where forward and world-up become parallel.
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
            ImGui::ColorEdit4("clear color", renderer.clearColor);
        }

        buildLightsSection();
        buildRenderSettingsSection();
        buildObjectsSection();
    }
    ImGui::End();
}

//======================================================================================================================
void EditorShell::updateCameraInput(float deltaSeconds) {
    // Drain SDL motion every frame so pre-look cursor travel cannot accumulate into a jump.
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    SDL_GetRelativeMouseState(&relativeX, &relativeY);

    if (!m_looking) {
        if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            SDL_SetWindowRelativeMouseMode(m_window, true);
        }
        // Ignore the entry-frame delta because it predates relative mode.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_looking = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        return;
    }

    // Screen Y grows downward while camera pitch grows upward.
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
        // Normalize diagonal movement to preserve speed.
        m_camera.move(glm::normalize(move) * (m_camera.moveSpeed * deltaSeconds));
    }
}

} // namespace lmx::app
