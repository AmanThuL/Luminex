#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "DisplayTransformOracle.h"
#include "Engine/Color.h"
#include "Engine/Scene.h"
#include "Engine/SceneLibrary.h"
#include "EngineTestSupport.h"
#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace lmx::engine;
using lmx::test::findRepoAsset;
using lmx::test::near3;
namespace rhi = lmx::rhi;
namespace render = lmx::render;

//======================================================================================================================
TEST_CASE("srgbToLinear decodes the exact IEC sRGB curve", "[engine]") {
    REQUIRE(srgbToLinear(0.0f) == 0.0f);
    REQUIRE(srgbToLinear(1.0f) == Catch::Approx(1.0f).margin(1e-4));
    REQUIRE(srgbToLinear(0.7f) == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(srgbToLinear(0.5f) == Catch::Approx(0.2140f).margin(1e-3));
}

//======================================================================================================================
TEST_CASE("srgbToLinear's vec3/vec4 overloads decode component-wise; vec4 keeps alpha",
          "[engine]") {
    const glm::vec3 v3 = srgbToLinear(glm::vec3(0.7f, 0.5f, 1.0f));
    REQUIRE(v3.x == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(v3.y == Catch::Approx(0.2140f).margin(1e-3));
    REQUIRE(v3.z == Catch::Approx(1.0f).margin(1e-4));

    const glm::vec4 v4 = srgbToLinear(glm::vec4(0.7f, 0.5f, 1.0f, 0.25f));
    REQUIRE(v4.x == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(v4.y == Catch::Approx(0.2140f).margin(1e-3));
    REQUIRE(v4.z == Catch::Approx(1.0f).margin(1e-4));
    REQUIRE(v4.w == 0.25f); // alpha untouched
}

namespace {

//======================================================================================================================
template <typename T>
std::string describeSceneError(const AssetResult<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

//======================================================================================================================
template <typename T>
std::string describeSceneError(const rhi::Result<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

//======================================================================================================================
// Compare all matrix elements so transform tests pin the complete round-trip.
bool matricesNear(const glm::mat4& a, const glm::mat4& b, float margin) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[col][row] - b[col][row]) > margin) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

//======================================================================================================================
TEST_CASE("decomposeTransform's rotation extraction matches modelMatrix's Y*X*Z composition "
          "order for a compound rotation",
          "[engine]") {
    SceneObject original;
    original.position = {1.0f, 2.0f, 3.0f};
    original.eulerDegrees = {35.0f, 40.0f, 25.0f}; // x, y, z -- all three axes, none of them zero
    original.scale = {2.0f, 0.5f, 1.5f};           // non-uniform, exercising decompose's scale too
    const glm::mat4 world = original.modelMatrix();

    const std::optional<DecomposedTransform> decomposed = decomposeTransform(world);
    REQUIRE(decomposed.has_value());
    REQUIRE(near3(decomposed->position, original.position));
    REQUIRE(near3(decomposed->scale, original.scale));

    SceneObject reconstructed;
    reconstructed.position = decomposed->position;
    reconstructed.eulerDegrees = decomposed->eulerDegrees;
    reconstructed.scale = decomposed->scale;
    const glm::mat4 roundTripped = reconstructed.modelMatrix();

    INFO("original eulerDegrees: (" + std::to_string(original.eulerDegrees.x) + ", " +
         std::to_string(original.eulerDegrees.y) + ", " + std::to_string(original.eulerDegrees.z) +
         ")");
    INFO("recovered eulerDegrees: (" + std::to_string(decomposed->eulerDegrees.x) + ", " +
         std::to_string(decomposed->eulerDegrees.y) + ", " +
         std::to_string(decomposed->eulerDegrees.z) + ")");
    REQUIRE(matricesNear(world, roundTripped, 1e-4f));
}

//======================================================================================================================
TEST_CASE("decomposeTransform still round-trips both fetched assets' single-axis node "
          "transforms",
          "[engine]") {
    // Sponza: a uniform scale, no rotation.
    {
        const glm::mat4 world = glm::scale(glm::mat4(1.0f), glm::vec3(0.008f));
        const std::optional<DecomposedTransform> decomposed = decomposeTransform(world);
        REQUIRE(decomposed.has_value());
        SceneObject reconstructed;
        reconstructed.position = decomposed->position;
        reconstructed.eulerDegrees = decomposed->eulerDegrees;
        reconstructed.scale = decomposed->scale;
        REQUIRE(matricesNear(world, reconstructed.modelMatrix(), 1e-5f));
    }
    // DamagedHelmet: a single 90-degree rotation about X, no translation or scale.
    {
        SceneObject source;
        source.eulerDegrees = {90.0f, 0.0f, 0.0f};
        const glm::mat4 world = source.modelMatrix();
        const std::optional<DecomposedTransform> decomposed = decomposeTransform(world);
        REQUIRE(decomposed.has_value());
        SceneObject reconstructed;
        reconstructed.position = decomposed->position;
        reconstructed.eulerDegrees = decomposed->eulerDegrees;
        reconstructed.scale = decomposed->scale;
        REQUIRE(matricesNear(world, reconstructed.modelMatrix(), 1e-4f));
    }
}

//======================================================================================================================
// Empty objects make ambient forwarding observable without constructing a GPU device.
TEST_CASE("Scene::view forwards Scene::ambient into SceneView::ambient", "[engine]") {
    Scene scene;
    scene.ambient = {0.1f, 0.2f, 0.3f};

    std::vector<render::DrawItem> items;
    const render::SceneView view =
        scene.view(items, render::ShadowFilter::PCF, /*wireframe=*/false);

    REQUIRE(near3(view.ambient, scene.ambient));
}

//======================================================================================================================
TEST_CASE("loadSponzaScene loads the fetched Sponza asset", "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf");
    if (!path) {
        SKIP("Assets/Fetched/Sponza/Sponza.gltf not present (xmake setup fetches it) -- "
             "skipping the asset-gated pin");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadSponzaScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    // The converter emits one primitive per MTL material.
    REQUIRE((*scene)->objects.size() == 25);
    REQUIRE((*scene)->materials.size() == 25);

    REQUIRE((*scene)->textures.size() == 24);

    size_t normalMapped = 0;
    for (const render::Material& material : (*scene)->materials) {
        if (material.normalMap != nullptr) {
            ++normalMapped;
        }
    }
    // The archive supplies height bumps, not tangent-space normals.
    REQUIRE(normalMapped == 0);

    REQUIRE((*scene)->boundingSphere.w > 0.0f);
    REQUIRE((*scene)->skyCubemap != nullptr);
}

//======================================================================================================================
TEST_CASE("loadHelmetScene loads the fetched DamagedHelmet asset", "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb");
    if (!path) {
        SKIP("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb not present (xmake setup fetches "
             "it) -- skipping the asset-gated pin");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadHelmetScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    REQUIRE((*scene)->objects.size() == 1);
    REQUIRE((*scene)->boundingSphere.w > 0.0f);
    REQUIRE((*scene)->skyCubemap != nullptr);

    // Damaged Helmet ships metallic-roughness, occlusion, and emissive maps; Scene.cpp's material
    // translation must upload and attach all three even though shading does not consume them yet.
    bool foundMetallicRoughness = false;
    bool foundOcclusion = false;
    bool foundEmissive = false;
    for (const render::Material& material : (*scene)->materials) {
        if (material.metallicRoughness != nullptr) {
            foundMetallicRoughness = true;
        }
        if (material.occlusion != nullptr) {
            foundOcclusion = true;
        }
        if (material.emissiveMap != nullptr) {
            foundEmissive = true;
        }
    }
    REQUIRE(foundMetallicRoughness);
    REQUIRE(foundOcclusion);
    REQUIRE(foundEmissive);
}

namespace {

//======================================================================================================================
const SceneObject* findObject(const Scene& scene, std::string_view name) {
    for (const SceneObject& object : scene.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

} // namespace

//======================================================================================================================
// Deterministic and fully code-generated -- the whole point of MaterialLab -- so this loads with
// no fetched-asset gate at all, unlike the Sponza/Helmet cases above.
TEST_CASE("loadMaterialLabScene builds a deterministic scene without fetched assets", "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    // 5x5 sphere grid (25) + 6 colour patches + 1 gradient ramp + 1 normal-map probe + 3 depth
    // probes + 1 mip probe.
    REQUIRE((*scene)->objects.size() == 37);
    // One material per sphere (25, distinct roughness/fresnelR0) + 6 patches + the ramp + the
    // normal probe + one material shared by the three depth probes + the mip probe.
    REQUIRE((*scene)->materials.size() == 35);
    // Sphere, unit quad (shared by the patches, the normal probe, and the mip probe), gradient
    // ramp quad, cube.
    REQUIRE((*scene)->meshes.size() == 4);
    // The gradient ramp, the normal map, and the mip probe's checkerboard.
    REQUIRE((*scene)->textures.size() == 3);

    size_t normalMapped = 0;
    for (const render::Material& material : (*scene)->materials) {
        if (material.normalMap != nullptr) {
            ++normalMapped;
        }
    }
    REQUIRE(normalMapped == 1);

    REQUIRE((*scene)->boundingSphere.w > 0.0f);
    REQUIRE((*scene)->skyCubemap != nullptr);

    REQUIRE(near3((*scene)->initialCamera.position, glm::vec3(0.0f, 0.0f, 80.0f)));
    REQUIRE((*scene)->initialCamera.yaw == 0.0f);
    REQUIRE((*scene)->initialCamera.pitch == 0.0f);
}

//======================================================================================================================
TEST_CASE("loadMaterialLabScene's sphere grid sweeps roughness across columns and fresnelR0 "
          "across rows",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const auto materialOf = [&](std::string_view name) -> const render::Material& {
        const SceneObject* object = findObject(**scene, name);
        REQUIRE(object != nullptr);
        return (*scene)->materials[object->materialIndex];
    };

    REQUIRE(materialOf("material-lab sphere r0c0").roughness == Catch::Approx(0.05f));
    REQUIRE(materialOf("material-lab sphere r0c4").roughness == Catch::Approx(1.0f));
    REQUIRE(materialOf("material-lab sphere r0c0").fresnelR0.x == Catch::Approx(0.04f));
    REQUIRE(materialOf("material-lab sphere r4c0").fresnelR0.x == Catch::Approx(1.0f));
    // Albedo stays white across the whole grid -- only roughness and fresnelR0 sweep.
    REQUIRE(near3(glm::vec3(materialOf("material-lab sphere r2c2").albedo), glm::vec3(1.0f)));
}

namespace {

struct ScreenBox {
    float minX, maxX, minY, maxY;
};

//======================================================================================================================
// Projects a world-space AABB's 8 corners through the camera and returns the enclosing
// screen-space box, in the same pixel convention as projectScenePixel above (row 0 = top) but
// without truncating to an integer pixel -- a partly off-screen box must stay readable as such
// rather than wrapping through uint32_t.
ScreenBox projectAabbToScreen(const render::Camera& camera, uint32_t size, const glm::vec3& center,
                              const glm::vec3& halfExtent) {
    ScreenBox box{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    const glm::mat4 viewProj = camera.projectionMatrix(1.0f) * camera.viewMatrix();
    for (float sx : {-1.0f, 1.0f}) {
        for (float sy : {-1.0f, 1.0f}) {
            for (float sz : {-1.0f, 1.0f}) {
                const glm::vec3 corner = center + glm::vec3(sx, sy, sz) * halfExtent;
                const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(size);
                const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(size);
                box.minX = std::min(box.minX, px);
                box.maxX = std::max(box.maxX, px);
                box.minY = std::min(box.minY, py);
                box.maxY = std::max(box.maxY, py);
            }
        }
    }
    return box;
}

//======================================================================================================================
bool disjoint(const ScreenBox& a, const ScreenBox& b) {
    return a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY;
}

} // namespace

//======================================================================================================================
// A pure projection check (no rendering): each depth probe's world AABB, and the sphere grid's,
// projected to screen space through initialCamera. This is what would have caught the depth
// probes' original placement, where the far probe's screen footprint sat entirely inside the mid
// probe's (hiding it completely) and the near/mid probes both clipped into the sphere grid.
TEST_CASE("loadMaterialLabScene's depth probes are simultaneously visible and occlude neither "
          "each other nor the sphere grid from initialCamera",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    render::Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;

    constexpr uint32_t kSize = 256;
    const auto boxOf = [&](std::string_view name, const glm::vec3& halfExtent) {
        const SceneObject* object = findObject(**scene, name);
        REQUIRE(object != nullptr);
        return projectAabbToScreen(camera, kSize, object->position, halfExtent);
    };

    const ScreenBox nearBox = boxOf("material-lab depth probe near", glm::vec3(0.25f));
    const ScreenBox midBox = boxOf("material-lab depth probe mid", glm::vec3(0.25f));
    const ScreenBox farBox = boxOf("material-lab depth probe far", glm::vec3(0.25f));
    // Sphere-inclusive: the grid's spheres sit at x,y in {-3,-1.5,0,1.5,3}, radius 0.5.
    const ScreenBox gridBox =
        projectAabbToScreen(camera, kSize, glm::vec3(0.0f), glm::vec3(3.5f, 3.5f, 0.5f));

    for (const ScreenBox& box : {nearBox, midBox, farBox}) {
        INFO("box: x[" + std::to_string(box.minX) + "," + std::to_string(box.maxX) + "] y[" +
             std::to_string(box.minY) + "," + std::to_string(box.maxY) + "]");
        REQUIRE(box.minX >= 0.0f);
        REQUIRE(box.maxX <= static_cast<float>(kSize));
        REQUIRE(box.minY >= 0.0f);
        REQUIRE(box.maxY <= static_cast<float>(kSize));
    }

    REQUIRE(disjoint(nearBox, midBox));
    REQUIRE(disjoint(nearBox, farBox));
    REQUIRE(disjoint(midBox, farBox));
    REQUIRE(disjoint(nearBox, gridBox));
    REQUIRE(disjoint(midBox, gridBox));
    REQUIRE(disjoint(farBox, gridBox));
}

//======================================================================================================================
// The flat region of the normal map is data (RGBA8Unorm, no lighting involved), so this copies
// the uploaded texture verbatim via Shaders/FullscreenSample.slang's `Load`-based passthrough
// rather than trying to reconstruct it from a lit render.
TEST_CASE("loadMaterialLabScene's normal-map probe encodes an exact flat {128,128,255,255} "
          "outside the bump",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* normalProbe = findObject(**scene, "material-lab normal probe");
    REQUIRE(normalProbe != nullptr);
    rhi::Texture* normalMap = (*scene)->materials[normalProbe->materialIndex].normalMap;
    REQUIRE(normalMap != nullptr);

    constexpr uint32_t kMapSize = 64;
    auto destination = (*device)->createTexture({.width = kMapSize,
                                                 .height = kMapSize,
                                                 .format = rhi::Format::BGRA8Unorm,
                                                 .renderTarget = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.materialLabNormalCopy"});
    INFO(describeSceneError(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(describeSceneError(library));
    REQUIRE(library.has_value());
    auto pipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = rhi::Format::BGRA8Unorm,
                                           .label = "lmx.test.materialLabNormalCopyPipeline"});
    INFO(describeSceneError(pipeline));
    REQUIRE(pipeline.has_value());

    rhi::CommandList& commands = (*device)->beginFrame();
    commands.beginRenderPass({.colorTarget = destination->get(),
                              .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                              .clear = true,
                              .label = "lmx.test.materialLabNormalCopy"});
    commands.bindPipeline(**pipeline);
    commands.bindTexture(0, *normalMap);
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kMapSize} * kMapSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());

    // Texel (0,0) is well outside the centred bump radius (28 texels from the (31.5,31.5)
    // centre) -- flat. Sorted so the assertion does not depend on the destination's BGRA channel
    // order, only on the multiset of bytes {128,128,255,255} being present.
    std::array<uint8_t, 4> corner = {pixels[0], pixels[1], pixels[2], pixels[3]};
    std::sort(corner.begin(), corner.end());
    INFO("corner texel bytes (sorted): " + std::to_string(corner[0]) + "," +
         std::to_string(corner[1]) + "," + std::to_string(corner[2]) + "," +
         std::to_string(corner[3]));
    REQUIRE(corner == std::array<uint8_t, 4>{128, 128, 255, 255});
}

//======================================================================================================================
TEST_CASE("loadSponzaScene's full SceneView renders through Renderer without exhausting the "
          "uniform ring",
          "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf");
    if (!path) {
        SKIP("Assets/Fetched/Sponza/Sponza.gltf not present (xmake setup fetches it) -- "
             "skipping the asset-gated pin");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadSponzaScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    render::Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;

    std::vector<render::DrawItem> items;
    const render::SceneView view =
        (*scene)->view(items, render::ShadowFilter::PCF, /*wireframe=*/false);
    REQUIRE(items.size() == 25);

    rhi::CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    // The asset camera must produce pixels beyond the clear. The clear is authored in display
    // space and decoded once at pass declaration, so what lands in the target is that colour
    // through the display transform -- and readback hands back BGRA, so the channels arrive
    // reversed from the authored order.
    const std::array<int, 3> clearBytes = lmx::test::displayBytes(
        {lmx::test::srgbDecode(0.05f), lmx::test::srgbDecode(0.07f), lmx::test::srgbDecode(0.10f)});
    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    bool sawNonClearPixel = false;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] != clearBytes[2] || pixels[i + 1] != clearBytes[1] ||
            pixels[i + 2] != clearBytes[0]) {
            sawNonClearPixel = true;
            break;
        }
    }
    CHECK(sawNonClearPixel);
}

namespace {

//======================================================================================================================
render::MeshData makeUvQuad(float halfExtent) {
    render::MeshData mesh;
    struct Corner {
        float x, y, u, v;
    };
    constexpr Corner kCorners[4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f, 0.0f},
    };
    for (const Corner& c : kCorners) {
        mesh.vertices.push_back({c.x * halfExtent, c.y * halfExtent, 0.0f, // position
                                 0.0f, 0.0f, 1.0f,                         // normal: +Z
                                 1.0f, 0.0f, 0.0f, 1.0f,                   // tangent
                                 c.u, c.v});
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

struct ProjectedPixel {
    uint32_t x = 0, y = 0;
};

//======================================================================================================================
// Same clip -> pixel convention Tests/GpuSmokeTests.cpp's own projectToPixel uses: row 0 is the
// top of a readback, so a clip-space +y maps to a small row index.
ProjectedPixel projectScenePixel(const render::Camera& camera, uint32_t size,
                                 const glm::vec3& world) {
    const glm::vec4 clip =
        camera.projectionMatrix(1.0f) * camera.viewMatrix() * glm::vec4(world, 1.0f);
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(size);
    const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(size);
    return {static_cast<uint32_t>(px), static_cast<uint32_t>(py)};
}

} // namespace

//======================================================================================================================
TEST_CASE("Sponza materials with distinct diffuse textures render distinct colours in one pass",
          "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf");
    if (!path) {
        SKIP("Assets/Fetched/Sponza/Sponza.gltf not present (xmake setup fetches it) -- "
             "skipping the asset-gated pin");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadSponzaScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    // Sample up to eight textures so at least one pair exposes a per-draw binding collision.
    std::vector<rhi::Texture*> distinctDiffuse;
    for (const render::Material& material : (*scene)->materials) {
        if (material.diffuse == nullptr) {
            continue;
        }
        const bool alreadySeen = std::find(distinctDiffuse.begin(), distinctDiffuse.end(),
                                           material.diffuse) != distinctDiffuse.end();
        if (!alreadySeen) {
            distinctDiffuse.push_back(material.diffuse);
        }
        if (distinctDiffuse.size() == 8) {
            break;
        }
    }
    REQUIRE(distinctDiffuse.size() >= 2);

    auto quad = render::createMesh(**device, makeUvQuad(0.45f), "lmx.test.sponzaDiffuseQuad");
    INFO(describeSceneError(quad));
    REQUIRE(quad.has_value());

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    const float spacing = 1.2f;
    const float startX = -spacing * static_cast<float>(distinctDiffuse.size() - 1) * 0.5f;
    // The 20% distance margin keeps the outer probes away from frustum-edge rounding.
    render::Camera camera;
    const float halfWidth = -startX + 0.45f;
    camera.position = {0.0f, 0.0f, 1.2f * halfWidth / glm::tan(camera.fovY * 0.5f)};
    std::vector<glm::vec3> centers;
    std::vector<render::DrawItem> items;
    items.reserve(distinctDiffuse.size());
    for (size_t i = 0; i < distinctDiffuse.size(); ++i) {
        const glm::vec3 center{startX + spacing * static_cast<float>(i), 0.0f, 0.0f};
        centers.push_back(center);
        render::Material material; // default albedo/roughness/fresnel; only diffuse differs
        material.diffuse = distinctDiffuse[i];
        items.push_back({.mesh = &*quad,
                         .model = glm::translate(glm::mat4(1.0f), center),
                         .material = material});
    }

    render::SceneView view;
    view.items = items;
    // Ambient-only lighting isolates texture differences from directional shading.
    view.ambient = {1.0f, 1.0f, 1.0f};
    for (render::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, 0.0f, 0.0f,
                           spacing * static_cast<float>(distinctDiffuse.size()) + 2.0f};

    rhi::CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const auto pixelAt = [&](uint32_t x, uint32_t y) {
        const size_t offset = (size_t{y} * kProbeSize + x) * 4;
        return std::array<uint8_t, 3>{pixels[offset], pixels[offset + 1], pixels[offset + 2]};
    };
    const auto colorDistance = [](const std::array<uint8_t, 3>& a,
                                  const std::array<uint8_t, 3>& b) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(int{a[c]} - int{b[c]});
        }
        return sum;
    };

    std::vector<std::array<uint8_t, 3>> probes;
    for (const glm::vec3& center : centers) {
        const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, center);
        REQUIRE(coord.x < kProbeSize);
        REQUIRE(coord.y < kProbeSize);
        probes.push_back(pixelAt(coord.x, coord.y));
    }

    int maxDistance = 0;
    for (size_t i = 0; i < probes.size(); ++i) {
        for (size_t j = i + 1; j < probes.size(); ++j) {
            maxDistance = std::max(maxDistance, colorDistance(probes[i], probes[j]));
        }
    }
    INFO("largest pairwise colour distance among " + std::to_string(probes.size()) +
         " Sponza diffuse quads: " + std::to_string(maxDistance));
    // A binding collision makes every probe identical; real texture variation clears this margin.
    REQUIRE(maxDistance > 30);
}

//======================================================================================================================
// The whole colour pipeline end to end, on the scene built to hold it still. Ambient 1.0 with
// every directional light dark and no sky bound makes the fragment's linear output exactly the
// material's albedo -- which the scene decoded from an authored sRGB constant -- so each patch's
// readback must be that constant decoded, tone mapped, and encoded, with nothing else in between.
// A second decode or a second encode anywhere on that path moves every patch off its number.
//
// Derived expectations at exposure 0, from Tests/DisplayTransformOracle.h (bytes, rounded):
//   red   (1,0,0)      -> linear (1, 0, 0)          -> 241, 33, 33
//   green (0,1,0)      -> linear (0, 1, 0)          -> 33, 241, 33
//   blue  (0,0,1)      -> linear (0, 0, 1)          -> 33, 33, 241
//   gray18 (0.46)      -> linear 0.1789             -> 104, 104, 104
//   white (1,1,1)      -> linear 1.0                -> 240, 240, 240
//   black (0,0,0)      -> linear 0.0                -> 0, 0, 0
// The saturated primaries land on 33 in their two dark channels rather than 0 because their peak
// channel is above the tone map's 0.76 shoulder, where it desaturates toward the compressed peak;
// the achromatic patches sit below it and lose only the constant 0.04 black offset.
TEST_CASE("loadMaterialLabScene's known-colour patches round-trip the display transform and its "
          "gradient ramp reads back monotonic",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    struct Patch {
        const char* name;
        glm::vec3 authoredSrgb;
    };
    const std::array<Patch, 6> kPatches = {{
        {"red", {1.0f, 0.0f, 0.0f}},
        {"green", {0.0f, 1.0f, 0.0f}},
        {"blue", {0.0f, 0.0f, 1.0f}},
        {"gray18", {0.46f, 0.46f, 0.46f}},
        {"white", {1.0f, 1.0f, 1.0f}},
        {"black", {0.0f, 0.0f, 0.0f}},
    }};

    const SceneObject* ramp = findObject(**scene, "material-lab gradient ramp");
    REQUIRE(ramp != nullptr);

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    // A bespoke camera framing just the patch row and the ramp beneath it -- the sphere grid,
    // normal probe, and depth probes sit outside this frustum and are not drawn here at all.
    render::Camera camera;
    camera.position = {0.0f, -5.25f, 8.5f};
    camera.fovY = glm::radians(45.0f);
    camera.nearZ = 0.1f;
    camera.farZ = 20.0f;

    std::vector<render::DrawItem> items;
    std::vector<glm::vec3> patchPositions;
    for (const Patch& patch : kPatches) {
        const SceneObject* object =
            findObject(**scene, std::string("material-lab patch ") + patch.name);
        REQUIRE(object != nullptr);
        patchPositions.push_back(object->position);
        items.push_back({.mesh = &(*scene)->meshes[object->meshIndex],
                         .model = object->modelMatrix(),
                         .material = (*scene)->materials[object->materialIndex]});
    }
    items.push_back({.mesh = &(*scene)->meshes[ramp->meshIndex],
                     .model = ramp->modelMatrix(),
                     .material = (*scene)->materials[ramp->materialIndex]});

    render::SceneView view;
    view.items = items;
    // Unit ambient makes the fragment's linear output the albedo itself; no sky is bound, so the
    // environment term is black and contributes nothing either.
    view.ambient = {1.0f, 1.0f, 1.0f};
    for (render::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, -5.25f, 0.0f, 5.0f};

    rhi::CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // Readback hands back BGRA, so the channel at offset 0 is blue.
    const auto channelAt = [&](uint32_t x, uint32_t y, size_t channel) -> uint8_t {
        const size_t offset = (size_t{y} * kProbeSize + x) * 4;
        return pixels[offset + channel];
    };
    constexpr std::array<size_t, 3> kRgbOffsets = {2, 1, 0};

    for (size_t i = 0; i < kPatches.size(); ++i) {
        const glm::vec3 linear{srgbToLinear(kPatches[i].authoredSrgb)};
        const std::array<int, 3> want = lmx::test::displayBytes(linear);
        const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, patchPositions[i]);
        REQUIRE(coord.x < kProbeSize);
        REQUIRE(coord.y < kProbeSize);
        for (size_t channel = 0; channel < 3; ++channel) {
            const int value = channelAt(coord.x, coord.y, kRgbOffsets[channel]);
            INFO(std::string(kPatches[i].name) + " patch channel " + std::to_string(channel) +
                 " = " + std::to_string(value) + ", expected " + std::to_string(want[channel]));
            REQUIRE(std::abs(value - want[channel]) <= 3);
        }
    }

    // Gradient ramp: byte i in all channels at texel column i, sampled left to right. The readback
    // must be non-decreasing end to end, with a wide spread -- a probe that would also pass
    // against a flat or empty ramp is not discriminating anything -- and no adjacent pair may jump
    // by more than kMaxRampStep, which is what a posterised intermediate would show up as.
    //
    // kMaxRampStep is derived, not measured: these 24 probes span authored bytes 4..251, so the
    // ideal chain moves 15.2 bytes at its steepest adjacent pair (just above the tone map's 0.08
    // knee, where the black offset's slope in encoded space peaks). 22 leaves margin for the
    // ramp's bilinear filtering and the target's own rounding without admitting a visible band.
    constexpr int kRampSamples = 24;
    constexpr int kMaxRampStep = 22;
    std::vector<int> samples;
    for (int i = 0; i < kRampSamples; ++i) {
        const float worldX = -2.9f + static_cast<float>(i) * (5.8f / (kRampSamples - 1));
        const glm::vec3 world{worldX, ramp->position.y, ramp->position.z};
        const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, world);
        REQUIRE(coord.x < kProbeSize);
        REQUIRE(coord.y < kProbeSize);
        samples.push_back(channelAt(coord.x, coord.y, /*channel=*/0));
    }
    for (size_t i = 1; i < samples.size(); ++i) {
        INFO("ramp sample " + std::to_string(i - 1) + "=" + std::to_string(samples[i - 1]) +
             ", sample " + std::to_string(i) + "=" + std::to_string(samples[i]));
        REQUIRE(samples[i] >= samples[i - 1]);
        REQUIRE(samples[i] - samples[i - 1] <= kMaxRampStep);
    }
    INFO("ramp spread: " + std::to_string(samples.front()) + " -> " +
         std::to_string(samples.back()));
    REQUIRE(samples.back() - samples.front() > 100);
}

