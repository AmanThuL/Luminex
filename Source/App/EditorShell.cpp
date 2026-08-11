//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShell.cpp
/// @brief Implements the docked editor UI, input, and scene interaction.
//----------------------------------------------------------------------------------------------------------------------

#include "App/EditorShell.h"

#include "App/GraphInspectorModel.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4ImGui.h"
#include "Render/GraphDump.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

// DockBuilder is an internal ImGui API; contain the unstable include here.
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
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

// The rolling history still samples every retired frame; only the changing text is held this long.
constexpr float kPassTimingRefreshSeconds = 0.25f;

constexpr float kMinLightDirectionLength = 1e-5f;

// A non-empty percentile window is required by ExposureResolve.slang's weighted average.
constexpr float kMinExposurePercentileGap = 1.0f;

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

    ExposureResetContext initial = self->m_exposureContext;
    initial.sceneId = initialScene;
    self->m_exposureResetPending = shouldResetExposure(self->m_exposureContext, initial);
    self->m_exposureContext = initial;

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
        // Keep the prior targets and restart the debounce to avoid retrying every frame. Failure
        // means the resize never took effect, so m_exposureContext is left naming the old extent
        // and shouldResetExposure() is never asked about this attempt at all.
        m_stableFrames = 0;
        LMX_LOG_ERROR("viewport resize to {}x{} failed: {}", m_viewportWidth, m_viewportHeight,
                      resized.error().message);
        return;
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
}

//======================================================================================================================
void EditorShell::buildUI(rhi::Device& device, render::Renderer& renderer, float deltaSeconds,
                          const FrameRecordRing& frameRecords) {
    m_frameTimesMs[m_frameTimeCursor] = deltaSeconds * 1000.0f;
    m_frameTimeCursor = (m_frameTimeCursor + 1) % m_frameTimesMs.size();
    updatePassTimingDisplay(deltaSeconds, frameRecords);

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_buildDefaultLayout) {
        m_buildDefaultLayout = false;
        buildDefaultLayout(dockspaceId);
    }

    buildViewport(renderer);
    buildInspector(device, renderer);
    buildGraphInspector(frameRecords);
    // Input consumes this frame's hover state and Inspector edits.
    updateCameraInput(deltaSeconds);
}

//======================================================================================================================
void EditorShell::updatePassTimingDisplay(float deltaSeconds, const FrameRecordRing& frameRecords) {
    if (m_passTimingsPaused) {
        return;
    }

    const RetainedFrame* newest = frameRecords.newestTimedFrame();
    if (newest == nullptr) {
        return;
    }

    const bool scheduleChanged =
        m_passTimingHistory.addFrame(newest->record.frameId, newest->timings);
    m_passTimingRefreshSeconds += deltaSeconds;
    if (scheduleChanged || m_displayedPassTimings.empty() ||
        m_passTimingRefreshSeconds >= kPassTimingRefreshSeconds) {
        m_displayedPassTimings = m_passTimingHistory.summaries();
        m_passTimingRefreshSeconds = 0.0f;
    }
}

//======================================================================================================================
render::SceneView EditorShell::sceneView() {
    render::SceneView view = m_activeScene->view(m_drawItems, m_shadowFilter, m_wireframe);
    // Exposure is a shell knob rather than scene data, so it is applied after the scene has
    // described itself -- the same way the wireframe and shadow-filter settings are.
    view.exposureEv = m_exposureEv;
    view.autoExposureEnabled = m_autoExposureEnabled;
    // exposureReset is left at SceneView's default (false); main.cpp sets it from
    // consumeExposureReset() before declaring passes.
    view.exposureLowPercentile = m_exposureLowPercentile;
    view.exposureHighPercentile = m_exposureHighPercentile;
    view.exposureTargetGrey = m_exposureTargetGrey;
    view.exposureEvMin = m_exposureEvMin;
    view.exposureEvMax = m_exposureEvMax;
    view.exposureCompensationEv = m_exposureCompensationEv;
    view.bloomEnabled = m_bloomEnabled;
    view.bloomThreshold = m_bloomThreshold;
    view.bloomIntensity = m_bloomIntensity;
    return view;
}

