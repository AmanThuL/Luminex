//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureFrameDataTests.cpp
/// @brief Pins the exposure frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Exposure/ExposureFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("exposure seed params carry exp2 of the manual EV", "[render][exposure]") {
    CHECK(buildExposureSeedParams(0.0f).exposure == 1.0f);
    CHECK(buildExposureSeedParams(2.0f).exposure == 4.0f);
    CHECK(buildExposureSeedParams(-1.0f).exposure == 0.5f);
}

//======================================================================================================================
TEST_CASE("histogram params state the fixed log-luminance range and the metered extent",
          "[render][exposure]") {
    const HistogramParams params = buildHistogramParams(1280, 720);

    CHECK(params.logLuminanceMin == -12.0f);
    CHECK(params.logLuminanceMax == 4.0f);
    CHECK(params.width == 1280u);
    CHECK(params.height == 720u);
}

//======================================================================================================================
TEST_CASE("exposure resolve params carry the view settings and the adaptation step",
          "[render][exposure]") {
    const ExposureResolveParams params =
        buildExposureResolveParams({.lowPercentile = 0.1f,
                                    .highPercentile = 0.9f,
                                    .targetGrey = 0.18f,
                                    .evMin = -4.0f,
                                    .evMax = 6.0f,
                                    .compensationEv = 0.5f,
                                    .adaptUpStopsPerSecond = 3.0f,
                                    .adaptDownStopsPerSecond = 1.0f});

    CHECK(params.lowPercentile == 0.1f);
    CHECK(params.highPercentile == 0.9f);
    CHECK(params.targetGrey == 0.18f);
    CHECK(params.evMin == -4.0f);
    CHECK(params.evMax == 6.0f);
    CHECK(params.compensationEv == 0.5f);
    CHECK(params.logLuminanceMin == -12.0f);
    CHECK(params.logLuminanceMax == 4.0f);
    CHECK(params.adaptUp == 3.0f);
    CHECK(params.adaptDown == 1.0f);
    CHECK(params.deltaSeconds == 1.0f / 60.0f);
    CHECK(params.pad == 0.0f);
}
