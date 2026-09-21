#include "Engine/Types/Camera.h"
#include "Render/Occlusion.h"
#include "Render/OcclusionHistory.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>
using namespace lmx::render;
//======================================================================================================================
TEST_CASE("occlusion history requires adjacent complete unchanged evidence",
          "[render][occlusion]") {
    OcclusionFrameFacts source{.frameNumber = 12,
                               .sceneGeneration = 3,
                               .coverageEpoch = 4,
                               .outputWidth = 1280,
                               .outputHeight = 720,
                               .enabled = true};
    auto current = source;
    current.frameNumber++;
    REQUIRE(occlusionHistoryReason(source, current, true) == OcclusionInvalidReason::None);
    SECTION("wireframe retains globally") {
        current.wireframe = true;
        REQUIRE(occlusionHistoryReason(source, current, true) == OcclusionInvalidReason::Wireframe);
    }
    SECTION("no source") {
        REQUIRE(occlusionHistoryReason({}, current, true) == OcclusionInvalidReason::NoSource);
    }
    SECTION("toggle") {
        REQUIRE(occlusionHistoryReason(source, current, false) ==
                OcclusionInvalidReason::PreviouslyDisabled);
    }
    SECTION("skipped frame") {
        current.frameNumber++;
        REQUIRE(occlusionHistoryReason(source, current, true) == OcclusionInvalidReason::SourceGap);
    }
    SECTION("coverage") {
        current.coverageEpoch++;
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::CoverageChanged);
    }
    SECTION("scene") {
        current.sceneGeneration++;
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::SceneChanged);
    }
    SECTION("output") {
        current.outputWidth++;
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::OutputExtentChanged);
    }
    SECTION("cut") {
        current.cameraCut = true;
        REQUIRE(occlusionHistoryReason(source, current, true) == OcclusionInvalidReason::CameraCut);
    }
    SECTION("translation") {
        current.cameraPosition.x = 1.0f;
        REQUIRE(occlusionHistoryReason(source, current, true) == OcclusionInvalidReason::None);
        current.cameraPosition.x = 1.01f;
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::CameraTranslation);
    }
    SECTION("rotation") {
        float angle = glm::radians(10.1f);
        current.cameraForward = {std::sin(angle), 0, -std::cos(angle)};
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::CameraRotation);
    }
    SECTION("nonfinite") {
        current.cameraPosition.x = std::numeric_limits<float>::infinity();
        REQUIRE(occlusionHistoryReason(source, current, true) ==
                OcclusionInvalidReason::NonFiniteCamera);
    }
}
//======================================================================================================================
TEST_CASE("occlusion projection and strict depth guards retain uncertain coverage",
          "[render][occlusion]") {
    lmx::engine::Camera camera;
    camera.fovY = glm::radians(90.0f);
    camera.nearZ = 1.0f;
    auto params = makeOcclusionParams(camera.projectionMatrix(1), 64, 64, 3, true, true);
    lmx::Aabb box{{-0.1f, -0.1f, -4.1f}, {0.1f, 0.1f, -4.0f}};
    auto projection = projectOcclusionBounds(box, params);
    REQUIRE(projection.outcome == OcclusionOutcome::Retained);
    REQUIRE(projection.rectangle == std::array<int32_t, 4>{30, 30, 34, 34});
    std::vector<float> depth(32 * 32, 0.5f);
    const OcclusionLevel level{depth, 32};
    REQUIRE(testOcclusionBounds(box, params, std::span{&level, 1}).occluded);
    SECTION("equality never rejects") {
        std::fill(depth.begin(), depth.end(), projection.zBox + params.depthGuard);
        REQUIRE_FALSE(testOcclusionBounds(box, params, std::span{&level, 1}).occluded);
    }
    SECTION("one source sample exposes geometry") {
        depth[15 * 32 + 15] = 0;
        REQUIRE_FALSE(testOcclusionBounds(box, params, std::span{&level, 1}).occluded);
    }
    SECTION("near crossing") {
        box.maximum.z = 0;
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::NearCrossing);
    }
    SECTION("actual near plane") {
        box.maximum.z = -0.5f;
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::NearCrossing);
    }
    SECTION("outside source") {
        box.maximum.x = 8;
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::OutsideSource);
    }
    SECTION("top level limit") {
        params.levelCount = 1;
        box = {{-3, -3, -4}, {3, 3, -4}};
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::RectTooLarge);
    }
    SECTION("invalid retains globally") {
        params.flags = 1;
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::HistoryInvalid);
    }
    SECTION("off no test") {
        params.flags = 0;
        REQUIRE(projectOcclusionBounds(box, params).outcome == OcclusionOutcome::NotTested);
    }
}
