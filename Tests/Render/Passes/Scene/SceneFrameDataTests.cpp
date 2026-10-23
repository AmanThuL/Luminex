//----------------------------------------------------------------------------------------------------------------------
/// @file SceneFrameDataTests.cpp
/// @brief Pins the scene, sky and local-light frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Scene/SceneFrameData.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <array>

using namespace lmx::render;
using namespace lmx::render::scene_detail;

namespace {

//======================================================================================================================
CameraFrameState cameraWithMatrices(float plain, float jittered) {
    CameraFrameState camera;
    camera.viewProjection = glm::mat4(plain);
    camera.viewProjectionJittered = glm::mat4(jittered);
    camera.position = glm::vec3(plain);
    camera.nearZ = 0.1f;
    return camera;
}

//======================================================================================================================
PassUniformInputs passInputs(const std::array<lmx::engine::DirectionalLight, 3>& lights) {
    return {.camera = cameraWithMatrices(2.0f, 3.0f),
            .previousCamera = cameraWithMatrices(5.0f, 6.0f),
            .temporalEnabled = false,
            .shadowTransform = glm::mat4(7.0f),
            .eyePosition = glm::vec3(1.0f, 2.0f, 3.0f),
            .timeSeconds = 4.5f,
            .exposureEv = 1.0f,
            .lights = lights,
            .shadowCaster = -1,
            .shadowFilter = ShadowFilter::PCSS};
}

} // namespace

//======================================================================================================================
TEST_CASE("pass uniforms pre-expose by the manual EV and select the shadow filter",
          "[render][scene]") {
    std::array<lmx::engine::DirectionalLight, 3> lights;
    lights[1].strength = glm::vec3(0.25f, 0.5f, 0.75f);
    lights[1].direction = glm::vec3(0.0f, 0.0f, -1.0f);
    auto inputs = passInputs(lights);
    SECTION("exposure and filter") {
        const PassUniforms uniforms = buildPassUniforms(inputs);
        CHECK(uniforms.preExposure == 2.0f);
        CHECK(uniforms.shadowFilter == kShadowFilterPcss);
        CHECK(uniforms.shadowCaster == -1);
        CHECK(uniforms.time == 4.5f);
        CHECK(uniforms.eyePos == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(uniforms.eyePadding == 0.0f);
        CHECK(uniforms.alignmentPadding[0] == 0.0f);
        CHECK(uniforms.alignmentPadding[1] == 0.0f);
        CHECK(uniforms.tailPadding[0] == 0);
        CHECK(uniforms.tailPadding[1] == 0);
        CHECK(uniforms.shadowTransform == glm::mat4(7.0f));
        CHECK(uniforms.lights[1].strength == glm::vec3(0.25f, 0.5f, 0.75f));
        CHECK(uniforms.lights[1].direction == glm::vec3(0.0f, 0.0f, -1.0f));
        CHECK(uniforms.lights[1].strengthPadding == 0.0f);
        CHECK(uniforms.lights[1].directionPadding == 0.0f);
    }
    SECTION("PCF is every other filter") {
        inputs.shadowFilter = ShadowFilter::PCF;
        CHECK(buildPassUniforms(inputs).shadowFilter == kShadowFilterPcf);
    }
    SECTION("raster matrix is plain with temporal off and jittered with it on") {
        CHECK(buildPassUniforms(inputs).viewProj == glm::mat4(2.0f));
        inputs.temporalEnabled = true;
        const PassUniforms uniforms = buildPassUniforms(inputs);
        CHECK(uniforms.viewProj == glm::mat4(3.0f));
        CHECK(uniforms.viewProjUnjittered == glm::mat4(2.0f));
        CHECK(uniforms.previousViewProjUnjittered == glm::mat4(5.0f));
    }
}

//======================================================================================================================
TEST_CASE("local light params zero the row count when local lights are off", "[render][scene]") {
    LocalLightParamInputs inputs{.requestedMode = lmx::engine::LocalLightMode::Off,
                                 .liveLightCount = 4,
                                 .lightRowCount = 9,
                                 .hasClusters = true,
                                 .extents = {.renderWidth = 640,
                                             .renderHeight = 360,
                                             .outputWidth = 1280,
                                             .outputHeight = 720},
                                 .nearZ = 0.1f};
    SECTION("off") {
        const LocalLightParams params = buildLocalLightParams(inputs);
        CHECK(params.mode == static_cast<uint32_t>(lmx::engine::LocalLightMode::Off));
        CHECK(params.rowCount == 0);
    }
    SECTION("no live light forces off") {
        inputs.requestedMode = lmx::engine::LocalLightMode::Direct;
        inputs.liveLightCount = 0;
        const LocalLightParams params = buildLocalLightParams(inputs);
        CHECK(params.mode == static_cast<uint32_t>(lmx::engine::LocalLightMode::Off));
        CHECK(params.rowCount == 0);
    }
    SECTION("clustered carries the grid, the render extent and the slice table") {
        inputs.requestedMode = lmx::engine::LocalLightMode::Clustered;
        const LocalLightParams params = buildLocalLightParams(inputs);
        CHECK(params.mode == static_cast<uint32_t>(lmx::engine::LocalLightMode::Clustered));
        CHECK(params.rowCount == 9);
        CHECK(params.gridX == kClusterTilesX);
        CHECK(params.gridY == kClusterTilesY);
        CHECK(params.gridZ == kClusterSliceCount);
        CHECK(params.activeOriginX == 0);
        CHECK(params.activeOriginY == 0);
        CHECK(params.activeWidth == 640);
        CHECK(params.activeHeight == 360);
        CHECK(params.sliceDepth[0] == clusterSliceDepths(0.1f)[0]);
        CHECK(params.sliceDepth[kClusterSliceCount] ==
              clusterSliceDepths(0.1f)[kClusterSliceCount]);
    }
}

//======================================================================================================================
TEST_CASE("sky uniforms carry the unjittered matrices and split the NDC jitter",
          "[render][scene]") {
    const SkyUniforms sky = buildSkyUniforms({.viewProj = glm::mat4(2.0f),
                                              .eyePos = glm::vec3(1.0f, 2.0f, 3.0f),
                                              .preExposure = 4.0f,
                                              .jitterNdc = glm::vec2(0.5f, -0.25f),
                                              .previousViewProj = glm::mat4(5.0f),
                                              .previousEyePos = glm::vec3(6.0f, 7.0f, 8.0f)});
    CHECK(sky.viewProj == glm::mat4(2.0f));
    CHECK(sky.eyePos == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(sky.preExposure == 4.0f);
    CHECK(sky.jitterNdcX == 0.5f);
    CHECK(sky.jitterNdcY == -0.25f);
    CHECK(sky.previousViewProj == glm::mat4(5.0f));
    CHECK(sky.previousEyePos == glm::vec3(6.0f, 7.0f, 8.0f));
    CHECK(sky.eyePadding == 0.0f);
    CHECK(sky.jitterPadding == 0.0f);
    CHECK(sky.previousEyePosPadding == 0.0f);
}
