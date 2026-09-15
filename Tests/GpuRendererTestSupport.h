#pragma once

#include "BrdfOracle.h"
#include "DisplayTransformOracle.h"
#include "GpuTestSupport.h"
#include "Scene/IblUpload.h"

#include "Asset/Ibl.h"
#include "EngineTestSupport.h"
#include "Scene/Scene.h"

#include <catch2/catch_approx.hpp>

#include "SceneTableTestSupport.h"
#include <cmath>
#include <cstring>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

namespace {

using lmx::render::Camera;
using lmx::render::DirectionalLight;
using lmx::render::Renderer;
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::FixtureSceneView;

[[maybe_unused]] constexpr std::array<float, 4> kSceneClear = {0.05f, 0.07f, 0.10f, 1.0f};

[[maybe_unused]] constexpr float kCubeOffsetX = 1.2f;

//======================================================================================================================
inline Camera sceneCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    return camera;
}

//======================================================================================================================
inline FixtureSceneView litSceneView(std::span<const FixtureDrawItem> items) {
    FixtureSceneView view;
    view.items = items;
    view.lights[0] = {.strength = {0.5f, 0.5f, 0.5f}, .direction = {0.0f, 0.0f, -1.0f}};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    return view;
}

//======================================================================================================================
// kSceneClear is authored in display space, and the renderer decodes it once when it declares the
// scene pass. It therefore reaches the display target the same way a fragment writing that linear
// colour would -- through the tone map and the encode -- rather than landing in the target
// verbatim, which is what it did while the scene pass owned the encode.
inline std::array<int, 3> sceneClearBytes() {
    return lmx::test::displayBytes({lmx::test::srgbDecode(kSceneClear[0]),
                                    lmx::test::srgbDecode(kSceneClear[1]),
                                    lmx::test::srgbDecode(kSceneClear[2])});
}

//======================================================================================================================
inline void requireClearPixel(const Pixel& pixel) {
    const std::array<int, 3> want = sceneClearBytes();
    REQUIRE(channelNear(pixel.r, want[0], 2));
    REQUIRE(channelNear(pixel.g, want[1], 2));
    REQUIRE(channelNear(pixel.b, want[2], 2));
}

//======================================================================================================================
inline std::array<FixtureDrawItem, 2> twoCubeScene(const FixtureMesh& cube) {
    return {{
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{-kCubeOffsetX, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}},
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{kCubeOffsetX, 0.0f, 0.0f}),
         .material = {.albedo = {0.0f, 0.0f, 1.0f, 1.0f}}},
    }};
}

//======================================================================================================================
inline void requireTwoCubeImage(const std::vector<uint8_t>& pixels, const char* label) {
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe(label, 2, 2, corner));
    requireClearPixel(corner);
    REQUIRE(corner.a == 255);

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

