//----------------------------------------------------------------------------------------------------------------------
/// @file main.cpp
/// @brief Runs the windowed editor or offscreen screenshot application.
//----------------------------------------------------------------------------------------------------------------------

#include "App/ConsoleLogSink.h"
#include "App/EditorShell.h"
#include "App/Measurement.h"
#include "App/Model/AppOptions.h"
#include "App/Model/FrameRecordRing.h"
#include "App/Model/SceneDefaults.h"
#include "App/Screenshot.h"
#include "Core/Log.h"
#include "Core/Parse.h"
#include "Engine/Catalog/SceneLibrary.h"
#include "Render/FrameDeclaration.h"
#include "Render/RenderGraph.h"
#include "Render/Renderer.h"
#include "Render/RhiLog.h"
#include <rojoRHI/CaptureSchema.h>
#include <rojoRHI/Metal4/Metal4Capture.h>
#include <rojoRHI/Metal4/Metal4ImGui.h>
#include <rojoRHI/RHI.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// SDL window dimensions are logical points.
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// UI colors are display-referred sRGB, straight-alpha blended in encoded space on BGRA8Unorm.
// Encoded 1.0 is SDR white; the UI never boosts it. This clear is written verbatim into dock gaps.
// Layout uses points and the per-window framebuffer scale controls font rasterization.
// Detached ImGui platform windows own separate BGRA8Unorm layers and remain SDR.
constexpr float kUiClearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};

// Capture paths are relative to the process working directory unless overridden.
constexpr std::string_view kCapturePath = "luminex-frame.gputrace";

constexpr uint64_t kResizeDownFrame = 100;
constexpr uint64_t kResizeUpFrame = 200;
constexpr int kResizeDownWidth = 800;
constexpr int kResizeDownHeight = 600;

//======================================================================================================================
// Zero disables frame-triggered behavior.
uint64_t frameNumberFromEnv(const char* name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return 0;
    }
    const std::string_view text(raw);
    uint64_t frames = 0;
    if (!lmx::parseNumber(text, frames)) {
        LMX_LOG_WARN("{}='{}' is not a number; ignoring it", name, text);
        return 0;
    }
    return frames;
}

//======================================================================================================================
// Non-positive, missing, or unparseable leaves dynamic resolution at its Inspector default (off).
float dynamicResolutionBudgetFromEnv() {
    const char* raw = std::getenv("LMX_DYNAMIC_RESOLUTION_BUDGET_MS");
    if (raw == nullptr) {
        return 0.0f;
    }
    const std::string_view text(raw);
    float budget = 0.0f;
    if (!lmx::parseNumber(text, budget) || !(budget > 0.0f)) {
        LMX_LOG_WARN("LMX_DYNAMIC_RESOLUTION_BUDGET_MS='{}' is not a positive number; ignoring it",
                     text);
        return 0.0f;
    }
    return budget;
}

