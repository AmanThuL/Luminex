#include "App/Model/SceneTableDisplay.h"
#include <catch2/catch_test_macros.hpp>

//======================================================================================================================
TEST_CASE("scene table diagnostics retain counts bytes and retirement state",
          "[app][scene-tables]") {
    const auto fields = lmx::app::sceneTableFields({.instanceCount = 2,
                                                    .materialCount = 1,
                                                    .meshCount = 3,
                                                    .instanceCapacity = 8,
                                                    .materialCapacity = 4,
                                                    .meshCapacity = 4,
                                                    .vertexBytes = 4096,
                                                    .indexBytes = 768,
                                                    .rowsWritten = 2,
                                                    .bytesWritten = 416,
                                                    .slot = 2,
                                                    .growthEvents = 1,
                                                    .pendingReleaseBuffers = 3});
    REQUIRE(fields[0].value == "2 / 8 rows");
    REQUIRE(fields[1].value == "1 / 4 rows");
    REQUIRE(fields[2].value == "3 / 4 rows");
    REQUIRE(fields[3].value == "4096 vertex bytes; 768 index bytes");
    REQUIRE(fields[4].value == "2 rows; 416 bytes");
    REQUIRE(fields[5].value == "3 / 3");
    REQUIRE(fields[6].value == "1");
    REQUIRE(fields[7].value == "3 buffers");
    const auto empty = lmx::app::sceneTableFields({});
    REQUIRE(empty[4].value == "0 rows; 0 bytes");
    REQUIRE(empty[7].value == "0 buffers");
}
