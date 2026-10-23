#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuContext.h"

#include <array>
#include <vector>

//======================================================================================================================
TEST_CASE("shaderLibraryPath prefixes the shader directory", "[engine]") {
    CHECK(lmx::engine::shaderLibraryPath("Bloom") == "Shaders/Bloom");
}

//======================================================================================================================
TEST_CASE("copyPassTimings copies labels and milliseconds in order", "[engine]") {
    const std::array<rojoRHI::PassTiming, 2> source = {{{"shadow", 0.5}, {"lighting", 1.25}}};
    std::vector<lmx::engine::PassTiming> destination;

    lmx::engine::copyPassTimings(source, destination);

    REQUIRE(destination.size() == 2);
    CHECK(destination[0].label == "shadow");
    CHECK(destination[0].gpuMilliseconds == 0.5);
    CHECK(destination[1].label == "lighting");
    CHECK(destination[1].gpuMilliseconds == 1.25);
}

//======================================================================================================================
TEST_CASE("copyPassTimings reuses the destination", "[engine]") {
    const std::array<rojoRHI::PassTiming, 3> first = {{{"a", 1.0}, {"b", 2.0}, {"c", 3.0}}};
    const std::array<rojoRHI::PassTiming, 1> second = {{{"d", 4.0}}};
    std::vector<lmx::engine::PassTiming> destination;
    lmx::engine::copyPassTimings(first, destination);
    const auto capacity = destination.capacity();

    lmx::engine::copyPassTimings(second, destination);

    REQUIRE(destination.size() == 1);
    CHECK(destination[0].label == "d");
    CHECK(destination[0].gpuMilliseconds == 4.0);
    CHECK(destination.capacity() == capacity);
}

//======================================================================================================================
TEST_CASE("toUpscalerSupport copies availability, range and name", "[engine]") {
    const rojoRHI::TemporalScalerSupport support{
        .available = true, .minInputScale = 0.5f, .maxInputScale = 1.0f, .name = "Temporal"};

    const lmx::engine::UpscalerSupportInfo info = lmx::engine::toUpscalerSupport(support);

    CHECK(info.available);
    CHECK(info.minInputScale == 0.5f);
    CHECK(info.maxInputScale == 1.0f);
    CHECK(info.name == "Temporal");
}
