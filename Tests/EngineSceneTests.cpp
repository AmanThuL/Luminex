#include "App/Model/SceneSession.h"
#include "EngineSceneTestSupport.h"
#include "Render/LocalLightMath.h"
#include "Scene/SponzaLightRig.h"
#include "SceneTableTestSupport.h"

#include <cstring>

#include <set>

namespace {

//======================================================================================================================
std::vector<render::InstanceRow> readSceneInstances(Scene& scene, rhi::Device& device) {
    device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()));
    const auto tables = scene.tables();
    device.endFrame(nullptr);
    device.waitIdle();
    std::vector<render::InstanceRow> rows(tables.instanceCount);
    tables.instances->readback(rows.data(), rows.size() * sizeof(render::InstanceRow));
    return rows;
}

} // namespace

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
    REQUIRE((*scene)->tableStats().materialCount == 25);

    std::set<rhi::Texture*> textures;
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        for (const auto id : {material.diffuse, material.normalMap, material.metallicRoughness,
                              material.occlusion, material.emissiveMap}) {
            if (id) {
                auto* texture = (*scene)->tryTexture(*id);
                REQUIRE(texture != nullptr);
                textures.insert(texture);
            }
        }
    }
    REQUIRE(textures.size() == 24);

    size_t normalMapped = 0;
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        if (material.normalMap.has_value()) {
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
    // translation must upload and attach all three for the GGX shader to consume.
    bool foundMetallicRoughness = false;
    bool foundOcclusion = false;
    bool foundEmissive = false;
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        if (material.metallicRoughness.has_value()) {
            foundMetallicRoughness = true;
        }
        if (material.occlusion.has_value()) {
            foundOcclusion = true;
        }
        if (material.emissiveMap.has_value()) {
            foundEmissive = true;
        }
    }
    REQUIRE(foundMetallicRoughness);
    REQUIRE(foundOcclusion);
    REQUIRE(foundEmissive);
}

//======================================================================================================================
// The unbaked fallback runs the identical bakeMips box filter in-process rather than leaving mip
// levels above 0 as undefined GPU memory (a minified sample would otherwise read stale VRAM), so
// this proves the two paths agree: load Helmet normally (baked DDS present), then again with
// Baked/ renamed aside (forcing the fallback), and require the two textures' level-1 mips are
// byte-identical -- compared as hashes so a mismatch stays diagnosable rather than asking Catch2
// to print a 16KB byte vector (see Tests/EngineAssetTests.cpp's determinism test for the same
// reasoning). Both loads start from the same stb_image-decoded JPEG bytes and run through the
// same bakeMips code (Source/Asset/TextureBake.h), so equality is exact, not approximate.
TEST_CASE("loadHelmetScene's unbaked fallback computes the same mip 1 the offline bake would",
          "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb");
    if (!path) {
        SKIP("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb not present (xmake setup fetches "
             "it) -- skipping the asset-gated pin");
    }
    const std::filesystem::path bakedDir = path->parent_path() / "Baked";
    if (!std::filesystem::exists(bakedDir)) {
        SKIP("Assets/Fetched/DamagedHelmet/Baked not present (xmake setup bakes it) -- skipping "
             "the asset-gated pin");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());

    auto bakedScene = loadHelmetScene(**device);
    INFO(describeSceneError(bakedScene));
    REQUIRE(bakedScene.has_value());
    rhi::Texture* bakedDiffuse =
        (*bakedScene)
            ->tryTexture(*(*bakedScene)->material((*bakedScene)->objects[0].material).diffuse);
    REQUIRE(bakedDiffuse != nullptr);
    const std::vector<uint8_t> bakedMip1 = readMipLevel1(**device, *bakedDiffuse);

    std::vector<uint8_t> fallbackMip1;
    {
        const TemporarilyHiddenDirectory hidden(bakedDir);
        auto fallbackScene = loadHelmetScene(**device);
        INFO(describeSceneError(fallbackScene));
        REQUIRE(fallbackScene.has_value());
        rhi::Texture* fallbackDiffuse =
            (*fallbackScene)
                ->tryTexture(
                    *(*fallbackScene)->material((*fallbackScene)->objects[0].material).diffuse);
        REQUIRE(fallbackDiffuse != nullptr);
        fallbackMip1 = readMipLevel1(**device, *fallbackDiffuse);
    }

    REQUIRE(bakedMip1.size() == fallbackMip1.size());
    INFO(describe("baked mip1 centre", 32, 32, pixelAt(bakedMip1, 32, 32)));
    INFO(describe("fallback mip1 centre", 32, 32, pixelAt(fallbackMip1, 32, 32)));
    REQUIRE(sha256Hex(std::as_bytes(std::span(bakedMip1))) ==
            sha256Hex(std::as_bytes(std::span(fallbackMip1))));
}

