#include "Core/Log.h"
#include "RHI/RHI.h"

#include <array>
#include <cstdint>
#include <vector>

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

// Small enough that a full readback is trivial, large enough to prove row stride handling.
constexpr uint32_t kReadbackExtent = 4;

} // namespace

int main() {
    lmx::log::init();

    auto device = lmx::rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    auto vertexBuffer = (*device)->createBuffer(
        {.size = sizeof(kTriangle), .label = "lmx.app.triangleVertices"}, kTriangle.data());
    if (!vertexBuffer) {
        LMX_LOG_ERROR("createBuffer failed: {}", vertexBuffer.error().message);
        return 1;
    }
    LMX_LOG_INFO("vertex buffer: {} bytes ({} vertices, stride {})", (*vertexBuffer)->size(),
                 kTriangle.size(), sizeof(Vertex));

    auto readbackTexture = (*device)->createTexture({.width = kReadbackExtent,
                                                     .height = kReadbackExtent,
                                                     .format = lmx::rhi::Format::RGBA8Unorm,
                                                     .renderTarget = true,
                                                     .cpuReadback = true,
                                                     .label = "lmx.app.readback"});
    if (!readbackTexture) {
        LMX_LOG_ERROR("createTexture failed: {}", readbackTexture.error().message);
        return 1;
    }
    LMX_LOG_INFO("readback texture: {}x{} RGBA8Unorm", (*readbackTexture)->width(),
                 (*readbackTexture)->height());

    // Relative to the process CWD: `xmake run App` (and `xmake test`) launch the target with
    // CWD == the target directory, which is exactly where the slang2metallib rule drops
    // Shaders/. Verified on this machine -- CWD was build/macosx/arm64/debug. No
    // executable-relative resolution needed, so none is added.
    auto library = (*device)->loadShaderLibrary("Shaders/Triangle");
    if (!library) {
        LMX_LOG_ERROR("loadShaderLibrary failed: {}", library.error().message);
        return 1;
    }

    auto pipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = lmx::rhi::Format::BGRA8Unorm,
                                                       .label = "lmx.app.trianglePipeline"});
    if (!pipeline) {
        LMX_LOG_ERROR("createGraphicsPipeline failed: {}", pipeline.error().message);
        return 1;
    }
    LMX_LOG_INFO("graphics pipeline: vertexMain/fragmentMain -> BGRA8Unorm");

    // Nothing has rendered into the texture yet (that is Task 11); this only exercises the
    // shared-storage readback path end to end.
    std::vector<uint8_t> pixels(size_t{kReadbackExtent} * kReadbackExtent * 4);
    (*readbackTexture)->readback(pixels.data(), pixels.size());
    LMX_LOG_INFO("texture readback: {} bytes", pixels.size());

    return 0;
}