namespace {

[[maybe_unused]] constexpr uint32_t kShadowMapSize = 256;

[[maybe_unused]] constexpr uint32_t kShadowVertexSlot = 0;
[[maybe_unused]] constexpr uint32_t kShadowPassSlot = 2;
[[maybe_unused]] constexpr uint32_t kShadowMapSlot = 3;
[[maybe_unused]] constexpr uint32_t kShadowSamplerSlot = 1;

[[maybe_unused]] constexpr int32_t kFilterPcf = 0;
[[maybe_unused]] constexpr int32_t kFilterPcss = 1;

struct ShadowPassUniforms {
    glm::mat4 lightViewProj;
    glm::mat4 shadowTransform;
    int32_t filter = 0;
    int32_t pad[3] = {0, 0, 0};
};
static_assert(sizeof(ShadowPassUniforms) == 144, "must match ShadowSmoke.slang's PassUniforms");

struct ShadowVertex {
    float x = 0.f, y = 0.f, z = 0.f;
};
static_assert(sizeof(ShadowVertex) == 12, "must match Slang's packed_float3 Vertex layout");

//======================================================================================================================
inline std::array<ShadowVertex, 6> shadowQuad(float x0, float x1, float y0, float y1, float z) {
    return {{
        {x0, y0, z},
        {x1, y0, z},
        {x1, y1, z},
        {x0, y0, z},
        {x1, y1, z},
        {x0, y1, z},
    }};
}

//======================================================================================================================
inline Pixel shadowPixelAt(const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * kShadowMapSize + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

//======================================================================================================================
inline size_t differingTexels(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t differing = 0;
    for (uint32_t y = 0; y < kShadowMapSize; ++y) {
        for (uint32_t x = 0; x < kShadowMapSize; ++x) {
            const int lhs = shadowPixelAt(a, x, y).r;
            const int rhs = shadowPixelAt(b, x, y).r;
            if (std::abs(lhs - rhs) > 8) {
                ++differing;
            }
        }
    }
    return differing;
}

} // namespace

namespace {

[[maybe_unused]] constexpr uint32_t kSceneProbeSize = 128;

//======================================================================================================================
inline Pixel pixelAtWidth(const std::vector<uint8_t>& bgra, uint32_t width, uint32_t x,
                          uint32_t y) {
    const size_t offset = (size_t{y} * width + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

struct PixelCoord {
    uint32_t x = 0, y = 0;
};

//======================================================================================================================
inline PixelCoord projectToPixel(const Camera& camera, uint32_t size, const glm::vec3& world) {
    const glm::vec4 clip =
        camera.projectionMatrix(1.0f) * camera.viewMatrix() * glm::vec4(world, 1.0f);
    REQUIRE(clip.w > 0.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    INFO("ndc (" + std::to_string(ndc.x) + ", " + std::to_string(ndc.y) + ")");
    REQUIRE(std::abs(ndc.x) < 1.0f);
    REQUIRE(std::abs(ndc.y) < 1.0f);
    return {static_cast<uint32_t>((ndc.x * 0.5f + 0.5f) * static_cast<float>(size)),
            static_cast<uint32_t>((0.5f - ndc.y * 0.5f) * static_cast<float>(size))};
}

//======================================================================================================================
inline lmx::render::Vertex clipVertex(float x, float y, float z) {
    return {x, y, z, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
}

} // namespace

namespace {

//======================================================================================================================
// Bit pattern of `value` in binary16 -- the layout an RGBA16Float readback hands back. The
// round-trip check pins every caller to a value binary16 holds exactly, which is what lets the
// probes below compare readback bits for equality rather than within a tolerance.
inline uint16_t halfBits(float value) {
    const _Float16 half = static_cast<_Float16>(value);
    REQUIRE(static_cast<float>(half) == value);
    uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

//======================================================================================================================
// The other direction, for values binary16 only approximates -- a decoded clear colour, say.
inline float floatOfHalfBits(uint16_t bits) {
    _Float16 half = 0;
    std::memcpy(&half, &bits, sizeof(half));
    return static_cast<float>(half);
}

// One RGBA16Float texel, in the channel order readback() produces.
struct HalfPixel {
    uint16_t r = 0, g = 0, b = 0, a = 0;
};

//======================================================================================================================
inline HalfPixel halfPixelAt(const std::vector<uint16_t>& rgba, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * kSize + x) * 4;
    return {rgba[offset], rgba[offset + 1], rgba[offset + 2], rgba[offset + 3]};
}

} // namespace

namespace {

// MaterialLab's three depth probes, mirrored from Source/Scene/MaterialLab.cpp: 0.5-unit cubes in
// the X=28 depth lane at these distances, offset laterally so each occupies its own tangent-space
// band. `distance` is to the cube's *centre*; the surface the camera sees is its front face, one
// half-extent nearer.
struct DepthProbe {
    const char* name;
    float distance;
    float lateralOffset;
};
[[maybe_unused]] constexpr std::array<DepthProbe, 3> kMaterialLabDepthProbes = {{
    {"near", 2.0f, -0.4f},
    {"mid", 10.0f, 1.0f},
    {"far", 40.0f, 7.0f},
}};
[[maybe_unused]] constexpr float kMaterialLabCameraDistance = 12.0f;
[[maybe_unused]] constexpr float kMaterialLabDepthLaneX = 28.0f;
[[maybe_unused]] constexpr float kDepthProbeHalfExtent = 0.25f;

// Big enough that the farthest probe's front face is several pixels across: it subtends
// 2 * 0.25 / 39.75 = 0.0126 radians of tangent against a half-FOV tangent of tan(22.5 degrees),
// which is 3% of the frame, so 512 puts about 15 pixels on it and its centre nowhere near an edge.
[[maybe_unused]] constexpr uint32_t kDepthReconstructSize = 512;

} // namespace

namespace {

[[maybe_unused]] constexpr uint32_t kBrdfProbeSize = 128;

//======================================================================================================================
// One RGBA16Float texel of an arbitrarily sized target, as float.
inline glm::vec3 hdrTexelAt(const std::vector<uint16_t>& rgba, uint32_t width, uint32_t x,
                            uint32_t y) {
    const size_t offset = (size_t{y} * width + x) * 4;
    const auto decode = [&](size_t channel) {
        _Float16 half = 0;
        std::memcpy(&half, &rgba[offset + channel], sizeof(half));
        return static_cast<float>(half);
    };
    return {decode(0), decode(1), decode(2)};
}

//======================================================================================================================
// A camera far enough back, with a narrow enough field of view, that the view direction is the same
// vector across the whole probe: the pinned-angle cases below state N.V exactly, so a probe pixel
// whose own view ray had drifted a degree off the axis would be comparing against the wrong angle.
// At 60 units and 5 degrees, half a pixel of a 128-wide target subtends 0.0007 radians.
inline Camera pinnedAngleCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 60.0f};
    camera.fovY = glm::radians(5.0f);
    camera.nearZ = 1.0f;
    camera.farZ = 200.0f;
    return camera;
}

} // namespace
