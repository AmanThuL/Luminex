#include "Support/TrackerFrameSupport.h"

#include <catch2/catch_test_macros.hpp>

//======================================================================================================================
TEST_CASE("the minimal frame runs four frames under the validation tracker",
          "[render][device][tracker]") {
    SECTION("empty") {
        lmx::test::runTrackedFrames("empty", {});
    }
    SECTION("material-lab") {
        lmx::test::runTrackedFrames("material-lab", {});
    }
}

//======================================================================================================================
TEST_CASE("the display read can be culled and restored under the tracker",
          "[render][device][tracker]") {
    lmx::test::runTrackedFrames("empty",
                                {.readDisplay = [](uint32_t frame) { return frame != 2; }});
}
