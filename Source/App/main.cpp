#include "App/EditorShell.h"
#include "App/Screenshot.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4Capture.h"
#include "RHI/Metal4/Metal4ImGui.h"
#include "RHI/RHI.h"
#include "Render/Renderer.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// Logical points, not pixels — SDL scales this by the display's backing factor.
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// The UI pass's clear. Only ever visible where no ImGui window covers it -- the dockspace's own
// gaps -- so it is a neutral dark rather than anything with an opinion. The *scene's* clear is
// Renderer::clearColor, which the Inspector edits.
constexpr float kUiClearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};

// Relative to the process CWD, which for `xmake run App` is the directory holding the binary.
constexpr std::string_view kCapturePath = "luminex-frame.gputrace";

// Automated-run knobs. LMX_MAX_FRAMES=N exits cleanly after N frame attempts, which is what
// makes this window app verifiable from a script; when it is set, the loop also drives the
// resize path on its own at the two frame numbers below, because there is no way to synthesise
// a real window resize without a user. Both are inert in a normal interactive run.
//
// M2 note: these drive SDL_SetWindowSize rather than Swapchain::resize, which is what M1 did.
// Resizing the swapchain alone now desynchronises the frame -- ImGui's draw data is sized from
// the *window*, so its scissor rects would exceed a smaller drawable and Metal validation
// rejects that. Driving the real window instead makes the drill exercise the whole path the way
// a user's resize does: SDL event -> swapchain resize -> new panel size -> debounced scene-target
// resize. Points, not pixels, because that is what SDL_SetWindowSize takes.
constexpr uint64_t kResizeDownFrame = 100;
constexpr uint64_t kResizeUpFrame = 200;
constexpr int kResizeDownWidth = 800;
constexpr int kResizeDownHeight = 600;

// Reads a frame number out of the environment. 0 -- which is also what an unset or malformed
// variable yields -- means "disabled" for every caller.
uint64_t frameNumberFromEnv(const char* name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return 0;
    }
    const std::string_view text(raw);
    uint64_t frames = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), frames);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        LMX_LOG_WARN("{}='{}' is not a number; ignoring it", name, text);
        return 0;
    }
    return frames;
}

