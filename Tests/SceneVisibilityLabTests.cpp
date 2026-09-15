//----------------------------------------------------------------------------------------------------------------------
/// @file SceneVisibilityLabTests.cpp
/// @brief Tests visibility-lab population, boundary coverage, and deterministic camera sweeps.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Visibility.h"
#include "Scene/SceneLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace lmx;

//======================================================================================================================
TEST_CASE("visibility lab keeps its requested population and initial boundary lanes",
          "[gpu][scene][visibility]") {
    auto device = rhi::createDevice();
    REQUIRE(device);
    scene::SceneLibrary library(**device);
    const auto id = scene::parseSceneId("visibility-lab");
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
    std::vector<render::DrawItem> items;
    const auto view = scene.view(items, render::ShadowFilter::PCF, false);
    auto camera = scene::cameraFromScene(scene.initialCamera);
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
    auto device = rhi::createDevice();
    REQUIRE(device);
    REQUIRE_FALSE(scene::loadVisibilityLabScene(**device, 0));
    REQUIRE_FALSE(scene::loadVisibilityLabScene(**device, 1048577));
    for (uint32_t count : {1u, 5u, 1024u}) {
        auto first = scene::loadVisibilityLabScene(**device, count);
        auto second = scene::loadVisibilityLabScene(**device, count);
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
