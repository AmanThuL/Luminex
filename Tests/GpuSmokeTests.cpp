#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "RHI/RHI.h"

namespace {

// Mirrors the MSL struct Slang emits for Shaders/Triangle.slang (measured, Task 6):
//   struct Vertex_natural_0 { packed_float2 position_0; packed_float3 color_1; };
// packed_* means no inter-member padding and no 16-byte struct alignment, so the stride
// is 20, not 32. Same mirror as Source/App/main.cpp -- if one drifts, this fires.
struct Vertex {
    float position[2];
    float color[3];
};
static_assert(sizeof(Vertex) == 20, "must match Slang's packed Vertex_natural_0 layout");

// The app's triangle, unchanged: red apex at clip y=+0.5, green bottom-left, blue
// bottom-right, counter-clockwise.
constexpr std::array<Vertex, 3> kTriangle = {{
    {{0.0f, 0.5f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
}};

// Slot 0 of the MTL4 argument table, where Slang binds `gVertices` (Task 6 record).
constexpr uint32_t kVertexBufferSlot = 0;

// Small enough that the whole test is a few milliseconds, large enough that every probe
// below sits at least three pixels clear of an edge rather than on one.
constexpr uint32_t kSize = 64;

// One BGRA8Unorm texel, in the channel order readback() produces.
struct Pixel {
    uint8_t b = 0, g = 0, r = 0, a = 0;
};

Pixel pixelAt(const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * kSize + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

// Catch2 stringifies the comparison operands, but not the pixel they came from; this goes
// through INFO so a failure says which colour was actually there.
std::string describe(const char* what, uint32_t x, uint32_t y, const Pixel& p) {
    return std::string(what) + " (" + std::to_string(x) + "," + std::to_string(y) +
           "): B=" + std::to_string(p.b) + " G=" + std::to_string(p.g) +
           " R=" + std::to_string(p.r) + " A=" + std::to_string(p.a);
}

// REQUIRE(result.has_value()) on its own reports "false != true"; the backend's message is
// the only thing that says *why*, so it is pulled out for INFO before the assertion.
template <typename T>
std::string errorOf(const lmx::rhi::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

// Row order: readback() (MTLTexture::getBytes) produces row 0 == the *top* of the image, so
// a clip-space y maps to row kSize * (1 - y) / 2. Measured in Task 11: the red apex at
// y=+0.5 landed on row 183 of a 720-row readback (720 * 0.5 / 2 = 180), and the base edge on
// row 539 (expected 540). Here that puts the apex near row 16 and the base near row 48.
TEST_CASE("offscreen triangle renders expected pixels", "[gpu]") {
    using namespace lmx::rhi;

    // Declared before every resource, so it is destroyed last: no RHI object may outlive the
    // device that created it. Nothing else is needed here -- there is no swapchain, and so
    // none of the presentation-surface ordering the windowed path has to respect.
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto vertexBuffer = (*device)->createBuffer(
        {.size = sizeof(kTriangle), .label = "lmx.test.smokeVertices"}, kTriangle.data());
    INFO(errorOf(vertexBuffer));
    REQUIRE(vertexBuffer.has_value());

    // cpuReadback puts the texture in shared storage so readback() is a plain copy;
    // renderTarget is what lets a pass draw into it.
    auto target = (*device)->createTexture({.width = kSize,
                                            .height = kSize,
                                            .format = Format::BGRA8Unorm,
                                            .renderTarget = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.smokeTarget"});
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    // pathNoExt, resolved against the process CWD. `xmake test` runs a target with CWD ==
    // target:rundir(), which defaults to the target directory -- exactly where this target's
    // slang2metallib rule drops Shaders/Triangle.{metal,metallib}. Verified on this machine
    // (CWD was build/macosx/arm64/debug/test, and the load still succeeded with App's copy
    // of Shaders/ deleted), so no executable-relative fallback is needed.
    auto library = (*device)->loadShaderLibrary("Shaders/Triangle");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = Format::BGRA8Unorm,
                                                       .label = "lmx.test.smokePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    CommandList& commands = (*device)->beginFrame();
    commands.beginRenderPass(
        {.colorTarget = target->get(), .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .clear = true});
    commands.bindPipeline(**pipeline);
    commands.bindVertexBuffer(kVertexBufferSlot, **vertexBuffer);
    commands.draw(static_cast<uint32_t>(kTriangle.size()));
    commands.endRenderPass();
    // nullptr: this frame only fills a texture, there is nothing to present.
    (*device)->endFrame(nullptr);
    // readback() does no synchronisation of its own, so the frame has to be off the GPU
    // first -- otherwise the pixels are whatever the allocation happened to contain.
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*target)->readback(pixels.data(), pixels.size());

    // No SECTIONs below: Catch2 replays the whole test case body once per section, which
    // would mean building a device and rendering a frame three times over for one image.

    // (2,2) is clip (-0.92, +0.92) -- far outside the triangle on both axes, so this holds
    // whichever way up the readback is, and it fails outright if the pass never ran.
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("corner", 2, 2, corner));
    REQUIRE(corner.b == 0);
    REQUIRE(corner.g == 0);
    REQUIRE(corner.r == 0);
    REQUIRE(corner.a == 255);

    // (32,40) is clip (+0.02, -0.27): on the vertical centre line, between the apex row and
    // the base row, and about twelve pixels inside the nearest edge.
    const Pixel inside = pixelAt(pixels, 32, 40);
    INFO(describe("inside", 32, 40, inside));
    REQUIRE(int{inside.b} + int{inside.g} + int{inside.r} > 60);
    REQUIRE(inside.a == 255);

    // The next three probes are what separate "a triangle was drawn" from "*this* triangle
    // was drawn": each sits about three pixels inside the edges meeting one vertex, so its
    // dominant channel must be that vertex's colour. A wrong vertex stride, a mis-bound
    // buffer slot or a flipped image moves the colours off these points while leaving the
    // two probes above perfectly happy.
    const Pixel apex = pixelAt(pixels, 32, 24);
    INFO(describe("apex", 32, 24, apex));
    REQUIRE(apex.r > 128);
    REQUIRE(apex.r > apex.g);
    REQUIRE(apex.r > apex.b);

    const Pixel bottomLeft = pixelAt(pixels, 20, 46);
    INFO(describe("bottom-left", 20, 46, bottomLeft));
    REQUIRE(bottomLeft.g > 128);
    REQUIRE(bottomLeft.g > bottomLeft.r);
    REQUIRE(bottomLeft.g > bottomLeft.b);

    const Pixel bottomRight = pixelAt(pixels, 43, 46);
    INFO(describe("bottom-right", 43, 46, bottomRight));
    REQUIRE(bottomRight.b > 128);
    REQUIRE(bottomRight.b > bottomRight.r);
    REQUIRE(bottomRight.b > bottomRight.g);
}
