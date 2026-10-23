//----------------------------------------------------------------------------------------------------------------------
/// @file BloomFrameDataTests.cpp
/// @brief Pins the bloom frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Bloom/BloomFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("bloom threshold params copy the scene and bloom extents", "[render][bloom]") {
    const BloomThresholdParams params = buildBloomThresholdParams({.threshold = 1.5f,
                                                                   .sceneWidth = 101,
                                                                   .sceneHeight = 61,
                                                                   .bloomWidth = 51,
                                                                   .bloomHeight = 31});

    CHECK(params.threshold == 1.5f);
    CHECK(params.srcWidth == 101u);
    CHECK(params.srcHeight == 61u);
    CHECK(params.dstWidth == 51u);
    CHECK(params.dstHeight == 31u);
}

//======================================================================================================================
TEST_CASE("bloom downsample params halve the mip extent and stop at one", "[render][bloom]") {
    const BloomDownsampleParams first = buildBloomDownsampleParams(51, 31, 1);
    CHECK(first.srcWidth == 51u);
    CHECK(first.srcHeight == 31u);
    CHECK(first.dstWidth == 25u);
    CHECK(first.dstHeight == 15u);

    const BloomDownsampleParams clamped = buildBloomDownsampleParams(8, 2, 3);
    CHECK(clamped.srcWidth == 2u);
    CHECK(clamped.srcHeight == 1u);
    CHECK(clamped.dstWidth == 1u);
    CHECK(clamped.dstHeight == 1u);
}

//======================================================================================================================
TEST_CASE("bloom upsample params name the smaller mip and the destination", "[render][bloom]") {
    const BloomUpsampleParams params = buildBloomUpsampleParams(51, 31, 1);
    CHECK(params.smallWidth == 12u);
    CHECK(params.smallHeight == 7u);
    CHECK(params.dstWidth == 25u);
    CHECK(params.dstHeight == 15u);

    const BloomUpsampleParams clamped = buildBloomUpsampleParams(8, 2, 1);
    CHECK(clamped.smallWidth == 2u);
    CHECK(clamped.smallHeight == 1u);
    CHECK(clamped.dstWidth == 4u);
    CHECK(clamped.dstHeight == 1u);
}