//======================================================================================================================
TEST_CASE("loadSponzaScene's full SceneView renders through Renderer without exhausting the "
          "frame-data arena",
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

    rhi::CommandList& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
    std::vector<render::DrawItem> items;
    const render::SceneView view =
        (*scene)->view(items, render::ShadowFilter::PCF, /*wireframe=*/false);
    REQUIRE(items.size() == 25);

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
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        if (!material.diffuse) {
            continue;
        }
        rhi::Texture* diffuse = (*scene)->tryTexture(*material.diffuse);
        REQUIRE(diffuse != nullptr);
        const bool alreadySeen = std::find(distinctDiffuse.begin(), distinctDiffuse.end(),
                                           diffuse) != distinctDiffuse.end();
        if (!alreadySeen) {
            distinctDiffuse.push_back(diffuse);
        }
        if (distinctDiffuse.size() == 8) {
            break;
        }
    }
    REQUIRE(distinctDiffuse.size() >= 2);

    auto quad = lmx::test::fixtureMesh(**device, makeUvQuad(0.45f), "lmx.test.sponzaDiffuseQuad");
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
    std::vector<lmx::test::FixtureDrawItem> items;
    items.reserve(distinctDiffuse.size());
    for (size_t i = 0; i < distinctDiffuse.size(); ++i) {
        const glm::vec3 center{startX + spacing * static_cast<float>(i), 0.0f, 0.0f};
        centers.push_back(center);
        lmx::test::FixtureMaterial
            material; // default albedo/roughness/metallic; only diffuse differs
        material.diffuse = distinctDiffuse[i];
        items.push_back({.mesh = &*quad,
                         .model = glm::translate(glm::mat4(1.0f), center),
                         .material = material});
    }

    lmx::test::FixtureSceneView view;
    view.items = items;
    // One head-on light, no environment. Every quad is coplanar and shares a normal, so each sees
    // the same N.L, the same N.V and the same BRDF -- which isolates texture differences from
    // shading exactly as the flat ambient term used to, without needing a term that no longer
    // exists.
    for (render::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.lights[0] = {.strength = {1.0f, 1.0f, 1.0f}, .direction = {0.0f, 0.0f, -1.0f}};
    view.boundingSphere = {0.0f, 0.0f, 0.0f,
                           spacing * static_cast<float>(distinctDiffuse.size()) + 2.0f};

    rhi::CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view.prepare(**device), /*barrierForSampling=*/false);
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
TEST_CASE("SceneLibrary lists the scenes in a fixed order", "[gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    SceneLibrary library(**device);

    REQUIRE(library.entries().size() == 8);
    REQUIRE(sceneIdString(library.entries()[0].id) == "sponza");
    REQUIRE(library.entries()[0].stableId == "sponza");
    REQUIRE(library.entries()[0].displayName == "Sponza");
    REQUIRE(sceneIdString(library.entries()[1].id) == "damaged-helmet");
    REQUIRE(library.entries()[1].stableId == "damaged-helmet");
    REQUIRE(sceneIdString(library.entries()[2].id) == "milk-truck");
    REQUIRE(library.entries()[2].stableId == "milk-truck");
    REQUIRE(library.entries()[2].displayName == "Milk Truck");
    REQUIRE(library.entries()[2].role == SceneRole::Sample);
    REQUIRE(sceneIdString(library.entries()[3].id) == "material-lab");
    REQUIRE(library.entries()[3].stableId == "material-lab");
    REQUIRE(library.entries()[3].displayName == "MaterialLab");
    REQUIRE(library.entries()[3].role == SceneRole::Diagnostic);
    REQUIRE(sceneIdString(library.entries()[4].id) == "temporal-lab");
    REQUIRE(library.entries()[4].stableId == "temporal-lab");
    REQUIRE(library.entries()[4].displayName == "TemporalLab");
    REQUIRE(library.entries()[4].role == SceneRole::Diagnostic);
    REQUIRE(library.entries()[5].stableId == "san-miguel");
    REQUIRE(library.entries()[5].role == SceneRole::Showcase);
    REQUIRE(library.entries()[6].stableId == "visibility-lab");
    REQUIRE(library.entries()[6].role == SceneRole::Diagnostic);
    REQUIRE(library.entries()[7].stableId == "light-lab");
    REQUIRE(library.entries()[7].displayName == "LightLab");
    REQUIRE(library.entries()[7].role == SceneRole::Diagnostic);
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

    const bool truckPresent =
        findRepoAsset("Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb").has_value();
    REQUIRE(library.entries()[2].available == truckPresent);
    REQUIRE(library.entries()[2].hint.empty() == truckPresent);

    // Both labs are code-generated: available regardless of what this checkout fetched.
    REQUIRE(library.entries()[3].available);
    REQUIRE(library.entries()[3].hint.empty());
    REQUIRE(library.entries()[4].available);
    REQUIRE(library.entries()[4].hint.empty());
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

//======================================================================================================================
TEST_CASE("loadMilkTruckScene loads the fetched CesiumMilkTruck asset with its wheel clip",
          "[gpu]") {
    const std::optional<std::filesystem::path> path =
        findRepoAsset("Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb");
    if (!path) {
        SKIP("Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb not present (xmake setup fetches "
             "it)");
    }

    auto device = rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMilkTruckScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());
    REQUIRE_FALSE((*scene)->objects.empty());
    REQUIRE((*scene)->boundingSphere.w > 0.0f);

    // The file animates its two wheel nodes and nothing else, so the body must have no track.
    REQUIRE((*scene)->animation.tracks.size() == 2);
    REQUIRE((*scene)->animation.duration > 0.0);
    REQUIRE((*scene)->animation.tracks.size() < (*scene)->objects.size());
    for (const RigidTrack& track : (*scene)->animation.tracks) {
        REQUIRE(track.objectIndex < (*scene)->objects.size());
        REQUIRE(track.keys.size() == static_cast<size_t>((*scene)->animation.duration * 60.0) + 1);
        // A wheel spins in place: its baked translation never moves.
        REQUIRE(near3(track.keys.front().translation, track.keys.back().translation));
    }

    // The scene is posed at t = 0 by the load, not left at the file's authored rest pose: a clip
    // whose first key differs from that rest pose would otherwise jump on the first frame.
    for (const RigidTrack& track : (*scene)->animation.tracks) {
        REQUIRE(matricesNear((*scene)->objects[track.objectIndex].modelMatrix(),
                             sampleRigidTrack(track, 0.0), 1e-4f));
    }

    // Nothing has moved yet, so every draw reprojects onto itself.
    std::vector<render::DrawItem> items;
    const auto rows = readSceneInstances(**scene, **device);
    (*scene)->view(items, render::ShadowFilter::PCF, false);
    REQUIRE(items.size() == (*scene)->objects.size());
    for (const render::DrawItem& item : items) {
        const auto& row = rows[item.instanceRow];
        REQUIRE(matricesNear(row.model, row.previousModel, 1e-6f));
        REQUIRE((row.flags & render::kInstanceMotionInvalid) == 0);
    }

    // Playing the clip moves the wheels and leaves the rest of the truck exactly where it was.
    const glm::mat4 bodyBefore = rows[items[0].instanceRow].model;
    (*scene)->commitFrame();
    (*scene)->advanceAnimation(1.0 / 60.0);
    (*scene)->animate((*scene)->animationTime);
    const auto movedRows = readSceneInstances(**scene, **device);
    (*scene)->view(items, render::ShadowFilter::PCF, false);
    bool anyMoved = false;
    for (const render::DrawItem& item : items) {
        const auto& row = movedRows[item.instanceRow];
        anyMoved = anyMoved || !matricesNear(row.model, row.previousModel, 1e-6f);
    }
    REQUIRE(anyMoved);
    REQUIRE(matricesNear(movedRows[items[0].instanceRow].model, bodyBefore, 1e-6f));
}

