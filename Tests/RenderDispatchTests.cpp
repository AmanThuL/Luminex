//----------------------------------------------------------------------------------------------------------------------
/// @file RenderDispatchTests.cpp
/// @brief Checks compute coverage and overflow boundaries for the shared dispatch helper.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/Dispatch.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

//======================================================================================================================
TEST_CASE("Two-dimensional dispatch covers each axis within its arithmetic precondition",
          "[render]") {
    using lmx::render::dispatchGroups2D;
    CHECK((dispatchGroups2D(0, 0) == std::array<uint32_t, 2>{0, 0}));
    CHECK((dispatchGroups2D(0, 17) == std::array<uint32_t, 2>{0, 3}));
    CHECK((dispatchGroups2D(1, 8) == std::array<uint32_t, 2>{1, 1}));
    CHECK((dispatchGroups2D(9, 16) == std::array<uint32_t, 2>{2, 2}));
    CHECK((dispatchGroups2D(1920, 1080) == std::array<uint32_t, 2>{240, 135}));
    constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max() - 7;
    CHECK((dispatchGroups2D(maximum, maximum) == std::array<uint32_t, 2>{536870911, 536870911}));
}
