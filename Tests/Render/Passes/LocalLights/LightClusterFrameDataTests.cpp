//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterFrameDataTests.cpp
/// @brief Pins the light clustering and light debug frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/LocalLights/LightClusterFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("light cluster kernel params copy columns, capacities and the slice table",
          "[render][light-cluster]") {
    LightClusterParams clusters;
    clusters.view = glm::mat4(1.0f);
    clusters.view[1][2] = 5.0f;
    clusters.inverseJitteredProjection[3][0] = 7.0f;
    clusters.rowCount = 12;
    clusters.activeWidth = 640;
    clusters.activeHeight = 360;
    clusters.perClusterCap = 8;
    clusters.globalCapacity = 100;
    for (uint32_t boundary = 0; boundary < kClusterSliceBoundaryCount; ++boundary) {
        clusters.sliceDepth[boundary] = float(boundary) + 0.5f;
    }

    const LightClusterKernelParams params = buildLightClusterKernelParams(clusters);

    CHECK(params.view[1][2] == 5.0f);
    CHECK(params.view[0][0] == 1.0f);
    CHECK(params.inverseProjection[3][0] == 7.0f);
    CHECK(params.rowCount == 12u);
    CHECK(params.activeWidth == 640u);
    CHECK(params.activeHeight == 360u);
    CHECK(params.perClusterCap == 8u);
    CHECK(params.globalCapacity == 100u);
    CHECK(params.froxelCount == kClusterCount);
    CHECK(params.padding0 == 0u);
    CHECK(params.padding1 == 0u);
    CHECK(params.sliceDepth[0][0] == 0.5f);
    CHECK(params.sliceDepth[1][2] == 6.5f);
    CHECK(params.sliceDepth[kClusterSliceBoundaryCount / 4][(kClusterSliceBoundaryCount - 1) % 4] ==
          float(kClusterSliceBoundaryCount - 1) + 0.5f);
}

//======================================================================================================================
TEST_CASE("light debug params map the view mode and copy the cluster extent",
          "[render][light-cluster]") {
    LightDebugParamsInputs inputs;
    inputs.mode = lmx::engine::LightDebugView::Overflow;
    inputs.clusters.rowCount = 9;
    inputs.clusters.activeWidth = 320;
    inputs.clusters.activeHeight = 180;
    inputs.clusters.sliceDepth[3] = 2.5f;
    inputs.inverseViewProjection[2][1] = 4.0f;
    inputs.outputWidth = 1280;
    inputs.outputHeight = 720;

    const LightDebugParams params = buildLightDebugParams(inputs);

    CHECK(params.mode == 2u);
    CHECK(params.rowCount == 9u);
    CHECK(params.activeWidth == 320u);
    CHECK(params.activeHeight == 180u);
    CHECK(params.outputWidth == 1280u);
    CHECK(params.outputHeight == 720u);
    CHECK(params.sliceDepth[3] == 2.5f);
    CHECK(params.inverseViewProjection[2][1] == 4.0f);
    inputs.mode = lmx::engine::LightDebugView::Missed;
    CHECK(buildLightDebugParams(inputs).mode == 3u);
}
