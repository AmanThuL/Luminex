#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

// Small enough that the whole test is a few milliseconds, large enough that each probe below
// sits far enough inside its region to be deterministically interior without MSAA; see the
// per-probe margins noted at each REQUIRE below.
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

// BGRA8Unorm quantises 0.0 to 0 and 1.0 to 255, and the flat-shaded triangles below interpolate
// three identical vertex colours, so the rendered channel is the vertex channel. The tolerance
// only has to separate "on" from "off", so it is deliberately loose rather than exact.
bool channelIs(uint8_t actual, float expected) {
    return expected > 0.5f ? actual > 200 : actual < 55;
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
    // the base row, and 7.5px from its nearest edge (the base, at row 48).
    const Pixel inside = pixelAt(pixels, 32, 40);
    INFO(describe("inside", 32, 40, inside));
    REQUIRE(int{inside.b} + int{inside.g} + int{inside.r} > 60);
    REQUIRE(inside.a == 255);

    // The next three probes are what separate "a triangle was drawn" from "*this* triangle
    // was drawn": each sits inside the edges meeting one vertex -- the two base-corner probes
    // (bottom-left, bottom-right) are 1.5px from the base edge at row 48 -- so its dominant
    // channel must be that vertex's colour. A wrong vertex stride, a mis-bound buffer slot or
    // a flipped image moves the colours off these points while leaving the two probes above
    // perfectly happy.
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

// Proves the transient uniform ring end to end -- the memcpy into the mapped ring, the
// gpuAddress() + offset arithmetic, and the residency registration -- on the GPU rather than by
// inspecting backend state.
//
// The trick is the payload: it feeds the *triangle vertices* through setUniforms and binds them
// at the vertex slot. Metal 4 argument tables hold untyped GPU addresses, so a suballocation of
// the ring is exactly as bindable as a whole MTLBuffer, and reusing Shaders/Triangle means the
// shader itself reports whether the bytes arrived -- no extra shader, and the expected image is
// the one the smoke test above already pins.
//
// The deliberate part is the *first* call: a throwaway blob is written first so the vertices
// land at a non-zero (256-aligned) offset. At offset 0 a dropped `+ offset` in either the
// memcpy destination or the bound address would still produce a perfect triangle; this is what
// makes that bug visible. If the ring were also not resident, the GPU would read unmapped
// memory here rather than vertices.
//
// Both calls target kVertexBufferSlot -- the second simply overwrites the first's binding.
// Pushing the cursor through a *different* slot would work too, but only for slots inside the
// device's maxBufferBindCount, which this test has no way to see; going through the slot the
// shader actually reads keeps the proof identical and the coupling zero.
TEST_CASE("uniform ring feeds a draw from a non-zero offset", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto target = (*device)->createTexture({.width = kSize,
                                            .height = kSize,
                                            .format = Format::BGRA8Unorm,
                                            .renderTarget = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.ringTarget"});
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/Triangle");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = Format::BGRA8Unorm,
                                                       .label = "lmx.test.ringPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    // Zeroes, written only to push the ring cursor past zero: 100 bytes rounds up to the
    // 256-byte alignment. Deliberately zeroes rather than noise -- if the real vertices below
    // were ever bound at offset 0 instead, the draw reads this and collapses to a degenerate
    // black triangle, which the colour probes catch immediately.
    const std::array<uint8_t, 100> filler{};

    CommandList& commands = (*device)->beginFrame();
    commands.beginRenderPass(
        {.colorTarget = target->get(), .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .clear = true});
    commands.bindPipeline(**pipeline);
    commands.setUniforms(kVertexBufferSlot, filler.data(), filler.size());
    commands.setUniforms(kVertexBufferSlot, kTriangle.data(), sizeof(kTriangle));
    commands.draw(static_cast<uint32_t>(kTriangle.size()));
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*target)->readback(pixels.data(), pixels.size());

    // Same probes as the smoke test: background stays clear, and each vertex's colour has to
    // land on its own corner. Garbage vertices fail these long before they look plausible.
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("corner", 2, 2, corner));
    REQUIRE(corner.b == 0);
    REQUIRE(corner.g == 0);
    REQUIRE(corner.r == 0);
    REQUIRE(corner.a == 255);

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

// Pins the per-frame rotation the ring exists for: that frame N's uniform bytes are frame N's,
// even once N starts reusing a slot an earlier frame wrote.
//
// kFramesInFlight is 3, so slot = frameNumber % 3 first repeats on the *fourth* frame -- which
// is also the first frame whose beginFrame actually waits on the pacing shared event (the wait
// is skipped while frameNumber <= kFramesInFlight). Six frames therefore cover two full
// rotations with the wait engaged, and frame 3 landing yellow where frame 0 left red in the same
// slot is precisely the observation that a stale or misrouted ring would fail.
//
// This also discharges the rotation coverage deferred from Task 2's review: nothing until now
// asserted that a frame reusing a slot sees its own data.
//
// Cheap by construction: one 64x64 target reused across frames, one flat-shaded draw each, and
// the colours are the six saturated corners of the RGB cube so each frame is unmistakable.
TEST_CASE("uniform ring keeps per-frame data across slot reuse", "[gpu]") {
    using namespace lmx::rhi;

    constexpr std::array<std::array<float, 3>, 6> kFrameColors = {{
        {1.0f, 0.0f, 0.0f}, // frame 0 -> ring slot 1
        {0.0f, 1.0f, 0.0f}, // frame 1 -> ring slot 2
        {0.0f, 0.0f, 1.0f}, // frame 2 -> ring slot 0
        {1.0f, 1.0f, 0.0f}, // frame 3 -> ring slot 1 again, first frame that waits
        {0.0f, 1.0f, 1.0f}, // frame 4 -> ring slot 2 again
        {1.0f, 0.0f, 1.0f}, // frame 5 -> ring slot 0 again
    }};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto target = (*device)->createTexture({.width = kSize,
                                            .height = kSize,
                                            .format = Format::BGRA8Unorm,
                                            .renderTarget = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.rotationTarget"});
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/Triangle");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = Format::BGRA8Unorm,
                                                       .label = "lmx.test.rotationPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);

    for (uint32_t frame = 0; frame < kFrameColors.size(); ++frame) {
        const std::array<float, 3>& color = kFrameColors[frame];

        // The app's triangle geometry, flat-shaded in this frame's colour: all three vertices
        // share it, so the interior interpolates to exactly that colour and one probe suffices.
        std::array<Vertex, 3> vertices = kTriangle;
        for (Vertex& vertex : vertices) {
            vertex.color[0] = color[0];
            vertex.color[1] = color[1];
            vertex.color[2] = color[2];
        }

        CommandList& commands = (*device)->beginFrame();
        commands.beginRenderPass(
            {.colorTarget = target->get(), .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .clear = true});
        commands.bindPipeline(**pipeline);
        commands.setUniforms(kVertexBufferSlot, vertices.data(), sizeof(vertices));
        commands.draw(static_cast<uint32_t>(vertices.size()));
        commands.endRenderPass();
        (*device)->endFrame(nullptr);
        // Per frame, not once at the end: the point is to observe *this* frame's pixels before
        // the next frame overwrites the target.
        (*device)->waitIdle();
        (*target)->readback(pixels.data(), pixels.size());

        // (32,40) is the interior probe the smoke test above establishes -- inside all three
        // edges, so it carries the flat colour rather than an edge-antialiased blend.
        const Pixel inside = pixelAt(pixels, 32, 40);
        INFO("frame " + std::to_string(frame) + " expected R=" + std::to_string(color[0]) +
             " G=" + std::to_string(color[1]) + " B=" + std::to_string(color[2]));
        INFO(describe("inside", 32, 40, inside));
        REQUIRE(channelIs(inside.r, color[0]));
        REQUIRE(channelIs(inside.g, color[1]));
        REQUIRE(channelIs(inside.b, color[2]));
        REQUIRE(inside.a == 255);
    }
}

// Proves the depth path end to end on the GPU: the pass's depth attachment (Metal4CommandList)
// and the pipeline's depth-stencil state (Metal4Device + bindPipeline). Neither has any other
// coverage -- the new validation tests are CPU-only -- and a depth feature that silently does
// nothing is exactly the kind of bug that survives until a scene looks subtly wrong.
//
// The mechanism is coplanarity, not occlusion by distance: Shaders/Triangle emits z = 0 for every
// vertex, so with clearDepth 1.0 the first draw passes LESS (0 < 1) and writes 0, and the second
// draw of the *same* geometry fails it (0 < 0 is false). Red therefore has to survive green.
// Deliberately chosen over two different depths because it needs no shader change: the existing
// shader has no way to vary z.
//
// The two passes below cover the two halves of the feature separately, and that split is
// deliberate -- it was measured, not assumed:
//   - Pass 1 (coplanar draws) covers bindPipeline's setDepthStencilState. Skipping that bind
//     leaves the encoder default (compare Always, no write) and green wins. Verified by
//     temporarily removing the bind: the probe came back G=255 R=0.
//   - Pass 1 does NOT cover the depth *attachment*: removing it from the pass descriptor leaves
//     this test green. Metal keeps per-tile depth storage for the pass regardless, so the test
//     still works -- the attachment texture is where depth is stored, not what makes the test
//     run. Also verified by temporarily removing it.
//   - Pass 2 is what covers the attachment, via clearDepth: a clear value only exists on the
//     depth attachment descriptor, so clearing to 0.0 and watching an entire z=0 draw vanish
//     is only possible if beginRenderPass actually wired the attachment up.
TEST_CASE("depth test rejects a coplanar second draw", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto target = (*device)->createTexture({.width = kSize,
                                            .height = kSize,
                                            .format = Format::BGRA8Unorm,
                                            .renderTarget = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.depthColorTarget"});
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    // renderTarget only -- neither sampled nor cpuReadback. That is the case the reworked
    // usage logic exists for: a pure depth target asks for no ShaderRead, so the driver keeps
    // it in whatever compressed layout it likes. It also pins that validation accepts a
    // D32Float render target (a *depth* target) rather than rejecting it as non-color-renderable.
    auto depthTarget = (*device)->createTexture({.width = kSize,
                                                 .height = kSize,
                                                 .format = Format::D32Float,
                                                 .renderTarget = true,
                                                 .label = "lmx.test.depthTarget"});
    INFO(errorOf(depthTarget));
    REQUIRE(depthTarget.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/Triangle");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = Format::BGRA8Unorm,
                                                       .depthFormat = Format::D32Float,
                                                       .depthTestEnable = true,
                                                       .depthWriteEnable = true,
                                                       .label = "lmx.test.depthPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    // Same geometry, flat-shaded, drawn in this order. Depth has to keep the first.
    const auto flatTriangle = [](float r, float g, float b) {
        std::array<Vertex, 3> vertices = kTriangle;
        for (Vertex& vertex : vertices) {
            vertex.color[0] = r;
            vertex.color[1] = g;
            vertex.color[2] = b;
        }
        return vertices;
    };
    const std::array<Vertex, 3> first = flatTriangle(1.0f, 0.0f, 0.0f);
    const std::array<Vertex, 3> second = flatTriangle(0.0f, 1.0f, 0.0f);

    CommandList& commands = (*device)->beginFrame();
    commands.beginRenderPass({.colorTarget = target->get(),
                              .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                              .clear = true,
                              .depthTarget = depthTarget->get(),
                              .clearDepth = 1.0f});
    commands.bindPipeline(**pipeline);
    commands.setUniforms(kVertexBufferSlot, first.data(), sizeof(first));
    commands.draw(static_cast<uint32_t>(first.size()));
    commands.setUniforms(kVertexBufferSlot, second.data(), sizeof(second));
    commands.draw(static_cast<uint32_t>(second.size()));
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*target)->readback(pixels.data(), pixels.size());

    // (32,40) is the interior probe the smoke test establishes. Red means the depth test
    // rejected the second draw; green means depth did nothing at all.
    const Pixel inside = pixelAt(pixels, 32, 40);
    INFO(describe("inside", 32, 40, inside));
    REQUIRE(channelIs(inside.r, 1.0f));
    REQUIRE(channelIs(inside.g, 0.0f));
    REQUIRE(inside.a == 255);

    // The clear still has to have happened outside the triangle -- a depth attachment must not
    // disturb the color attachment's own load/store behaviour.
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("corner", 2, 2, corner));
    REQUIRE(corner.r == 0);
    REQUIRE(corner.g == 0);
    REQUIRE(corner.b == 0);
    REQUIRE(corner.a == 255);

    // Pass 2: same everything, but the depth attachment is cleared to 0.0 instead of 1.0. Now
    // the *first* draw fails LESS as well (0 < 0 is false), so nothing reaches the color target
    // and the whole image stays at the clear colour. clearDepth lives on the depth attachment
    // descriptor and nowhere else, so this can only hold if beginRenderPass built that
    // attachment -- which is exactly what pass 1 above cannot see.
    CommandList& secondFrame = (*device)->beginFrame();
    secondFrame.beginRenderPass({.colorTarget = target->get(),
                                 .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                                 .clear = true,
                                 .depthTarget = depthTarget->get(),
                                 .clearDepth = 0.0f});
    secondFrame.bindPipeline(**pipeline);
    secondFrame.setUniforms(kVertexBufferSlot, first.data(), sizeof(first));
    secondFrame.draw(static_cast<uint32_t>(first.size()));
    secondFrame.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    (*target)->readback(pixels.data(), pixels.size());

    const Pixel occluded = pixelAt(pixels, 32, 40);
    INFO(describe("occluded", 32, 40, occluded));
    REQUIRE(occluded.r == 0);
    REQUIRE(occluded.g == 0);
    REQUIRE(occluded.b == 0);
    REQUIRE(occluded.a == 255);
}

// --- lmx::render::Renderer cases ------------------------------------------------------------
//
// Everything below drives the real Renderer rather than a hand-rolled pass, so the shader binding
// contract, the ObjectUniforms mirror and the depth/barrier wiring are all covered as the App uses
// them, not as a test re-derives them.

namespace {

using lmx::render::Camera;
using lmx::render::DrawItem;
using lmx::render::Mesh;
using lmx::render::Renderer;

// Renderer's default clear, restated here because the probes below assert against it: if the
// default ever changes, these tests must be re-derived rather than silently loosened.
constexpr std::array<float, 4> kSceneClear = {0.05f, 0.07f, 0.10f, 1.0f};

// x offset of each cube from the origin. At the camera distance below this puts the two cubes on
// opposite halves of a 64px image with clear background between them -- see the probe comments.
constexpr float kCubeOffsetX = 1.2f;

// The camera every scene case uses: 5 units back on +Z, level, looking down -Z at the origin.
Camera sceneCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    return camera;
}

// BGRA8Unorm quantises a float channel by rounding, so the clear colour lands within a ulp or two
// of this. Tolerance 2, not 0, because the exact rounding of the conversion is not contractual.
bool isClearChannel(uint8_t actual, float expected) {
    const int want = static_cast<int>(expected * 255.0f + 0.5f);
    return std::abs(int{actual} - want) <= 2;
}

// The two-cube scene shared by the capture case and the barrier case. Deliberately *one* mesh
// drawn twice: buffer slot 0 then holds the same address for both draws, so the only thing that
// differs between them is the per-draw uniform block at slot 1 -- which is exactly the variable
// the argument-table capture question turns on (plan Amendment A2).
std::array<DrawItem, 2> twoCubeScene(const Mesh& cube) {
    return {{
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{-kCubeOffsetX, 0.0f, 0.0f}),
         .baseColor = {1.0f, 0.0f, 0.0f, 1.0f}},
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{kCubeOffsetX, 0.0f, 0.0f}),
         .baseColor = {0.0f, 0.0f, 1.0f, 1.0f}},
    }};
}

// The image twoCubeScene must produce, as three probes.
//
// Geometry, so the coordinates are checkable rather than magic: fovY 60 deg at aspect 1 makes the
// visible half-extent at the cubes' front faces (4.5 units out) 4.5*tan(30 deg) = 2.598, so the
// left cube's front face spans x in [-1.7, -0.7] -> columns 11..23 and the right cube's spans
// columns 41..53; both span rows 26..38. (16,32) and (48,32) sit interior to those, and (2,2) is
// outside both by a wide margin.
//
// Lighting, so the thresholds are derived rather than tuned: the front faces have normal +Z, and
// the shader's fixed light is normalize(0.4, 1.0, 0.3), so n.l = 0.268 and the shade factor is
// 0.25 + 0.75*0.268 = 0.45. A white cube vertex times baseColor times that lands the lit channel
// near 115 and the other two at 0 -- hence "> 64 and clearly dominant" rather than "> 200".
void requireTwoCubeImage(const std::vector<uint8_t>& pixels, const char* label) {
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe(label, 2, 2, corner));
    REQUIRE(isClearChannel(corner.b, kSceneClear[2]));
    REQUIRE(isClearChannel(corner.g, kSceneClear[1]));
    REQUIRE(isClearChannel(corner.r, kSceneClear[0]));
    REQUIRE(corner.a == 255);

    // The load-bearing pair. Under last-write-wins argument-table semantics both draws would use
    // the *second* item's uniforms -- same mvp, same colour -- so two blue cubes would stack on
    // the right and the left probe would come back at the clear colour. That is what this fails on.
    const Pixel left = pixelAt(pixels, 16, 32);
    INFO(describe("left cube", 16, 32, left));
    REQUIRE(left.r > 64);
    REQUIRE(left.r > left.g + 32);
    REQUIRE(left.r > left.b + 32);

    const Pixel right = pixelAt(pixels, 48, 32);
    INFO(describe("right cube", 48, 32, right));
    REQUIRE(right.b > 64);
    REQUIRE(right.b > right.r + 32);
    REQUIRE(right.b > right.g + 32);
}

} // namespace