//======================================================================================================================
// Exit-gate check: Metal's blit generateMipmaps was measured to point-pick, not filter, so a
// point-picked mip of MaterialLab's 1-texel checkerboard (makeCheckerboardPixels) reads solid
// black or solid white -- stepping by a power of two always lands on the same parity. A correctly
// box-filtered chain (Source/Engine/TextureBake.h's bakeMips, the same function the offline bake
// tool uses) instead converges every level above 0 to an exact uniform mid-gray, because every 2x2
// block of a 1-texel checkerboard contains exactly two black and two white texels.
//
// The bespoke camera sits 10 world units from the probe -- a 1x1 unit quad at that distance,
// against a 64px target and 45-degree vertical FOV, covers roughly 8 screen pixels while sampling
// a 64-texel-wide texture, comfortably selecting a mip level above 0 (texel/pixel ratio ~8, so LOD
// ~3) while still covering enough pixels that rasterization cannot miss every sample. The probe's
// own material has diffuse = the checkerboard texture (sRGB) and albedo = white; with ambient =
// (1,1,1) and every light off (the same "no light, no sky, ambient times diffuse" configuration as
// this file's white-patch test above), the shaded linear value is exactly the sampled texel: 0.5.
// sRGB-encoded, linear 0.5 lands at byte ~188 -- the same value pinned by this file's white-patch
// test and by Tests/GpuRendererTests.cpp's encode oracle, derived there from the identical
// linearToSrgb(0.5) computation.
TEST_CASE("loadMaterialLabScene's mip probe converges to mid-gray under strong minification, "
          "proving its mips are filtered rather than point-picked",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* probe = findObject(**scene, "material-lab mip probe");
    REQUIRE(probe != nullptr);

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    // 10 world units back from the probe, on the +Z side its quad normal faces (the probe's front
    // face is invisible from initialCamera on the other side by construction -- see
    // MaterialLab.cpp's file-level comment).
    render::Camera camera;
    camera.position = probe->position + glm::vec3(0.0f, 0.0f, 10.0f);
    camera.fovY = glm::radians(45.0f);
    camera.nearZ = 0.1f;
    camera.farZ = 20.0f;

    std::vector<render::DrawItem> items;
    items.push_back({.mesh = &(*scene)->meshes[probe->meshIndex],
                     .model = probe->modelMatrix(),
                     .material = (*scene)->materials[probe->materialIndex]});

    render::SceneView view;
    view.items = items;
    view.ambient = {1.0f, 1.0f, 1.0f};
    for (render::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {probe->position.x, probe->position.y, probe->position.z, 2.0f};

    rhi::CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, probe->position);
    REQUIRE(coord.x < kProbeSize);
    REQUIRE(coord.y < kProbeSize);
    const size_t offset = (static_cast<size_t>(coord.y) * kProbeSize + coord.x) * 4;

    for (size_t channel = 0; channel < 3; ++channel) {
        const int value = pixels[offset + channel];
        INFO("mip probe channel " + std::to_string(channel) + " = " + std::to_string(value));
        REQUIRE(std::abs(value - 188) <= 6);
        // Point-picking this pattern reads solid black (0) or solid white (255); mid-gray is far
        // from both, so these hold with wide margin for a correctly filtered chain and fail for a
        // point-picked one.
        REQUIRE(value > 40);
        REQUIRE(value < 215);
    }
}

