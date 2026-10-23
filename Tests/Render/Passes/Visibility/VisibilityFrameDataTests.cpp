//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityFrameDataTests.cpp
/// @brief Pins the visibility frame data the pure builder produces.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Visibility/VisibilityFrameData.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("visibility params size capacities from the buffers and pick the layout",
          "[render][visibility]") {
    const VisibilityParams sparse = buildVisibilityParams({.dense = false,
                                                           .rowBufferBytes = 4000,
                                                           .argumentBufferBytes = 2000,
                                                           .stateCapacity = 64,
                                                           .candidateCount = 33});
    CHECK(sparse.layout == 0u);
    CHECK(sparse.rowCapacity == 1000u);
    CHECK(sparse.argumentCapacity == 100u);
    CHECK(sparse.stateCapacity == 64u);
    CHECK(sparse.candidateCount == 33u);
    CHECK(sparse.viewCount == 2u);
    CHECK(sparse.padding[0] == 0u);
    CHECK(sparse.padding[1] == 0u);

    CHECK(buildVisibilityParams({.dense = true}).layout == 1u);
}

//======================================================================================================================
TEST_CASE("visibility params cap each capacity by its override and never raise one",
          "[render][visibility]") {
    const VisibilityParamsInputs base{.rowBufferBytes = 4000,
                                      .argumentBufferBytes = 2000,
                                      .stateCapacity = 64,
                                      .capacityOverride = std::array<uint32_t, 3>{10, 1000, 8}};
    const VisibilityParams params = buildVisibilityParams(base);
    CHECK(params.rowCapacity == 10u);
    CHECK(params.argumentCapacity == 100u);
    CHECK(params.stateCapacity == 8u);
}
