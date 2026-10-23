//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayFrameDataTests.cpp
/// @brief Pins the display frame data the pure builder produces.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Display/DisplayFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("display params write zero bloom when bloom is off", "[render][display]") {
    CHECK(buildDisplayParams(false, 0.7f).bloomIntensity == 0.0f);
    CHECK(buildDisplayParams(true, 0.7f).bloomIntensity == 0.7f);
}
