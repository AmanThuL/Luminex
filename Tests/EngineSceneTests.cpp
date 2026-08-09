#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    // probes.
    REQUIRE((*scene)->objects.size() == 36);
    // One material per sphere (25, distinct roughness/fresnelR0) + 6 patches + the ramp + the
    // normal probe + one material shared by the three depth probes.
    REQUIRE((*scene)->materials.size() == 34);
    // Sphere, unit quad (shared by the patches and the normal probe), gradient ramp quad, cube.
    REQUIRE((*scene)->meshes.size() == 4);
    // The gradient ramp and the normal map are the scene's only two textures.
    REQUIRE((*scene)->textures.size() == 2);

    size_t normalMapped = 0;
    for (const render::Material& material : (*scene)->materials) {
        if (material.normalMap != nullptr) {
            ++normalMapped;
        }
    }
    REQUIRE(normalMapped == 1);

    REQUIRE((*scene)->boundingSphere.w > 0.0f);
    REQUIRE((*scene)->skyCubemap != nullptr);

    REQUIRE(near3((*scene)->initialCamera.position, glm::vec3(0.0f, 0.0f, 12.0f)));
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

    // The asset camera must produce pixels beyond the raw hardware clear value (13, 18, 26).
    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    bool sawNonClearPixel = false;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] != 13 || pixels[i + 1] != 18 || pixels[i + 2] != 26) {
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
// Ambient-only lighting isolates albedo/texture from directional shading, the same isolation
// Tests/GpuRendererTests.cpp's sRGB-encode oracle uses (linear 0.5 -> byte 188).
TEST_CASE("loadMaterialLabScene's white patch round-trips sRGB and its gradient ramp reads back "
          "monotonic",
          "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* whitePatch = findObject(**scene, "material-lab patch white");
    REQUIRE(whitePatch != nullptr);
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
    items.push_back({.mesh = &(*scene)->meshes[whitePatch->meshIndex],
                     .model = whitePatch->modelMatrix(),
                     .material = (*scene)->materials[whitePatch->materialIndex]});
    items.push_back({.mesh = &(*scene)->meshes[ramp->meshIndex],
                     .model = ramp->modelMatrix(),
                     .material = (*scene)->materials[ramp->materialIndex]});

    render::SceneView view;
    view.items = items;
    view.ambient = {0.5f, 0.5f, 0.5f};
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

    const auto channelAt = [&](uint32_t x, uint32_t y, size_t channel) -> uint8_t {
        const size_t offset = (size_t{y} * kProbeSize + x) * 4;
        return pixels[offset + channel];
    };

    // White patch: albedo 1 times ambient 0.5, sRGB-encoded -- the same expectation as the
    // encode oracle. Channel order is left unspecified on purpose: white makes every channel
    // equal, so the probe does not need to know it.
    const ProjectedPixel patchPixel = projectScenePixel(camera, kProbeSize, whitePatch->position);
    REQUIRE(patchPixel.x < kProbeSize);
    REQUIRE(patchPixel.y < kProbeSize);
    for (size_t channel = 0; channel < 3; ++channel) {
        const int value = channelAt(patchPixel.x, patchPixel.y, channel);
        INFO("white patch channel " + std::to_string(channel) + " = " + std::to_string(value));
        REQUIRE(std::abs(value - 188) <= 6);
    }

    // Gradient ramp: byte i in all channels at texel column i, sampled left to right. The
    // readback must be non-decreasing end to end, with a wide spread -- a probe that would also
    // pass against a flat or empty ramp is not discriminating anything.
    std::vector<int> samples;
    for (int i = 0; i < 8; ++i) {
        const float worldX = -2.9f + static_cast<float>(i) * (5.8f / 7.0f);
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
    }
    INFO("ramp spread: " + std::to_string(samples.front()) + " -> " +
         std::to_string(samples.back()));
    REQUIRE(samples.back() - samples.front() > 100);
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