//======================================================================================================================
// The synthetic clip's value at t = 0 is (0, 0, 0) while its node authors (10, 0, 0), so a scene
// left at the file's rest pose is visibly wrong and would jump on its first animated frame. This
// pins that loadGltfScene poses the scene at the clip's t = 0 and only then seeds the previous
// transforms, so the first frame draws the played pose and still reports no motion.
TEST_CASE("loadGltfScene opens an animated file at the clip's t = 0, not its authored rest pose",
          "[scene][gpu]") {
    auto device = rhi::createDevice();
    REQUIRE(device.has_value());

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-scene-animated-pose-test";
    const std::filesystem::path gltfPath = lmx::test::writeAnimatedQuadGltf(dir, "LINEAR");

    auto scene = loadGltfScene(**device, gltfPath.string(), "AnimatedQuad");
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());
    REQUIRE((*scene)->objects.size() == 1);
    REQUIRE((*scene)->animation.tracks.size() == 1);

    const glm::mat4 clipAtZero = sampleRigidTrack((*scene)->animation.tracks[0], 0.0);
    REQUIRE(near3(glm::vec3(clipAtZero[3]), glm::vec3(0.0f)));

    std::vector<render::DrawItem> items;
    const auto rows = readSceneInstances(**scene, **device);
    (*scene)->view(items, render::ShadowFilter::PCF, false);
    REQUIRE(items.size() == 1);
    const auto& row = rows[items[0].instanceRow];
    REQUIRE(matricesNear(row.model, clipAtZero, 1e-4f));
    REQUIRE(matricesNear(row.previousModel, row.model, 1e-6f));
    // The authored rest pose, which the scene must *not* be left at.
    REQUIRE_FALSE(near3(glm::vec3(row.model[3]), glm::vec3(10.0f, 0.0f, 0.0f)));

    // The bounds describe the posed geometry: the quad spans x in [-1, 1] about the clip's origin,
    // not about the authored (10, 0, 0).
    REQUIRE(glm::vec3((*scene)->boundingSphere).x == Catch::Approx(0.0f).margin(1e-4));
}

