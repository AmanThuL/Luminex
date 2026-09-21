//----------------------------------------------------------------------------------------------------------------------
/// @file SceneVisibilityLabTests.cpp
/// @brief Tests visibility-lab population, boundary coverage, and deterministic camera sweeps.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Visibility/Visibility.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"
#include "Scenes/SceneLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace lmx;

//======================================================================================================================
TEST_CASE("visibility lab keeps its requested population and initial boundary lanes",
          "[gpu][scene][visibility]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scenes::SceneLibrary library(**device);
    const auto id = scenes::parseSceneId("visibility-lab");
    REQUIRE(id);
    REQUIRE(library.entry(*id).available);
    auto result = library.get(*id);
    REQUIRE(result);
    auto& scene = **result;
    REQUIRE(scene.objects.size() == 4096);
    REQUIRE(scene.tableStats().materialCount == 4);
    REQUIRE(scene.animation.duration == 12.0);
    REQUIRE(scene.animation.cameraTrack.size() == 721);
    REQUIRE(scene.animation.cameraTrack.front().position == scene.initialCamera.position);
    REQUIRE(scene.animation.cameraTrack.back().position == scene.initialCamera.position);
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()));
    std::vector<engine::DrawItem> items;
    const auto view = render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    auto camera = engine::cameraFromScene(scene.initialCamera);
    const auto classify = [&] {
        const auto planes = render::extractFrustumPlanes(camera.projectionMatrix(16.0f / 9.0f) *
                                                         camera.viewMatrix());
        return render::classifyView(planes, items, view.tables);
    };
    const auto initial = classify();
    REQUIRE(initial.rejected > 0);
    REQUIRE(initial.visible > 0);
    REQUIRE(initial.visible < 4096 / 3);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(initial.candidates[i].state == render::VisibilityState::Visible);
    }
    for (double seconds : {0.0, 3.0, 6.0}) {
        scene.animationTime = seconds;
        scene.followCameraTrack(camera);
        const auto classification = classify();
        const auto planes = render::extractFrustumPlanes(camera.projectionMatrix(16.0f / 9.0f) *
                                                         camera.viewMatrix());
        uint32_t independent = 0;
        for (const auto& item : items) {
            independent += render::classifyInstance(
                               planes, view.tables.instanceRows[item.instanceRow], item.instanceRow)
                               .state == render::VisibilityState::Visible;
        }
        REQUIRE(classification.visible == independent);
        if (seconds == 6.0)
            REQUIRE(classification.visible == 4096);
    }
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("visibility lab count includes boundary probes and seeded layout repeats",
          "[gpu][scene][visibility]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    REQUIRE_FALSE(scenes::loadVisibilityLabScene(**device, 0));
    REQUIRE_FALSE(scenes::loadVisibilityLabScene(**device, 1048577));
    for (uint32_t count : {1u, 5u, 1024u}) {
        auto first = scenes::loadVisibilityLabScene(**device, count);
        auto second = scenes::loadVisibilityLabScene(**device, count);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE((*first)->objects.size() == count);
        REQUIRE((*second)->objects.size() == count);
        for (size_t i = 0; i < count; ++i) {
            REQUIRE((*first)->objects[i].position == (*second)->objects[i].position);
            REQUIRE((*first)->objects[i].mesh.slot == (*second)->objects[i].mesh.slot);
            REQUIRE((*first)->objects[i].material.slot == (*second)->objects[i].material.slot);
        }
    }
}

//======================================================================================================================
TEST_CASE("visibility lab occluders append deterministic slabs without altering the default grid",
          "[gpu][scene][visibility][occlusion]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    REQUIRE_FALSE(scenes::loadVisibilityLabScene(**device, 32, 1025));
    auto baseline = scenes::loadVisibilityLabScene(**device, 32);
    auto explicitZero = scenes::loadVisibilityLabScene(**device, 32, 0);
    auto occluded = scenes::loadVisibilityLabScene(**device, 32, 4);
    auto repeated = scenes::loadVisibilityLabScene(**device, 32, 4);
    REQUIRE(baseline);
    REQUIRE(explicitZero);
    REQUIRE(occluded);
    REQUIRE(repeated);
    REQUIRE((*occluded)->objects.size() == 36);
    REQUIRE((*occluded)->tableStats().materialCount == 6);
    REQUIRE((*occluded)->animation.tracks.empty());
    REQUIRE((*occluded)->animation.emissiveTracks.empty());
    for (size_t i = 0; i < 32; ++i) {
        const auto& original = (*baseline)->objects[i];
        const auto& zero = (*explicitZero)->objects[i];
        const auto& augmented = (*occluded)->objects[i];
        REQUIRE(original.name == zero.name);
        REQUIRE(original.modelMatrix() == zero.modelMatrix());
        REQUIRE(original.modelMatrix() == augmented.modelMatrix());
        REQUIRE(original.mesh.slot == augmented.mesh.slot);
        REQUIRE(original.material.slot == augmented.material.slot);
    }
    REQUIRE((*baseline)->boundingSphere == (*explicitZero)->boundingSphere);
    REQUIRE((*baseline)->animation.cameraTrack.size() ==
            (*explicitZero)->animation.cameraTrack.size());
    for (size_t i = 0; i < (*baseline)->animation.cameraTrack.size(); ++i) {
        const auto& a = (*baseline)->animation.cameraTrack[i];
        const auto& b = (*explicitZero)->animation.cameraTrack[i];
        REQUIRE(a.time == b.time);
        REQUIRE(a.position == b.position);
        REQUIRE(a.yaw == b.yaw);
        REQUIRE(a.pitch == b.pitch);
    }
    for (size_t i = 32; i < 36; ++i) {
        const auto& slab = (*occluded)->objects[i];
        const auto& repeatedSlab = (*repeated)->objects[i];
        REQUIRE(slab.modelMatrix() == repeatedSlab.modelMatrix());
        REQUIRE(slab.scale.x > 1.0f);
        REQUIRE(slab.scale.y >= 8.0f);
        const auto& material = (*occluded)->material(slab.material);
        REQUIRE(material.alphaMode ==
                (i == 32 ? engine::AlphaMode::Mask : engine::AlphaMode::Opaque));
        if (i == 32) {
            REQUIRE(material.diffuse);
            REQUIRE(material.doubleSided);
        }
        if (i > 32) {
            const auto& previous = (*occluded)->objects[i - 1];
            REQUIRE(slab.position.x - slab.scale.x * 0.5f >
                    previous.position.x + previous.scale.x * 0.5f);
        }
    }
    (*device)->beginFrame();
    REQUIRE((*baseline)->prepareFrame((*device)->frameNumber()));
    REQUIRE((*explicitZero)->prepareFrame((*device)->frameNumber()));
    const auto a = (*baseline)->tables();
    const auto b = (*explicitZero)->tables();
    REQUIRE(a.instanceRows.size() == b.instanceRows.size());
    REQUIRE(std::memcmp(a.instanceRows.data(), b.instanceRows.data(),
                        a.instanceRows.size() * sizeof(engine::InstanceRow)) == 0);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}