//======================================================================================================================
bool EditorShell::consumeExposureReset() {
    const bool pending = m_exposureResetPending;
    m_exposureResetPending = false;
    return pending;
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
        // Six stops each way: enough to drive a scene to black or to the tone map's shoulder,
        // which is the whole range a manual exposure control is useful over here.
        ImGui::SliderFloat("Exposure (EV)", &m_exposureEv, -6.0f, 6.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        // Off->on is a reset trigger (spec 9): the feedback loop has produced nothing yet, so the
        // first auto frame has to start from the manual EV exactly like a fresh scene would.
        // On->off is not: shouldResetExposure() only fires on the false->true edge.
        if (ImGui::Checkbox("Auto exposure", &m_autoExposureEnabled)) {
            ExposureResetContext candidate = m_exposureContext;
            candidate.autoExposureEnabled = m_autoExposureEnabled;
            if (shouldResetExposure(m_exposureContext, candidate)) {
                m_exposureResetPending = true;
            }
            m_exposureContext = candidate;
        }
        if (m_autoExposureEnabled) {
            ImGui::SliderFloat("Low percentile", &m_exposureLowPercentile, 0.0f,
                               m_exposureHighPercentile - kMinExposurePercentileGap, "%.0f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("High percentile", &m_exposureHighPercentile,
                               m_exposureLowPercentile + kMinExposurePercentileGap, 100.0f, "%.0f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Target grey", &m_exposureTargetGrey, 0.01f, 1.0f, "%.3f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Auto EV min", &m_exposureEvMin, -12.0f, m_exposureEvMax, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Auto EV max", &m_exposureEvMax, m_exposureEvMin, 12.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Exposure compensation", &m_exposureCompensationEv, -6.0f, 6.0f,
                               "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::Checkbox("Bloom", &m_bloomEnabled);
        if (m_bloomEnabled) {
            ImGui::SliderFloat("Bloom threshold", &m_bloomThreshold, 0.0f, 10.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Bloom intensity", &m_bloomIntensity, 0.0f, 2.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
        }
        // Off gives every transient its own memory. Nothing about the image changes -- a
        // transient cannot be read before it is written -- so what this compares is cost.
        ImGui::Checkbox("Transient pooling", &m_poolingEnabled);
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

namespace {

//======================================================================================================================
std::string_view passKindLabel(render::PassKind kind) {
    switch (kind) {
    case render::PassKind::Raster:
        return "raster";
    case render::PassKind::Compute:
        return "compute";
    case render::PassKind::Copy:
        return "copy";
    }
    return "raster";
}

//======================================================================================================================
std::string_view cullReasonLabel(render::CullReason reason) {
    switch (reason) {
    case render::CullReason::ProducesNothing:
        return "produces nothing";
    case render::CullReason::NoSinkReachesIt:
        return "no sink reaches it";
    }
    return "no sink reaches it";
}

//======================================================================================================================
// Shared by the scheduled and culled sections below, so a pass reads identically in both and only
// the reason and schedule position differ.
void drawPassRow(const app::GraphInspectorPassRow& pass, std::optional<uint32_t> scheduleOrder) {
    std::string header =
        std::format("p{} {} \"{}\"", pass.index, passKindLabel(pass.kind), pass.label);
    if (scheduleOrder) {
        header = std::format("#{} {}", *scheduleOrder, header);
    }
    if (pass.gpuMilliseconds) {
        header += std::format(" -- {:.3f} ms", *pass.gpuMilliseconds);
    }
    if (pass.cullReason) {
        header += std::format(" -- culled: {}", cullReasonLabel(*pass.cullReason));
    }
    // TreeNode with no explicit ID derives one from the whole label, so a label that changes every
    // frame (the GPU time above) would reopen a fresh, always-collapsed node each frame. "###"
    // tells ImGui to hash only what follows it for the ID while still displaying everything before
    // it, so the visible text can keep changing while the node's open/closed state stays put.
    header += std::format("###p{}", pass.index);
    ImGui::PushID(static_cast<int>(pass.index));
    if (ImGui::TreeNode(header.c_str())) {
        for (const app::GraphInspectorUseRow& use : pass.uses) {
            std::string line = std::format("{} r{} \"{}\" v{}", render::roleName(use.role),
                                           use.resource, use.resourceName, use.version);
            if (!use.rangeText.empty()) {
                line += std::format(" {}", use.rangeText);
            }
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

//======================================================================================================================
void EditorShell::buildGraphInspector(const FrameRecordRing& frameRecords) {
    if (!ImGui::Begin("Render Graph")) {
        ImGui::End();
        return;
    }

    const RetainedFrame* newest = frameRecords.newestTimedFrame();
    if (newest == nullptr) {
        // Nothing has retired yet -- true for the first few frames of a run, and not an error.
        ImGui::TextUnformatted("no retired frame yet");
        ImGui::End();
        return;
    }

    const app::GraphInspectorModel model =
        app::buildGraphInspectorModel(newest->record, newest->timings);

    ImGui::Text("frame %llu -- pooling %s", static_cast<unsigned long long>(model.frameId),
                model.poolingEnabled ? "on" : "off");
    ImGui::Text("transients: requested %llu B, high-water %llu B, saved %llu B",
                static_cast<unsigned long long>(model.memory.requested),
                static_cast<unsigned long long>(model.memory.highWater),
                static_cast<unsigned long long>(model.memory.aliasSavings));

    if (ImGui::Button("Dump frame")) {
        const std::string filename = std::format("graph-dump-frame-{}.txt", model.frameId);
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (file) {
            file << render::dumpCompiledFrame(newest->record);
            LMX_LOG_INFO("render-graph frame {} dumped to '{}'", model.frameId,
                         std::filesystem::absolute(filename).string());
        } else {
            LMX_LOG_ERROR("Render Graph panel: cannot open '{}' for writing", filename);
        }
    }

    if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const app::GraphInspectorResourceRow& resource : model.resources) {
            const std::string line =
                resource.kind == render::GraphResourceKind::Texture
                    ? std::format("r{} texture \"{}\" {}", resource.index, resource.name,
                                  render::formatName(resource.format))
                    : std::format("r{} buffer \"{}\"", resource.index, resource.name);
            ImGui::TextUnformatted(line.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Schedule", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (uint32_t order = 0; order < model.schedule.size(); ++order) {
            drawPassRow(model.passes[model.schedule[order]], order);
        }
    }

    if (ImGui::CollapsingHeader("Culled passes")) {
        for (const app::GraphInspectorPassRow& pass : model.passes) {
            if (pass.cullReason) {
                drawPassRow(pass, std::nullopt);
            }
        }
    }

    if (ImGui::CollapsingHeader("Transitions")) {
        for (const app::GraphInspectorTransitionRow& transition : model.transitions) {
            if (transition.aliasedFrom) {
                ImGui::Text("before p%u %s alias-of r%u", transition.beforePass,
                            transition.description.c_str(), *transition.aliasedFrom);
            } else {
                ImGui::Text("before p%u %s", transition.beforePass, transition.description.c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Transients")) {
        for (const app::GraphInspectorTransientRow& transient : model.transients) {
            if (!transient.used) {
                ImGui::Text("r%u \"%s\" unused", transient.resource,
                            transient.resourceName.c_str());
                continue;
            }
            ImGui::Text("r%u \"%s\" passes p%u..p%u offset %llu size %llu align %llu%s",
                        transient.resource, transient.resourceName.c_str(), transient.firstPass,
                        transient.lastPass, static_cast<unsigned long long>(transient.offset),
                        static_cast<unsigned long long>(transient.size),
                        static_cast<unsigned long long>(transient.alignment),
                        transient.aliases ? " aliased" : "");
        }
    }

    ImGui::End();
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
    // A scene switch is a reset trigger (spec 9): the previous scene's metering has nothing to say
    // about the new one's content.
    ExposureResetContext candidate = m_exposureContext;
    candidate.sceneId = id;
    if (shouldResetExposure(m_exposureContext, candidate)) {
        m_exposureResetPending = true;
    }
    m_exposureContext = candidate;
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
            ImGui::Checkbox("Pause GPU timings", &m_passTimingsPaused);
            ImGui::TextDisabled("60-frame average -- updates 4x/s");
            // Schedule changes reset every series together, so these rows never average timings
            // from unlike graph shapes. The exact newest frame remains available in Render Graph.
            if (m_displayedPassTimings.empty()) {
                ImGui::TextDisabled("waiting for retired GPU timings");
            }
            for (const PassTimingSummary& timing : m_displayedPassTimings) {
                ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.averageGpuMilliseconds);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("latest %.3f ms\nrange %.3f..%.3f ms\n%zu samples",
                                      timing.latestGpuMilliseconds, timing.minimumGpuMilliseconds,
                                      timing.maximumGpuMilliseconds, timing.sampleCount);
                }
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