//======================================================================================================================
TEST_CASE("Sponza rig is deterministic, idempotent and rejects foreign scenes",
          "[scene][light-rig]") {
    Scene scene;
    scene.name = "Sponza";
    Scene foreign;
    foreign.name = "Sponza";
    SponzaLightRig rig;
    REQUIRE(rig.setEnabled(scene, false));
    REQUIRE(scene.localLights().empty());
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(rig.enabled());
    const std::vector<LightId> ids(rig.lightIds().begin(), rig.lightIds().end());
    REQUIRE(ids.size() > 0);
    REQUIRE(ids.size() <= 32);
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(std::ranges::equal(rig.lightIds(), ids));
    for (const auto id : ids) {
        REQUIRE(scene.light(id));
        REQUIRE(render::makeLightRow(*scene.light(id)));
        REQUIRE_FALSE(foreign.light(id));
        REQUIRE_FALSE(foreign.removeLight(id));
    }
    REQUIRE_FALSE(rig.setEnabled(foreign, false));
    REQUIRE(scene.localLights().size() == ids.size());
    REQUIRE(rig.setEnabled(scene, false));
    REQUIRE_FALSE(rig.enabled());
    REQUIRE(scene.localLights().size() == ids.size());
    REQUIRE(scene.enabledLightCount() == 0);
    for (const auto id : ids) {
        REQUIRE(scene.light(id));
        REQUIRE_FALSE(scene.light(id)->enabled);
    }
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(rig.lightIds().front() == ids.front());
    const auto original = *scene.light(ids.front());
    auto edited = original;
    edited.intensity = 137.0f;
    edited.colour = {0.1f, 0.2f, 0.3f};
    REQUIRE(scene.updateLight(ids.front(), edited));
    REQUIRE(rig.setEnabled(scene, false));
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(scene.light(ids.front())->intensity == 137.0f);
    REQUIRE(scene.light(ids.front())->colour == edited.colour);
    SponzaLightRig rebound;
    REQUIRE(rebound.setEnabled(scene, true));
    REQUIRE(std::ranges::equal(rebound.lightIds(), ids));
    REQUIRE(scene.localLights().size() == ids.size());
    REQUIRE(scene.updateLight(ids.front(), original));
    SponzaLightRig otherRig;
    REQUIRE(otherRig.setEnabled(foreign, true));
    REQUIRE(otherRig.lightIds().size() == rig.lightIds().size());
    for (size_t i = 0; i < rig.lightIds().size(); ++i) {
        const auto first = render::makeLightRow(*scene.light(rig.lightIds()[i]));
        const auto second = render::makeLightRow(*foreign.light(otherRig.lightIds()[i]));
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(std::memcmp(&*first, &*second, sizeof(render::LightRow)) == 0);
    }
}

