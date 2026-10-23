//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalFrameDataTests.cpp
/// @brief Pins temporal parameter layouts and the plain values each kernel consumes.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Temporal/TemporalFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("native temporal params keep camera transforms and history flags", "[render][temporal]") {
    CameraFrameState camera;
    camera.inverseViewProjection = glm::mat4(2.0f);
    CameraFrameState previous;
    previous.viewProjection = glm::mat4(3.0f);
    previous.nearZ = 0.125f;
    TemporalResolveParamsInputs inputs{.extents = {640, 360, 1280, 720},
                                       .camera = camera,
                                       .previousCamera = previous,
                                       .historyValid = true,
                                       .rejectionWanted = true,
                                       .reprojectedWanted = true};
    const auto valid = buildTemporalResolveParams(inputs);
    CHECK(valid.width == 1280u);
    CHECK(valid.height == 720u);
    CHECK(valid.historyValid == 1u);
    CHECK(valid.writeDiagnostics == 3u);
    CHECK(valid.inverseViewProjection == camera.inverseViewProjection);
    CHECK(valid.previousViewProjection == previous.viewProjection);
    CHECK(valid.previousNearZ == 0.125f);
    for (const float pad : valid.pad) {
        CHECK(pad == 0.0f);
    }

    inputs.reprojectedWanted = false;
    CHECK(buildTemporalResolveParams(inputs).writeDiagnostics == 1u);
    inputs.rejectionWanted = false;
    inputs.reprojectedWanted = true;
    CHECK(buildTemporalResolveParams(inputs).writeDiagnostics == 2u);

    inputs.historyValid = false;
    inputs.rejectionWanted = false;
    inputs.reprojectedWanted = false;
    const auto reset = buildTemporalResolveParams(inputs);
    CHECK(reset.historyValid == 0u);
    CHECK(reset.writeDiagnostics == 0u);
    CHECK(reset.previousViewProjection == previous.viewProjection);
}

//======================================================================================================================
TEST_CASE("temporal upscale params distinguish active previous and output extents",
          "[render][temporal]") {
    CameraFrameState camera;
    camera.inverseViewProjection = glm::mat4(4.0f);
    camera.jitterPixels = {0.25f, -0.375f};
    CameraFrameState previous;
    previous.viewProjection = glm::mat4(5.0f);
    previous.nearZ = 0.0625f;
    TemporalUpscaleParamsInputs inputs{.extents = {800, 449, 1600, 900},
                                       .previousExtents = {960, 540, 1600, 900},
                                       .camera = camera,
                                       .previousCamera = previous,
                                       .historyValid = true,
                                       .rejectionWanted = false,
                                       .reprojectedWanted = true};
    const auto params = buildTemporalUpscaleParams(inputs);
    CHECK(params.width == 800u);
    CHECK(params.height == 449u);
    CHECK(params.historyValid == 1u);
    CHECK(params.writeDiagnostics == 2u);
    CHECK(params.inverseViewProjection == camera.inverseViewProjection);
    CHECK(params.previousViewProjection == previous.viewProjection);
    CHECK(params.previousNearZ == 0.0625f);
    CHECK(params.outputWidth == 1600u);
    CHECK(params.outputHeight == 900u);
    CHECK(params.allocatedWidth == 1600u);
    CHECK(params.allocatedHeight == 900u);
    CHECK(params.previousRenderWidth == 960u);
    CHECK(params.previousRenderHeight == 540u);
    CHECK(params.jitterOffset == glm::vec2(0.25f, 0.375f));
    for (const float pad : params.pad) {
        CHECK(pad == 0.0f);
    }

    inputs.historyValid = false;
    CHECK(buildTemporalUpscaleParams(inputs).historyValid == 0u);
}

//======================================================================================================================
TEST_CASE("spatial upscale params keep allocation capacity and texture-space jitter",
          "[render][temporal]") {
    const auto params = buildSpatialUpscaleParams({853, 479, 1706, 958}, {-0.25f, 0.125f});
    CHECK(params.renderWidth == 853u);
    CHECK(params.renderHeight == 479u);
    CHECK(params.outputWidth == 1706u);
    CHECK(params.outputHeight == 958u);
    CHECK(params.allocatedWidth == 1706u);
    CHECK(params.allocatedHeight == 958u);
    CHECK(params.jitterOffset == glm::vec2(-0.25f, -0.125f));
}

//======================================================================================================================
TEST_CASE("temporal reproject params dispatch at output extent and read the active rectangle",
          "[render][temporal]") {
    const auto params = buildTemporalReprojectParams({853, 479, 1706, 958}, {0.125f, -0.25f});
    CHECK(params.width == 1706u);
    CHECK(params.height == 958u);
    CHECK(params.renderWidth == 853u);
    CHECK(params.renderHeight == 479u);
    CHECK(params.allocatedWidth == 1706u);
    CHECK(params.allocatedHeight == 958u);
    CHECK(params.jitterOffset == glm::vec2(0.125f, 0.25f));
}

//======================================================================================================================
TEST_CASE("temporal debug params keep shader selectors and the two pixel extents",
          "[render][temporal]") {
    const FrameExtents extents{640, 360, 1280, 720};
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::Off, extents).view == 1u);
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::MotionVectors, extents).view == 1u);
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::ReprojectionError, extents).view == 2u);
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::ReprojectedHistory, extents).view == 3u);
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::RejectionMask, extents).view == 4u);
    CHECK(buildTemporalDebugViewParams(TemporalDebugView::BlendWeight, extents).view == 5u);
    const auto params = buildTemporalDebugViewParams(TemporalDebugView::HistoryAge, extents);
    CHECK(params.view == 6u);
    CHECK(params.renderWidth == 640u);
    CHECK(params.renderHeight == 360u);
    CHECK(params.outputWidth == 1280u);
    CHECK(params.outputHeight == 720u);
}

//======================================================================================================================
TEST_CASE("vendor packing params cover the active render extent", "[render][temporal]") {
    const auto params = buildVendorPackParams({853, 479, 1706, 958});
    CHECK(params.width == 853u);
    CHECK(params.height == 479u);
}