//======================================================================================================================
// RHI and ImGui objects are scoped inside the lifetime of the SDL-owned Metal layer. Declaration
// order keeps the device alive until every dependent object has been released.
int run(SDL_Window* window, void* metalLayer, const lmx::app::AppOptions& options,
        const std::shared_ptr<lmx::app::ConsoleLog>& consoleLog) {
    auto device = rojoRHI::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    lmx::engine::SceneLibrary sceneLibrary(**device, options.labInstances, options.labOccluders,
                                           options.labLights, options.labLightPile);

    // Swapchain dimensions follow the backing store, not logical window points.
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) {
        LMX_LOG_ERROR("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return 1;
    }

    auto swapchain = (*device)->createSwapchain({.nativeLayer = metalLayer,
                                                 .width = static_cast<uint32_t>(pixelWidth),
                                                 .height = static_cast<uint32_t>(pixelHeight),
                                                 .format = rojoRHI::Format::BGRA8Unorm});
    if (!swapchain) {
        LMX_LOG_ERROR("createSwapchain failed: {}", swapchain.error().message);
        return 1;
    }

    // Points size a maximized window to the display's usable bounds; pixels are what the
    // swapchain above was created at, following SDL_GetWindowSizeInPixels.
    int pointWidth = 0;
    int pointHeight = 0;
    SDL_GetWindowSize(window, &pointWidth, &pointHeight);
    LMX_LOG_INFO("window: {}x{} points, swapchain: {}x{} pixels BGRA8Unorm", pointWidth,
                 pointHeight, pixelWidth, pixelHeight);

    // Per-frame graphs die before the pool; the renderer and shell unwind before its resources.
    lmx::render::TransientPool transientPool(**device);

    // The viewport adopts its panel size after the first UI layout.
    auto renderer = lmx::render::Renderer::create(**device, static_cast<uint32_t>(pixelWidth),
                                                  static_cast<uint32_t>(pixelHeight));
    if (!renderer) {
        LMX_LOG_ERROR("Renderer::create failed: {}", renderer.error().message);
        return 1;
    }
    LMX_LOG_INFO("display: {}", lmx::render::describe((*renderer)->displayDomain()));
    (*renderer)->clearColor[0] = lmx::app::kSceneClearGray;
    (*renderer)->clearColor[1] = lmx::app::kSceneClearGray;
    (*renderer)->clearColor[2] = lmx::app::kSceneClearGray;
    (*renderer)->clearColor[3] = 1.0f;

    // EditorShell must release ImGui resources before the renderer and device.
    auto shell = lmx::app::EditorShell::create(window, **device, sceneLibrary, options.initialScene,
                                               consoleLog);
    if (!shell) {
        return 1;
    }

    if (auto primed = shell->primeTemporal(options); !primed) {
        LMX_LOG_ERROR("startup lighting failed: {}", primed.error().message);
        return 1;
    }
    shell->actions().configureCapture(rojoRHI::metal4::captureAvailable());

    const float dynamicResolutionBudget = dynamicResolutionBudgetFromEnv();
    if (dynamicResolutionBudget > 0.0f) {
        shell->primeDynamicResolution(true, dynamicResolutionBudget);
        LMX_LOG_INFO("LMX_DYNAMIC_RESOLUTION_BUDGET_MS={}: dynamic resolution starts on",
                     dynamicResolutionBudget);
    }

    const uint64_t maxFrames = frameNumberFromEnv("LMX_MAX_FRAMES");
    if (maxFrames > 0) {
        LMX_LOG_INFO("LMX_MAX_FRAMES={}: exiting after that many frames", maxFrames);
    }
    const uint64_t captureAtFrame = frameNumberFromEnv("LMX_CAPTURE_AT_FRAME");
    if (captureAtFrame > 0) {
        LMX_LOG_INFO("LMX_CAPTURE_AT_FRAME={}: will capture that frame", captureAtFrame);
    }
    // beginCapture owns capture-path validation.
    const char* capturePathEnv = std::getenv("LMX_CAPTURE_PATH");
    const std::string capturePath = (capturePathEnv != nullptr && *capturePathEnv != '\0')
                                        ? std::string(capturePathEnv)
                                        : std::string(kCapturePath);
    LMX_LOG_INFO("press 'c' to capture one frame to {} (needs MTL_CAPTURE_ENABLED=1)", capturePath);

    uint64_t frameIndex = 0;
    uint64_t presentedFrames = 0;
    // The frames an observer can still ask about: the three that can be in flight, plus the one
    // whose timings the next beginFrame() publishes.
    lmx::app::FrameRecordRing frameRecords;
    uint64_t skippedFrames = 0;
    bool running = true;
    uint64_t previousTicksNs = SDL_GetTicksNS();
    // Accumulation in double avoids precision loss in the float shader time during long runs.
    double elapsedSeconds = 0.0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // ImGui must observe every event before input ownership is queried.
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // Platform viewports are real SDL windows, so the detached Render Graph raises the
                // same window events the main window does and only its id tells them apart.
                // Closing it must not end the run, and its window carries a real close button, so
                // the id filter is what keeps that button from quitting Luminex.
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    running = false;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                // Do not capture from key repeats or keyboard input owned by ImGui.
                if (event.key.key == SDLK_C && !event.key.repeat &&
                    !ImGui::GetIO().WantCaptureKeyboard) {
                    shell->actions().requestCapture();
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // The same id filter, and here it is load-bearing today: resizing the detached
                // Render Graph window would otherwise resize the one swapchain the main window
                // presents from. Minimized windows report a zero extent, which is invalid for a
                // swapchain.
                if (event.window.windowID == SDL_GetWindowID(window) && event.window.data1 > 0 &&
                    event.window.data2 > 0) {
                    (*swapchain)
                        ->resize(static_cast<uint32_t>(event.window.data1),
                                 static_cast<uint32_t>(event.window.data2));
                }
                break;
            default:
                break;
            }
        }
        // The menu's Quit reaches the same exit the window close button does, one frame after it
        // was chosen -- the frame that drew the menu still finishes normally.
        if (shell->actions().consumeQuit()) {
            running = false;
        }
        if (!running) {
            break;
        }

        ++frameIndex;

        if (maxFrames > 0) {
            // SDL_SetWindowSize is a silent no-op on a still-maximized window on macOS (returns
            // success, emits no event); SDL_RestoreWindow first is what lets this hook exercise
            // the down/up resize path whether the run started maximized or --windowed, where the
            // window was never maximized and the restore is a no-op.
            if (frameIndex == kResizeDownFrame) {
                SDL_RestoreWindow(window);
                SDL_SetWindowSize(window, kResizeDownWidth, kResizeDownHeight);
            } else if (frameIndex == kResizeUpFrame) {
                SDL_SetWindowSize(window, kWindowWidth, kWindowHeight);
            }
        }
        if (captureAtFrame > 0 && frameIndex == captureAtFrame) {
            shell->actions().requestCapture();
        }

        const uint64_t nowNs = SDL_GetTicksNS();
        const double deltaSecondsExact = static_cast<double>(nowNs - previousTicksNs) * 1e-9;
        previousTicksNs = nowNs;
        elapsedSeconds += deltaSecondsExact;
        const float deltaSeconds = static_cast<float>(deltaSecondsExact);
        const float timeSeconds = static_cast<float>(elapsedSeconds);

        // Resize can drain and replace scene targets, so it precedes the ImGui frame that uses
        // them.
        if (!shell->applyPendingViewportResize(**device, **renderer)) {
            (*device)->waitIdle();
            return 1;
        }

        // Acquire before ImGui::NewFrame so a dropped drawable cannot leave an open ImGui frame.
        auto target = (*swapchain)->acquireNextTexture();
        if (!target) {
            // Drawable starvation is transient; drop the frame without opening encoder state.
            ++skippedFrames;
            LMX_LOG_WARN("frame {} skipped: {}", frameIndex, target.error().message);
            if (maxFrames > 0 && frameIndex >= maxFrames) {
                running = false;
            }
            continue;
        }

        // Capture exactly one acquired frame. Consumed only past the acquire above, so a skipped
        // drawable retains the request; failed attempts are consumed rather than retried.
        bool capturingThisFrame = false;
        if (shell->actions().consumeCapture()) {
            capturingThisFrame = rojoRHI::metal4::beginCapture(**device, capturePath);
            auto& result = shell->actions().captureResult();
            std::error_code pathError;
            result.path = std::filesystem::absolute(capturePath, pathError).string();
            if (!capturingThisFrame) {
                result.status = lmx::app::ActionStatus::Failed;
                result.message = rojoRHI::metal4::captureFailureReason();
            } else {
                result.message = "Capturing GPU work; waiting for completion.";
            }
        }

        // The Metal backend prepares its frame before ImGui builds draw data and RHI encoding
        // begins.
        shell->prepareUIFrame();
        rojoRHI::metal4::imguiNewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        shell->buildUI(**device, **renderer, deltaSeconds, frameRecords);
        ImGui::Render();

        const auto measurementWaitStart = std::chrono::steady_clock::now();
        rojoRHI::CommandList& commands = (*device)->beginFrame();
        const auto measurementEncodeStart = std::chrono::steady_clock::now();
        shell->retireMeasurement((*device)->passTimingsFrame(), (*device)->passTimings());
        // The dynamic-resolution controller's attribution is by frame number, so this frame's
        // number is recorded as soon as it exists -- right after the beginFrame() that assigns it.
        shell->controllerDeclared((*device)->frameNumber());
        (*renderer)->timeSeconds = timeSeconds;

        // This frame will be declared (the drawable was acquired above), so the scene's animation
        // clock and camera-track follow advance now, before the SceneView below captures whatever
        // pose and object transforms result.
        shell->advanceFrameAnimation();
        if (auto prepared = shell->prepareSceneFrame((*device)->frameNumber()); !prepared) {
            LMX_LOG_ERROR("scene table preparation failed: {}", prepared.error().message);
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            return 1;
        }

        // Named rather than passed inline: the pass bodies borrow this view and run when the
        // graph executes, which is past the end of the statement that would hold a temporary.
        lmx::render::SceneView view = shell->sceneView();
        // Spec 9's four reset triggers, forwarded to Renderer::declarePasses: on the frame any of
        // them fires, it seeds the persistent exposure buffer with exp2(exposureEv) itself (an
        // ordinary compute dispatch), rather than main.cpp reading anything back -- the feedback
        // loop lives entirely on the GPU timeline.
        view.exposureReset = shell->consumeExposureReset();

        lmx::render::FrameDeclaration frame(transientPool, **renderer, commands, shell->camera(),
                                            view, shell->poolingEnabled());
        shell->retireVisibility(**renderer);
        shell->retireLighting(**renderer);
        shell->observeDeclaration(**renderer, (*device)->frameNumber());
        lmx::render::RenderGraph& graph = frame.graph();
        const lmx::render::GraphTexture displayColor =
            shell->declareSelection(graph, commands, frame.displayColor(), view, **renderer);
        const lmx::render::GraphTexture drawable =
            graph.importTexture(**target, rojoRHI::Format::BGRA8Unorm, "lmx.app.drawable");

        lmx::render::PassDesc ui;
        // Declaring the read is the whole ordering statement: it puts this pass after the display
        // pass and that target's transition to a shader read in front of it.
        ui.textureReads.push_back(displayColor);
        // This attachment layout must match the pipeline configured by imguiInit().
        ui.color = lmx::render::ColorAttachment{
            .handle = drawable,
            .clearColor = {kUiClearColor[0], kUiClearColor[1], kUiClearColor[2], kUiClearColor[3]}};
        // ImGui owns encoder state once it starts, so no engine draw follows it in this pass.
        graph.addPass("lmx.pass.ui", std::move(ui), [&commands](const lmx::render::PassResources&) {
            rojoRHI::metal4::imguiRender(commands);
        });
        // The drawable this frame presents, and the only sink the frame declares: everything the
        // renderer put in front of it is live because this pass reads it, so nothing here has to
        // repeat what the display transform already rooted.
        graph.presentTexture(lmx::render::nextVersion(drawable));

        // A frame that cannot validate is a mis-declared frame, which is programmer error: execute
        // aborts with the graph's own message rather than encoding a hazard.
        //
        // Retaining the record it answers with is what lets an observer describe a frame that has
        // already been submitted: the timings of a frame are readable only once it retires, several
        // frames after the declarations that explain them are gone.
        frameRecords.retain(frame.execute(), shell->frameMetrics(**renderer));
        // The frame's declarations are committed now that execute() has accepted them: the next
        // frame's motion is measured from here. A frame skipped for a missing drawable reaches
        // neither this nor advanceFrameAnimation() above.
        shell->commitFrame();
        // Published by this frame's beginFrame() and naming a frame that has already retired, which
        // is why the join is by number rather than by position.
        frameRecords.joinTimings((*device)->passTimingsFrame(), (*device)->passTimings());
        (*device)->endFrame(swapchain->get());
        const auto measurementEncodeEnd = std::chrono::steady_clock::now();
        if (const auto* measuredRecord = frameRecords.find((*device)->frameNumber())) {
            shell->recordMeasurementFrame((*device)->frameNumber(),
                                          std::chrono::duration<double, std::milli>(
                                              measurementEncodeStart - measurementWaitStart)
                                              .count(),
                                          std::chrono::duration<double, std::milli>(
                                              measurementEncodeEnd - measurementEncodeStart)
                                              .count(),
                                          measuredRecord->record);
        }
        if (shell->measurementNeedsRetirementWait()) {
            (*device)->waitIdle();
            (*renderer)->drainVisibilityAfterIdle();
            (*renderer)->drainLightingAfterIdle();
            shell->retireVisibility(**renderer);
            shell->retireLighting(**renderer);
        }
        ++presentedFrames;

        // Platform viewports follow the present because the ImGui backend renders each extra window
        // on the same device queue with the per-frame-slot allocator this frame just finished
        // encoding against; running them after endFrame keeps that slot's use strictly ordered. The
        // skipped-drawable path continues above without opening an ImGui frame, so it never reaches
        // here with stale platform draw data.
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        if (capturingThisFrame) {
            {
                rojoRHI::debug::SchemaContext ctx;
                ctx.sceneName = std::string(shell->activeSceneName());
                ctx.frameIndex = frameIndex;
                const glm::vec3 cameraPos = shell->camera().position;
                ctx.cameraPos = {cameraPos.x, cameraPos.y, cameraPos.z};
                ctx.boundingSphere = {view.boundingSphere.x, view.boundingSphere.y,
                                      view.boundingSphere.z, view.boundingSphere.w};
                const auto& light0 = view.lights[0];
                ctx.light0Direction = {light0.direction.x, light0.direction.y, light0.direction.z};
                ctx.light0Strength = {light0.strength.x, light0.strength.y, light0.strength.z};
                ctx.shadowFilter =
                    view.shadowFilter == lmx::render::ShadowFilter::PCSS ? "PCSS" : "PCF";
                rojoRHI::debug::CaptureSchema::instance().setContext(std::move(ctx));
            }
            // Captured work must complete before the trace document is finalized.
            (*device)->waitIdle();
            rojoRHI::metal4::endCapture();
            auto& result = shell->actions().captureResult();
            std::error_code outputError;
            const bool written = std::filesystem::is_directory(result.path, outputError);
            result.status =
                written ? lmx::app::ActionStatus::Succeeded : lmx::app::ActionStatus::Failed;
            result.message = written ? "GPU trace written."
                                     : "Capture completed without an output document. Check the "
                                       "output path and retry.";
        }

        if (maxFrames > 0 && frameIndex >= maxFrames) {
            running = false;
        }
    }

    // Establish one explicit idle boundary before dependent resources unwind.
    (*device)->waitIdle();
    (*renderer)->drainVisibilityAfterIdle();
    (*renderer)->drainLightingAfterIdle();
    shell->retireVisibility(**renderer);
    shell->retireLighting(**renderer);

    LMX_LOG_INFO("frame loop finished: {} presented, {} skipped, {} attempted", presentedFrames,
                 skippedFrames, frameIndex);
    return 0;
}

