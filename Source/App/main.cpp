#include "Core/Log.h"
#include "RHI/RHI.h"

#include <SDL3/SDL.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace {

// Mirrors the MSL struct Slang emits for Shaders/Triangle.slang (measured, Task 6):
//   struct Vertex_natural_0 { packed_float2 position_0; packed_float3 color_1; };
// packed_* means no inter-member padding and no 16-byte struct alignment, so the C++
// mirror is five bare floats and the stride is 20, not 32.
struct Vertex {
    float px, py;
    float r, g, b;
};
static_assert(sizeof(Vertex) == 20, "vertex stride must match the shader's packed_float2/3 layout");

// The classic RGB triangle, clip space, counter-clockwise from the top.
constexpr std::array<Vertex, 3> kTriangle = {{
    {0.0f, 0.5f, 1.0f, 0.0f, 0.0f},
    {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
    {0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
}};

// Slot 0 of the MTL4 argument table, which is where Slang binds `gVertices` (Task 6 record).
constexpr uint32_t kVertexBufferSlot = 0;

// Logical points, not pixels — SDL scales this by the display's backing factor.
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

constexpr float kClearColor[4] = {0.1f, 0.15f, 0.2f, 1.0f};

// Automated-run knobs. LMX_MAX_FRAMES=N exits cleanly after N frame attempts, which is what
// makes this window app verifiable from a script; when it is set, the loop also drives the
// resize path on its own at the two frame numbers below, because there is no way to synthesise
// a real window resize without a user. Both are inert in a normal interactive run.
constexpr uint64_t kResizeDownFrame = 100;
constexpr uint64_t kResizeUpFrame = 200;
constexpr uint32_t kResizeDownWidth = 800;
constexpr uint32_t kResizeDownHeight = 600;

// 0 means "run until the user quits".
uint64_t maxFramesFromEnv() {
    const char* raw = std::getenv("LMX_MAX_FRAMES");
    if (raw == nullptr) {
        return 0;
    }
    const std::string_view text(raw);
    uint64_t frames = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), frames);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        LMX_LOG_WARN("LMX_MAX_FRAMES='{}' is not a number; running until quit", text);
        return 0;
    }
    return frames;
}

// Everything needed to draw the triangle, created in one place and recorded by one function
// (recordTriangleFrame) so that every way of rendering it stays the same drawing.
struct TriangleAssets {
    std::unique_ptr<lmx::rhi::Buffer> vertexBuffer;
    std::unique_ptr<lmx::rhi::ShaderLibrary> library;
    std::unique_ptr<lmx::rhi::GraphicsPipeline> pipeline;
};

// Returns nullopt after logging; every step here is fatal for the caller.
std::optional<TriangleAssets> createTriangleAssets(lmx::rhi::Device& device) {
    TriangleAssets assets;

    auto vertexBuffer = device.createBuffer(
        {.size = sizeof(kTriangle), .label = "lmx.app.triangleVertices"}, kTriangle.data());
    if (!vertexBuffer) {
        LMX_LOG_ERROR("createBuffer failed: {}", vertexBuffer.error().message);
        return std::nullopt;
    }
    LMX_LOG_INFO("vertex buffer: {} bytes ({} vertices, stride {})", (*vertexBuffer)->size(),
                 kTriangle.size(), sizeof(Vertex));
    assets.vertexBuffer = std::move(*vertexBuffer);

    // Relative to the process CWD: `xmake run App` (and `xmake test`) launch the target with
    // CWD == the target directory, which is exactly where the slang2metallib rule drops
    // Shaders/. Verified on this machine -- CWD was build/macosx/arm64/debug. No
    // executable-relative resolution needed, so none is added.
    auto library = device.loadShaderLibrary("Shaders/Triangle");
    if (!library) {
        LMX_LOG_ERROR("loadShaderLibrary failed: {}", library.error().message);
        return std::nullopt;
    }
    assets.library = std::move(*library);

    // BGRA8Unorm because that is the swapchain's format, and a pipeline's color format must
    // match the attachment it renders into.
    auto pipeline = device.createGraphicsPipeline({.library = assets.library.get(),
                                                   .vertexEntry = "vertexMain",
                                                   .fragmentEntry = "fragmentMain",
                                                   .colorFormat = lmx::rhi::Format::BGRA8Unorm,
                                                   .label = "lmx.app.trianglePipeline"});
    if (!pipeline) {
        LMX_LOG_ERROR("createGraphicsPipeline failed: {}", pipeline.error().message);
        return std::nullopt;
    }
    LMX_LOG_INFO("graphics pipeline: vertexMain/fragmentMain -> BGRA8Unorm");
    assets.pipeline = std::move(*pipeline);

    return assets;
}

// The whole of M1's rendering: clear to steel blue, draw three vertices. Recorded into an already
// open frame; the caller owns beginFrame/endFrame.
void recordTriangleFrame(lmx::rhi::CommandList& commands, lmx::rhi::Texture& target,
                         const TriangleAssets& assets) {
    commands.beginRenderPass(
        {.colorTarget = &target,
         .clearColor = {kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]},
         .clear = true});
    commands.bindPipeline(*assets.pipeline);
    commands.bindVertexBuffer(kVertexBufferSlot, *assets.vertexBuffer);
    commands.draw(static_cast<uint32_t>(kTriangle.size()));
    commands.endRenderPass();
}

// Everything RHI-owned lives here so that returning destroys it in reverse creation order --
// swapchain first, device last -- before main tears down the SDL window the swapchain's layer
// belongs to.
int run(SDL_Window* window, void* metalLayer) {
    auto device = lmx::rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    auto assets = createTriangleAssets(**device);
    if (!assets) {
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

    const uint64_t maxFrames = maxFramesFromEnv();
    if (maxFrames > 0) {
        LMX_LOG_INFO("LMX_MAX_FRAMES={}: exiting after that many frames", maxFrames);
    }

    uint64_t frameIndex = 0;
    uint64_t presentedFrames = 0;
    uint64_t skippedFrames = 0;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // data1/data2 are the new size in pixels. A minimised window reports 0 in at
                // least one dimension, which is not a size any swapchain can adopt.
                if (event.window.data1 > 0 && event.window.data2 > 0) {
                    (*swapchain)
                        ->resize(static_cast<uint32_t>(event.window.data1),
                                 static_cast<uint32_t>(event.window.data2));
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
                (*swapchain)->resize(kResizeDownWidth, kResizeDownHeight);
            } else if (frameIndex == kResizeUpFrame) {
                (*swapchain)
                    ->resize(static_cast<uint32_t>(pixelWidth), static_cast<uint32_t>(pixelHeight));
            }
        }

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

        lmx::rhi::CommandList& commands = (*device)->beginFrame();
        recordTriangleFrame(commands, **target, *assets);
        (*device)->endFrame(swapchain->get());
        ++presentedFrames;

        if (maxFrames > 0 && frameIndex >= maxFrames) {
            running = false;
        }
    }

    // Not load-bearing -- the swapchain and device destructors each drain on their own -- but
    // it keeps the teardown readable: the loop is over and the GPU is idle before anything
    // starts being released. The redundant drain costs one already-signalled event.
    (*device)->waitIdle();

    LMX_LOG_INFO("frame loop finished: {} presented, {} skipped, {} attempted", presentedFrames,
                 skippedFrames, frameIndex);
    return 0;
}

} // namespace

int main() {
    lmx::log::init();

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