// Everything RHI-owned and everything ImGui-owned lives here so that returning destroys it in
// reverse creation order -- editor shell first (it drains the device through imguiShutdown, so
// the device must still exist), then swapchain, renderer, meshes, device -- before main tears
// down the SDL window the swapchain's layer belongs to.
int run(SDL_Window* window, void* metalLayer) {
    auto device = lmx::rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    auto meshes = lmx::app::createSceneMeshes(**device);
    if (!meshes) {
        LMX_LOG_ERROR("createSceneMeshes failed: {}", meshes.error().message);
        return 1;
    }

    // Pixels, not points: on a Retina display the backing store is 2x the logical window size,
    // and a swapchain sized in points would be presented upscaled and blurry.
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) {
        LMX_LOG_ERROR("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return 1;
    }

    auto swapchain = (*device)->createSwapchain({.nativeLayer = metalLayer,
                                                 .width = static_cast<uint32_t>(pixelWidth),
                                                 .height = static_cast<uint32_t>(pixelHeight),
                                                 .format = lmx::rhi::Format::BGRA8Unorm});
    if (!swapchain) {
        LMX_LOG_ERROR("createSwapchain failed: {}", swapchain.error().message);
        return 1;
    }
    LMX_LOG_INFO("swapchain: {}x{} pixels BGRA8Unorm", pixelWidth, pixelHeight);

    // Started at the window's size only because the Viewport panel's size is not known until it
    // has been laid out once; the shell's debounce corrects it within the first few frames.
    auto renderer = lmx::render::Renderer::create(**device, static_cast<uint32_t>(pixelWidth),
                                                  static_cast<uint32_t>(pixelHeight));
    if (!renderer) {
        LMX_LOG_ERROR("Renderer::create failed: {}", renderer.error().message);
        return 1;
    }

    // Declared last so it is destroyed first: ~EditorShell tears the ImGui backends down, and the
    // renderer glue drains this device while doing it.
    auto shell = lmx::app::EditorShell::create(
        window, **device, lmx::app::makeDefaultScene(meshes->cube, meshes->plane));
    if (!shell) {
        return 1;
    }

    const uint64_t maxFrames = frameNumberFromEnv("LMX_MAX_FRAMES");
    if (maxFrames > 0) {
        LMX_LOG_INFO("LMX_MAX_FRAMES={}: exiting after that many frames", maxFrames);
    }
    // The keypress path cannot be driven by a script -- there is nobody to press the key in an
    // automated run -- so the same one-frame capture is reachable by frame number. The 'c' key is
    // the path a human uses; this exists for verification and CI.
    const uint64_t captureAtFrame = frameNumberFromEnv("LMX_CAPTURE_AT_FRAME");
    if (captureAtFrame > 0) {
        LMX_LOG_INFO("LMX_CAPTURE_AT_FRAME={}: will capture that frame", captureAtFrame);
    }
    LMX_LOG_INFO("press 'c' to capture one frame to {} (needs MTL_CAPTURE_ENABLED=1)",
                 kCapturePath);

    uint64_t frameIndex = 0;
    uint64_t presentedFrames = 0;
    uint64_t skippedFrames = 0;
    bool running = true;
    // Set by the 'c' key or the frame hook, consumed by the next frame that actually renders.
    bool captureRequested = false;
    uint64_t previousTicksNs = SDL_GetTicksNS();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // ImGui sees every event first and unconditionally: it is the thing that decides who
            // owns the mouse and the keyboard, and it cannot decide from events it never saw.
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                // Ignoring repeats: holding the key would otherwise queue a capture per frame.
                // WantCaptureKeyboard holds last frame's answer, which is the documented way to
                // use it -- without it, typing 'c' into an Inspector field would start a capture.
                if (event.key.key == SDLK_C && !event.key.repeat &&
                    !ImGui::GetIO().WantCaptureKeyboard) {
                    captureRequested = true;
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // data1/data2 are the new size in pixels. A minimised window reports 0 in at
                // least one dimension, which is not a size any swapchain can adopt.
                if (event.window.data1 > 0 && event.window.data2 > 0) {
                    pixelWidth = event.window.data1;
                    pixelHeight = event.window.data2;
                    (*swapchain)
                        ->resize(static_cast<uint32_t>(pixelWidth),
                                 static_cast<uint32_t>(pixelHeight));
                }
                break;
            default:
                break;
            }
        }
        if (!running) {
            break;
        }

        ++frameIndex;

        if (maxFrames > 0) {
            if (frameIndex == kResizeDownFrame) {
                SDL_SetWindowSize(window, kResizeDownWidth, kResizeDownHeight);
            } else if (frameIndex == kResizeUpFrame) {
                SDL_SetWindowSize(window, kWindowWidth, kWindowHeight);
            }
        }
        if (captureAtFrame > 0 && frameIndex == captureAtFrame) {
            captureRequested = true;
        }

        const uint64_t nowNs = SDL_GetTicksNS();
        const float deltaSeconds = static_cast<float>(nowNs - previousTicksNs) * 1e-9f;
        previousTicksNs = nowNs;
        const float timeSeconds = static_cast<float>(nowNs) * 1e-9f;

        // Before the ImGui frame opens, for two reasons: it drains the GPU and frees the old
        // scene targets, which the Viewport image below is about to name; and it must not run
        // between an imguiNewFrame() and its imguiRender().
        shell->applyPendingViewportResize(**device, **renderer);

        // The acquire comes before *any* ImGui call this frame, and that ordering is the whole
        // answer to the skip-on-failure path: an imguiNewFrame() with no matching imguiRender()
        // leaves ImGui mid-frame, and the next ImGui::NewFrame() would abort on it. With the
        // acquire first, a skipped frame has simply not started an ImGui frame to orphan.
        auto target = (*swapchain)->acquireNextTexture();
        if (!target) {
            // Expected under contention, not an error: every drawable is still in flight or the
            // layer timed out. Dropping the frame is the correct response.
            ++skippedFrames;
            LMX_LOG_WARN("frame {} skipped: {}", frameIndex, target.error().message);
            if (maxFrames > 0 && frameIndex >= maxFrames) {
                running = false;
            }
            continue;
        }

        // The capture window is exactly one frame wide, and it opens here -- after the acquire
        // that can still drop the frame, before any encoding. A request that finds capture
        // unavailable is consumed rather than retried: the fix is an environment variable at
        // launch, so retrying every frame would only repeat the warning forever.
        bool capturingThisFrame = false;
        if (captureRequested) {
            captureRequested = false;
            capturingThisFrame = lmx::rhi::metal4::beginCapture(**device, kCapturePath);
        }

        // Metal4ImGui.h's contract: the renderer's NewFrame runs before Device::beginFrame(),
        // because Dear ImGui wants it before ImGui::NewFrame() and therefore before the UI code
        // that decides what this frame draws at all.
        lmx::rhi::metal4::imguiNewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        shell->buildUI(**renderer, deltaSeconds);
        ImGui::Render();

        lmx::rhi::CommandList& commands = (*device)->beginFrame();
        // barrierForSampling defaults to true, and is wanted here: the UI pass below samples this
        // target through ImGui's own backend.
        (*renderer)->render(commands, shell->camera(), shell->drawItems(timeSeconds));

        // The UI pass: the swapchain drawable, single-sampled, no depth attachment -- ImGui's
        // pipeline was built against exactly that description at imguiInit(), and a mismatch is
        // a Metal validation failure rather than anything this code could catch.
        commands.beginRenderPass(
            {.colorTarget = *target,
             .clearColor = {kUiClearColor[0], kUiClearColor[1], kUiClearColor[2], kUiClearColor[3]},
             .clear = true});
        // Last in the pass: imguiRender hands the encoder to ImGui and ImGui does not put it
        // back -- pipeline, argument table and scissor all belong to it afterwards.
        lmx::rhi::metal4::imguiRender(commands);
        commands.endRenderPass();
        (*device)->endFrame(swapchain->get());
        ++presentedFrames;

        if (capturingThisFrame) {
            // stopCapture finalises the document, so the frame it is meant to contain has to be
            // off the GPU first. One stall on one frame, only when capturing.
            (*device)->waitIdle();
            lmx::rhi::metal4::endCapture();
        }

        if (maxFrames > 0 && frameIndex >= maxFrames) {
            running = false;
        }
    }

    // Not load-bearing -- the swapchain and device destructors each drain on their own, and
    // imguiShutdown drains before freeing ImGui's buffers -- but it keeps the teardown readable:
    // the loop is over and the GPU is idle before anything starts being released.
    (*device)->waitIdle();

    LMX_LOG_INFO("frame loop finished: {} presented, {} skipped, {} attempted", presentedFrames,
                 skippedFrames, frameIndex);
    return 0;
}

