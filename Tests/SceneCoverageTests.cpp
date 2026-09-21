//----------------------------------------------------------------------------------------------------------------------
/// @file SceneCoverageTests.cpp
/// @brief Tests coverage revisions and scene-independent identity tokens used by occlusion history.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/SceneViewBuilder.h"
#include "Scene/Scene.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lmx;

namespace {
//======================================================================================================================
uint64_t prepareCoverage(rojoRHI::Device& device, scene::Scene& scene) {
    device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()));
    std::vector<render::DrawItem> items;
    const auto view = render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(view.coverageEpoch == scene.coverageEpoch());
    const auto epoch = view.coverageEpoch;
    device.endFrame(nullptr);
    device.waitIdle();
    return epoch;
}
} // namespace

//======================================================================================================================
TEST_CASE("Scene coverage invalidates every geometry and mask edit", "[gpu][scene][occlusion]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scene::Scene scene;
    const auto firstMesh = scene.addMesh(render::makeCube(), "lmx.test.coverage.first");
    const auto secondMesh = scene.addMesh(render::makeCube(), "lmx.test.coverage.second");
    const auto firstMaterial = scene.addMaterial({});
    const auto secondMaterial = scene.addMaterial({});
    const auto id = scene.addObject({.mesh = firstMesh, .material = firstMaterial});
    const std::array<uint8_t, 4> pixels{255, 255, 255, 0};
    const rojoRHI::TextureMip mip{.data = pixels.data(), .bytesPerRow = 4};
    auto texture = (*device)->createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA8Unorm,
                                             .sampled = true,
                                             .label = "lmx.test.coverage.alpha"},
                                            std::span(&mip, 1));
    REQUIRE(texture);
    const auto textureId = scene.addTexture(std::move(*texture));
    REQUIRE(scene.finalize(**device));
    const auto before = prepareCoverage(**device, scene);
    REQUIRE(prepareCoverage(**device, scene) == before);
    SECTION("translation") {
        scene.tryObject(id)->position.x = 2.0f;
    }
    SECTION("rotation") {
        scene.tryObject(id)->eulerDegrees.y = 30.0f;
    }
    SECTION("rescaling") {
        scene.tryObject(id)->scale.y = 2.0f;
    }
    SECTION("mesh assignment") {
        scene.tryObject(id)->mesh = secondMesh;
    }
    SECTION("material assignment") {
        scene.tryObject(id)->material = secondMaterial;
    }
    SECTION("alpha mode") {
        scene.material(firstMaterial).alphaMode = render::AlphaMode::Mask;
    }
    SECTION("alpha cutoff") {
        scene.material(firstMaterial).alphaCutoff = 0.75f;
    }
    SECTION("sidedness") {
        scene.material(firstMaterial).doubleSided = true;
    }
    SECTION("alpha factor") {
        scene.material(firstMaterial).albedo.a = 0.2f;
    }
    SECTION("diffuse identity") {
        scene.material(firstMaterial).diffuse = textureId;
    }
    SECTION("texture coordinates") {
        scene.material(firstMaterial).uvTransform[3].x = 0.5f;
    }
    SECTION("addition") {
        scene.addObject({.mesh = firstMesh, .material = firstMaterial});
    }
    SECTION("removal") {
        scene.removeObject(id);
    }
    SECTION("same row reused with identical geometry") {
        scene.removeObject(id);
        const auto next = scene.addObject({.mesh = firstMesh, .material = firstMaterial});
        REQUIRE(next.slot == id.slot);
        REQUIRE(next.generation != id.generation);
    }
    const auto changed = prepareCoverage(**device, scene);
    REQUIRE(changed > before);
    REQUIRE(prepareCoverage(**device, scene) == changed);
}

//======================================================================================================================
TEST_CASE("Scene coverage ignores motion history, lighting and object ordering",
          "[gpu][scene][occlusion]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scene::Scene scene;
    const auto mesh = scene.addMesh(render::makeCube(), "lmx.test.coverage.exclusions");
    const auto material = scene.addMaterial({});
    scene.addObject({.mesh = mesh, .material = material});
    scene.addObject({.position = {2, 0, 0}, .mesh = mesh, .material = material});
    REQUIRE(scene.finalize(**device));
    const auto before = prepareCoverage(**device, scene);
    scene.objects[0].previousModel[3].x = 4.0f;
    scene.objects[0].emissiveStrength = 3.0f;
    scene.objects[0].motionClass = render::MotionClass::Invalid;
    scene.material(material).emissive = {3, 2, 1};
    scene.material(material).albedo.r = 0.3f;
    scene.material(material).roughness = 0.8f;
    scene.lights[0].strength = {5, 5, 5};
    std::ranges::reverse(scene.objects);
    REQUIRE(prepareCoverage(**device, scene) == before);
    scene.commitFrame();
    REQUIRE(prepareCoverage(**device, scene) == before);
    scene.resetMotion();
    REQUIRE(prepareCoverage(**device, scene) == before);
}

//======================================================================================================================
TEST_CASE("Scene view tokens distinguish stores and recycled instance rows", "[scene][occlusion]") {
    const auto add = [](scene::Scene& scene) {
        const auto mesh = scene.addMesh(render::makeCube(), "lmx.test.coverage.identity");
        const auto material = scene.addMaterial({});
        return scene.addObject({.mesh = mesh, .material = material});
    };
    const auto token = [](scene::Scene& scene) {
        std::vector<render::DrawItem> items;
        render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
        REQUIRE(items.size() == 1);
        return items.front().instanceIdentity;
    };
    scene::Scene first;
    scene::Scene second;
    const auto oldId = add(first);
    add(second);
    const auto oldToken = token(first);
    REQUIRE(oldToken != token(second));
    REQUIRE(oldToken != 0);
    const auto object = first.objects.front();
    const auto before = first.coverageEpoch();
    first.removeObject(oldId);
    REQUIRE(first.coverageEpoch() > before);
    const auto removed = first.coverageEpoch();
    const auto newId = first.addObject({.mesh = object.mesh, .material = object.material});
    REQUIRE(first.coverageEpoch() > removed);
    REQUIRE(newId.slot == oldId.slot);
    REQUIRE(token(first) != oldToken);
}
