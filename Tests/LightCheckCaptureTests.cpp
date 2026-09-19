//----------------------------------------------------------------------------------------------------------------------
/// @file LightCheckCaptureTests.cpp
/// @brief Pins the independent auditor's portable raw-light-evidence record layout.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/LightCheckCapture.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

//======================================================================================================================
TEST_CASE("light check capture has a fixed little-endian layout", "[app][light-check]") {
    lmx::render::LightClusterCheckFrame frame;
    frame.frameNumber = 0x0102030405060708ULL;
    frame.params.activeWidth = 641;
    frame.params.activeHeight = 359;
    frame.params.rowCount = 9;
    frame.gpu.grid.resize(lmx::render::kClusterCount);
    frame.gpu.grid[0] = {0, 2};
    frame.gpu.indices = {1, 8};
    frame.gpu.counters = {2, 2, 0, 0, 0, 2};
    frame.cpu = frame.gpu;
    std::ostringstream out;
    REQUIRE(lmx::app::writeLightCheckHeader(out));
    REQUIRE(lmx::app::writeLightCheckFrame(out, frame));
    const std::string bytes = out.str();
    CHECK(bytes.substr(0, 8) == std::string("LMXLC01\0", 8));
    for (size_t i = 0; i < 8; ++i)
        CHECK(static_cast<uint8_t>(bytes[8 + i]) == 8 - i);
    const auto readWord = [&](size_t offset) {
        uint32_t value = 0;
        for (uint32_t i = 0; i < 4; ++i)
            value |= uint32_t{static_cast<uint8_t>(bytes[offset + i])} << (i * 8);
        return value;
    };
    CHECK(readWord(16) == 641);
    CHECK(readWord(20) == 359);
    CHECK(readWord(24) == 9);
    CHECK(readWord(28) == lmx::render::kMaxLightsPerCluster);
    CHECK(readWord(32) == lmx::render::kLightClusterIndexCapacity);
    CHECK(readWord(36) == lmx::render::kClusterCount);
    CHECK(readWord(40) == 2);
    CHECK(readWord(44) == 2);
    CHECK(readWord(48) == 2);
    CHECK(readWord(72) == 2);
    CHECK(readWord(100) == 2);
    const size_t indicesStart = 8 + 40 + 48 + 16 * lmx::render::kClusterCount;
    CHECK(readWord(indicesStart) == 1);
    CHECK(readWord(indicesStart + 4) == 8);
    CHECK(readWord(indicesStart + 8) == 1);
    CHECK(readWord(indicesStart + 12) == 8);
    CHECK(bytes.size() == indicesStart + 16);

    frame.gpu.indices.pop_back();
    std::ostringstream malformed;
    CHECK_FALSE(lmx::app::writeLightCheckFrame(malformed, frame));
    CHECK(malformed.str().empty());
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    CHECK_FALSE(lmx::app::writeLightCheckHeader(failed));
}
