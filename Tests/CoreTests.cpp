#include <catch2/catch_test_macros.hpp>

#include "Core/Align.h"
#include "Core/Assert.h"
#include "Core/Log.h"

TEST_CASE("log init is idempotent", "[core]") {
    lmx::log::init();
    lmx::log::init(); // second call must not throw or duplicate sinks
    LMX_LOG_INFO("hello from tests");
    SUCCEED();
}

TEST_CASE("alignUp rounds to the next multiple", "[core]") {
    STATIC_REQUIRE(lmx::alignUp(0, 256) == 0);
    STATIC_REQUIRE(lmx::alignUp(1, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(256, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(257, 256) == 512);
    STATIC_REQUIRE(lmx::alignUp(144, 16) == 144);
}