//======================================================================================================================
int runWindowed(const lmx::app::AppOptions& options,
                const std::shared_ptr<lmx::app::ConsoleLog>& consoleLog) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LMX_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    // kWindowWidth/kWindowHeight is the pre-maximize size and what --windowed keeps.
    SDL_WindowFlags windowFlags =
        SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (options.maximized) {
        windowFlags |= SDL_WINDOW_MAXIMIZED;
    }
    SDL_Window* window = SDL_CreateWindow("Luminex", kWindowWidth, kWindowHeight, windowFlags);
    if (window == nullptr) {
        LMX_LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // SDL_MetalView owns the layer and outlives all RHI objects created by run().
    SDL_MetalView view = SDL_Metal_CreateView(window);
    if (view == nullptr) {
        LMX_LOG_ERROR("SDL_Metal_CreateView failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const int exitCode = run(window, SDL_Metal_GetLayer(view), options, consoleLog);

    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    lmx::log::init();
    auto consoleLog = std::make_shared<lmx::app::ConsoleLog>();
    lmx::app::ConsoleLogSink consoleSink(consoleLog);
    lmx::render::installRhiLogForwarding();

    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }

    const lmx::app::AppOptionsResult options = lmx::app::parseAppOptions(arguments);
    if (!options) {
        LMX_LOG_ERROR("{}", options.error().message);
        return 1;
    }

    // Offscreen capture does not initialize SDL or create a window.
    if (options->mode == lmx::app::RunMode::Screenshot) {
        return lmx::app::runScreenshot(*options);
    }
    if (options->mode == lmx::app::RunMode::CaptureSequence) {
        return lmx::app::runCaptureSequence(*options);
    }
    if (options->mode == lmx::app::RunMode::Measure) {
        return lmx::app::runMeasurement(*options);
    }
    return runWindowed(*options, consoleLog);
}