//======================================================================================================================
TEST_CASE("SceneSession preserves per-scene rig state without touching unrelated scenes",
          "[app][scene-session][light-rig]") {
    Scene sponza;
    sponza.name = "Sponza";
    Scene other;
    other.name = "LightLab";
    lmx::app::SceneSession session;
    session.activate(sponza, lmx::app::SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.localLightRigAvailable());
    REQUIRE_FALSE(session.localLightRigEnabled());
    REQUIRE(session.setLocalLightRig(true));
    const size_t count = sponza.localLights().size();
    session.activate(other, lmx::app::SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE_FALSE(session.localLightRigAvailable());
    REQUIRE_FALSE(session.localLightRigEnabled());
    REQUIRE_FALSE(session.setLocalLightRig(true));
    REQUIRE(other.localLights().empty());
    session.activate(sponza, lmx::app::SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.localLightRigEnabled());
    REQUIRE(sponza.localLights().size() == count);
    REQUIRE(session.setLocalLightRig(false));
    REQUIRE(sponza.localLights().size() == count);
    REQUIRE(sponza.enabledLightCount() == 0);
}

//======================================================================================================================
TEST_CASE("Sponza authored rig preserves geometry rows and has no animation tracks",
          "[gpu][scene][light-rig]") {
    if (!findRepoAsset("Assets/Fetched/Sponza/Sponza.gltf")) {
        SKIP("Sponza asset not fetched");
    }
    auto device = rhi::createDevice();
    REQUIRE(device);
    auto loaded = loadSponzaScene(**device);
    REQUIRE(loaded);
    auto& scene = **loaded;
    lmx::app::SceneSession session;
    session.activate(scene, lmx::app::SceneActivationMotion::PreserveLoadedMotion);
    const auto before = readSceneInstances(scene, **device);
    REQUIRE(scene.tables().liveLightCount == 16);
    REQUIRE(scene.tables().lights != nullptr);
    REQUIRE(session.setLocalLightRig(true));
    const auto during = readSceneInstances(scene, **device);
    REQUIRE(scene.tables().liveLightCount > 0);
    REQUIRE(scene.tables().liveLightCount <= 32);
    REQUIRE(scene.animation.lightTracks.empty());
    REQUIRE(scene.animationLightId(0));
    REQUIRE(before.size() == during.size());
    REQUIRE(std::memcmp(before.data(), during.data(),
                        before.size() * sizeof(render::InstanceRow)) == 0);
    const std::vector<LightId> ids(scene.localLights().begin(), scene.localLights().end());
    const auto firstPosition = scene.light(ids.front())->position;
    session.stepAnimation();
    REQUIRE(scene.light(ids.front())->position == firstPosition);
    REQUIRE(session.setLocalLightRig(false));
    const auto after = readSceneInstances(scene, **device);
    REQUIRE(scene.tables().liveLightCount == 0);
    REQUIRE(before.size() == after.size());
    REQUIRE(std::memcmp(before.data(), after.data(), before.size() * sizeof(render::InstanceRow)) ==
            0);
    for (const auto id : ids) {
        REQUIRE(scene.light(id));
        REQUIRE_FALSE(scene.light(id)->enabled);
    }
}

//======================================================================================================================
TEST_CASE("Sponza rig leaves unrelated lights intact and fails capacity without partial additions",
          "[scene][light-rig]") {
    Scene scene;
    scene.name = "Sponza";
    const auto authored = scene.addLight(render::LocalLight{});
    REQUIRE(authored);
    SponzaLightRig rig;
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(scene.removeLight(rig.lightIds().front()));
    REQUIRE(rig.setEnabled(scene, false));
    REQUIRE(scene.localLights().size() == 16);
    REQUIRE(scene.enabledLightCount() == 1);
    REQUIRE(scene.light(*authored));
    REQUIRE(rig.setEnabled(scene, true));
    REQUIRE(scene.localLights().size() == 16);
    Scene crowded;
    crowded.name = "Sponza";
    SponzaLightRig crowdedRig;
    while (crowded.localLights().size() < render::kMaxLocalLights - 8) {
        REQUIRE(crowded.addLight(render::LocalLight{}));
    }
    const size_t count = crowded.localLights().size();
    REQUIRE_FALSE(crowdedRig.setEnabled(crowded, true));
    REQUIRE_FALSE(crowdedRig.enabled());
    REQUIRE(crowded.localLights().size() == count);
}