int runWindowed() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LMX_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("Luminex", kWindowWidth, kWindowHeight,
                         SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        LMX_LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // SDL owns the CAMetalLayer; the view is what keeps it alive, so it must outlive every RHI
    // object built on top of it -- hence the destruction of all of those inside run().
    SDL_MetalView view = SDL_Metal_CreateView(window);
    if (view == nullptr) {
        LMX_LOG_ERROR("SDL_Metal_CreateView failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const int exitCode = run(window, SDL_Metal_GetLayer(view));

    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

} // namespace

int main(int argc, char** argv) {
    lmx::log::init();

    std::string_view screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--") {
            // Conventional end-of-options marker, not an error -- nothing after it is parsed
            // as a flag by this app today, but a bare "--" should never be rejected.
            continue;
        }
        if (arg == "--screenshot") {
            if (i + 1 >= argc) {
                LMX_LOG_ERROR("--screenshot needs an output path: App --screenshot <out.bmp>");
                return 1;
            }
            screenshotPath = argv[++i];
        } else {
            LMX_LOG_ERROR("unknown argument '{}'; usage: App [--screenshot <out.bmp>]", arg);
            return 1;
        }
    }

    // SDL is never initialised on the screenshot path: it renders offscreen, so a window would be
    // pure ceremony -- and skipping it keeps the path free of any windowing dependency at all.
    if (!screenshotPath.empty()) {
        return lmx::app::runScreenshot(std::filesystem::path(screenshotPath));
    }
    return runWindowed();
}