//======================================================================================================================
TEST_CASE("scene IDs are stable and reject unknown input", "[engine]") {
    REQUIRE(sceneIdString(*parseSceneId("sponza")) == "sponza");
    REQUIRE(sceneIdString(*parseSceneId("damaged-helmet")) == "damaged-helmet");
    REQUIRE(sceneIdString(*parseSceneId("material-lab")) == "material-lab");
    REQUIRE_FALSE(parseSceneId("3"));
    REQUIRE_FALSE(parseSceneId("Sponza"));
    REQUIRE(sceneIdString(defaultSceneId()) == "sponza");
}

//======================================================================================================================
TEST_CASE("SceneLibrary lists the three scenes in a fixed order", "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    SceneLibrary library(**device);

    REQUIRE(library.entries().size() == 3);
    REQUIRE(sceneIdString(library.entries()[0].id) == "sponza");
    REQUIRE(library.entries()[0].stableId == "sponza");
    REQUIRE(library.entries()[0].displayName == "Sponza");
    REQUIRE(sceneIdString(library.entries()[1].id) == "damaged-helmet");
    REQUIRE(library.entries()[1].stableId == "damaged-helmet");
    REQUIRE(sceneIdString(library.entries()[2].id) == "material-lab");
    REQUIRE(library.entries()[2].stableId == "material-lab");
    REQUIRE(library.entries()[2].displayName == "MaterialLab");
    REQUIRE(library.entries()[2].role == SceneRole::Diagnostic);
}