// The milestone's key measurement (plan Amendment A2): does a draw capture the MTL4 argument
// table's bindings as they stood when *it* was encoded, or do all draws in a pass read whatever
// was written last?
//
// Two cubes, one pass, one mesh, two per-draw uniform blocks with different model matrices and
// different base colours. Encode-time capture puts a red cube on the left and a blue one on the
// right. Last-write-wins puts a single blue cube on the right and leaves the left probe at the
// clear colour. There is no third outcome, and no CPU-side inspection can distinguish them -- so
// this test, not the documentation, is the answer.
//
// It also covers the rest of the Renderer end to end: the Mesh.slang binding contract (slot 0
// vertices / slot 1 uniforms), the 144-byte column-major ObjectUniforms mirror (a transposed
// upload puts the cubes nowhere near these probes), indexed draws, and the depth attachment the
// pipeline is built with.
TEST_CASE("renderer draws per-object uniforms in one pass", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.cube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 2> items = twoCubeScene(*cube);

    CommandList& commands = (*device)->beginFrame();
    // barrierForSampling false: nothing else in this frame reads the target, and a barrier no pass
    // consumes is a dropped edge the RHI aborts on at endFrame (Amendment A4).
    (*renderer)->render(commands, sceneCamera(), items, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    requireTwoCubeImage(pixels, "clear");
}

// Proves the Renderer's depth path decides visibility, and that *draw order does not*. Two upright
// planes on the camera axis: green nearer than red. Rendering [far, near] and then [near, far]
// must give the same picture -- green.
//
// The second ordering is the one with teeth: it is the painter's-algorithm case, where a renderer
// with no depth buffer, a disabled depth test, or a depth attachment that never made it into the
// pass descriptor paints red over green and this fails. (Verified by construction: with
// depthTestEnable turned off in the Renderer's pipeline, the second ordering came back R=92 G=0.)
// The first ordering is the control that the planes are where the probe expects them at all.
TEST_CASE("renderer depth test beats draw order", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    // halfExtent 1: at the plane distances below this covers the centre probe from both depths
    // while leaving the image corners at the clear colour.
    auto plane = lmx::render::createMesh(**device, lmx::render::makePlane(1.0f), "lmx.test.plane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    // makePlane lies in XZ with a +Y normal; a quarter turn about X stands it up facing +Z, i.e.
    // square-on to a camera looking down -Z.
    const auto upright = [](float z) {
        return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
               glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    };
    const DrawItem nearGreen{
        .mesh = &*plane, .model = upright(1.0f), .baseColor = {0.0f, 1.0f, 0.0f, 1.0f}};
    const DrawItem farRed{
        .mesh = &*plane, .model = upright(-1.0f), .baseColor = {1.0f, 0.0f, 0.0f, 1.0f}};

    const std::array<std::array<DrawItem, 2>, 2> orderings = {{
        {{farRed, nearGreen}}, // back-to-front: passes even without a depth buffer
        {{nearGreen, farRed}}, // front-to-back: only depth can keep green on top
    }};
    const std::array<const char*, 2> names = {"far-then-near", "near-then-far"};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (size_t i = 0; i < orderings.size(); ++i) {
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), orderings[i], /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        // Per ordering, not once at the end: each readback has to observe its own frame.
        (*device)->waitIdle();
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

        INFO(std::string("ordering: ") + names[i]);

        // The planes' grey vertex colour (0.8) times the green base colour times the same 0.45
        // shade factor as the cubes lands the green channel near 92, red at 0.
        const Pixel center = pixelAt(pixels, 32, 32);
        INFO(describe("center", 32, 32, center));
        REQUIRE(center.g > 64);
        REQUIRE(center.g > center.r + 32);
        REQUIRE(center.g > center.b + 32);

        // Neither plane reaches the corner, so a depth attachment must not have disturbed the
        // colour clear.
        const Pixel corner = pixelAt(pixels, 2, 2);
        INFO(describe("corner", 2, 2, corner));
        REQUIRE(isClearChannel(corner.b, kSceneClear[2]));
        REQUIRE(isClearChannel(corner.g, kSceneClear[1]));
        REQUIRE(isClearChannel(corner.r, kSceneClear[0]));
    }
}

// The M2 architecture in miniature: scene pass -> textureBarrier -> second pass samples the scene
// target -> readback. This is the only case that exercises the barrier at all, and the only one
// where the Renderer runs in its *real* configuration (cpuReadback false -- the App never reads
// the scene target back on the CPU; it hands it to a second pass).
//
// The destination is cleared to magenta, a colour the scene cannot produce, so a copy pass that
// silently drew nothing fails every probe instead of coincidentally matching. (Verified: with the
// draw(3) removed, the first probe came back B=255 G=0 R=255.)
//
// What this case does *not* prove, stated plainly rather than left to be assumed: that the barrier
// is load-bearing. Re-running it with barrierForSampling false still passes on this machine -- the
// two encoders are serial on one command buffer, and this driver evidently orders them anyway. The
// case proves the barrier is correctly plumbed and does not corrupt the image; the argument for
// needing it at all is the API contract (Amendment A3), not this measurement.
TEST_CASE("renderer scene survives a barrier into a sampling pass", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.barrierCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = (*device)->createTexture({.width = kSize,
                                                 .height = kSize,
                                                 .format = Format::BGRA8Unorm,
                                                 .renderTarget = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.barrierDestination"});
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    // No depth: the copy pass is a single fullscreen triangle with nothing to occlude.
    auto copyPipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.fullscreenSamplePipeline"});
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    // Slot 0 of the argument table's *texture* index space, where Slang binds gSource
    // ([[texture(0)]] in the emitted MSL, Task 8 record) -- a separate space from the buffer slots
    // the mesh pipeline uses, so this 0 does not collide with the vertex buffer's.
    constexpr uint32_t kSourceTextureSlot = 0;

    CommandList& commands = (*device)->beginFrame();
    const std::array<DrawItem, 2> items = twoCubeScene(*cube);
    // barrierForSampling true: the copy pass below is exactly the "later pass samples the target"
    // case the barrier exists for.
    (*renderer)->render(commands, sceneCamera(), items, /*barrierForSampling=*/true);

    // The pending barrier is encoded as the first command of this pass (Amendment A3).
    commands.beginRenderPass(
        {.colorTarget = destination->get(), .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}, .clear = true});
    commands.bindPipeline(**copyPipeline);
    commands.bindTexture(kSourceTextureSlot, (*renderer)->colorTarget());
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());

    // Identical probes to the capture case: the copy is a Load at matching coordinates, so the
    // destination must be the scene image texel for texel -- including its clear colour, which is
    // also what proves the magenta clear was fully overwritten.
    requireTwoCubeImage(pixels, "copied clear");
}
