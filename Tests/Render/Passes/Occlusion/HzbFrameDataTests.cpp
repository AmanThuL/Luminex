//----------------------------------------------------------------------------------------------------------------------
/// @file HzbFrameDataTests.cpp
/// @brief Pins the HZB frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/HzbFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("HZB reduce params halve an odd active extent upward per level", "[render][hzb]") {
    const HzbReduceParams first = buildHzbReduceParams(101, 61, 0);
    CHECK(first.sourceWidth == 101u);
    CHECK(first.sourceHeight == 61u);
    CHECK(first.destinationWidth == 51u);
    CHECK(first.destinationHeight == 31u);
    CHECK(first.sourceLevel == 0u);

    const HzbReduceParams second = buildHzbReduceParams(101, 61, 1);
    CHECK(second.sourceWidth == 51u);
    CHECK(second.sourceHeight == 31u);
    CHECK(second.destinationWidth == 26u);
    CHECK(second.destinationHeight == 16u);
    CHECK(second.sourceLevel == 0u);

    const HzbReduceParams third = buildHzbReduceParams(101, 61, 2);
    CHECK(third.sourceWidth == 26u);
    CHECK(third.destinationWidth == 13u);
    CHECK(third.destinationHeight == 8u);
    CHECK(third.sourceLevel == 1u);
}

//======================================================================================================================
TEST_CASE("HZB debug params clamp the level and zero the unused words", "[render][hzb]") {
    const HzbDebugInputs inputs{.requestedLevel = 9,
                                .levelCount = 4,
                                .activeWidth = 300,
                                .activeHeight = 200,
                                .outputWidth = 640,
                                .outputHeight = 360};
    CHECK(buildHzbDebugParams(inputs) == std::array<uint32_t, 8>{3, 300, 200, 0, 640, 360, 0, 0});

    HzbDebugInputs inside = inputs;
    inside.requestedLevel = 2;
    CHECK(buildHzbDebugParams(inside)[0] == 2u);
}
