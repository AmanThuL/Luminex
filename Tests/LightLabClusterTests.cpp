//----------------------------------------------------------------------------------------------------------------------
/// @file LightLabClusterTests.cpp
/// @brief Pins overflow-free light assignment along the authored lab camera and orbit tracks.
//----------------------------------------------------------------------------------------------------------------------
#include "Scenes/LightLab.h"

#include "Engine/Lights/LocalLightMath.h"
#include "Engine/View/Camera.h"
#include "Render/Passes/LocalLights/LightClusters.h"

#include <catch2/catch_test_macros.hpp>

//======================================================================================================================
TEST_CASE("LightLab populations remain overflow-free across the actual rail and orbits",
          "[scene][light-lab][light-cluster]") {
    using namespace lmx;
    const auto rail = scenes::lightLabCameraTrack();
    for (uint32_t count : {64u, 256u, 1024u, 4096u}) {
        const auto authored = scenes::lightLabLights(count, 0);
        const auto tracks = scenes::lightLabTracks(count, 0);
        for (double seconds : {0.0, 1.5, 3.0, 4.5, 6.0, 7.5, 9.0, 10.5, 719.0 / 60.0}) {
            auto lights = authored;
            for (const auto& track : tracks)
                lights[track.light].position =
                    asset::sampleOrbit(track, static_cast<float>(seconds));
            std::vector<engine::LightRow> rows;
            for (const auto& light : lights) {
                auto row = engine::makeLightRow(light);
                REQUIRE(row);
                rows.push_back(*row);
            }
            const auto pose = asset::sampleCameraTrack(rail, seconds);
            engine::Camera camera;
            camera.position = pose.position;
            camera.yaw = pose.yaw;
            camera.pitch = pose.pitch;
            camera.fovY = scenes::kLightLabCameraFovY;
            camera.nearZ = scenes::kLightLabCameraNearZ;
            for (glm::uvec2 extent : {glm::uvec2{1280, 720}, glm::uvec2{641, 359}}) {
                CAPTURE(count, seconds, extent.x, extent.y);
                const auto projection = camera.projectionMatrix(float(extent.x) / float(extent.y));
                const render::LightClusterParams params{
                    .view = camera.viewMatrix(),
                    .inverseJitteredProjection = glm::inverse(projection),
                    .rowCount = count,
                    .activeWidth = extent.x,
                    .activeHeight = extent.y,
                    .sliceDepth = render::clusterSliceDepths(camera.nearZ)};
                const auto lists = render::buildLightClusters(rows, params);
                CAPTURE(lists.counters.candidates, lists.counters.assigned,
                        lists.counters.maxCount);
                REQUIRE(lists.counters.assigned > 0);
                REQUIRE(lists.counters.droppedPerCluster == 0);
                REQUIRE(lists.counters.droppedGlobal == 0);
                REQUIRE(lists.counters.truncatedFroxels == 0);
                REQUIRE(lists.counters.candidates == lists.counters.assigned);
            }
        }
    }
}
