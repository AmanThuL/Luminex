#include <catch2/catch_test_macros.hpp>

#include "Core/Assert.h"
#include "Core/Log.h"

TEST_CASE("log init is idempotent", "[core]") {
    lmx::log::init();
    lmx::log::init(); // second call must not throw or duplicate sinks
    LMX_LOG_INFO("hello from tests");
    SUCCEED();
}
