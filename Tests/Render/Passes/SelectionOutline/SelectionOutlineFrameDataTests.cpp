//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutlineFrameDataTests.cpp
/// @brief Pins the selection outline frame data the pure builders produce.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/SelectionOutline/SelectionOutlineFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("outline radius scales with the backing scale and clamps to one and six",
          "[render][outline]") {
    CHECK(buildOutlineParams(0.5f).radius == 1.0f);
    CHECK(buildOutlineParams(2.0f).radius == 3.0f);
    CHECK(buildOutlineParams(10.0f).radius == 6.0f);
}

//======================================================================================================================
TEST_CASE("mask params carry the transform and row with full coverage", "[render][outline]") {
    glm::mat4 viewProjection(1.0f);
    viewProjection[3][1] = 2.0f;

    const MaskParams params = buildMaskParams(viewProjection, 17);

    CHECK(params.viewProj[3][1] == 2.0f);
    CHECK(params.instanceRow == 17u);
    CHECK(params.selected == 1.0f);
    CHECK(params.padding[0] == 0u);
    CHECK(params.padding[1] == 0u);
}