//======================================================================================================================
TEST_CASE("SceneLibrary reports the fetched scenes' availability from what this checkout has",
          "[gpu]") {
    // Not asset-gated in the usual sense: it checks that `available` agrees with whatever this
    // checkout actually has (present or not), so it is meaningful either way rather than being
    // skipped when the fetched assets are missing.
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    SceneLibrary library(**device);

    const bool sponzaPresent = findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf").has_value();
    REQUIRE(library.entries()[0].available == sponzaPresent);
    REQUIRE(library.entries()[0].hint.empty() == sponzaPresent);

    const bool helmetPresent =
        findRepoAsset("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb").has_value();
    REQUIRE(library.entries()[1].available == helmetPresent);
    REQUIRE(library.entries()[1].hint.empty() == helmetPresent);

    // MaterialLab is fully code-generated: available regardless of what this checkout has fetched.
    REQUIRE(library.entries()[2].available);
    REQUIRE(library.entries()[2].hint.empty());
}

//======================================================================================================================
TEST_CASE("SceneLibrary::get lazily loads a scene once and caches the instance", "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf");
    if (!path) {
        SKIP("Assets/Fetched/Sponza/Sponza.gltf not present (xmake setup fetches it)");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    SceneLibrary library(**device);

    auto first = library.get(defaultSceneId());
    INFO(describeSceneError(first));
    REQUIRE(first.has_value());
    auto second = library.get(defaultSceneId());
    REQUIRE(second.has_value());
    REQUIRE(*first == *second); // the same cached Scene*, not rebuilt
}
