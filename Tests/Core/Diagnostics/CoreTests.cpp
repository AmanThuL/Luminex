#include <catch2/catch_test_macros.hpp>

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Diagnostics/LogSink.h"
#include "Core/Math/Align.h"

#include <string>
#include <vector>

//======================================================================================================================
TEST_CASE("log init is idempotent", "[core]") {
    lmx::log::init();
    lmx::log::init(); // second call must not throw or duplicate sinks
    LMX_LOG_INFO("hello from tests");
    SUCCEED();
}

//======================================================================================================================
TEST_CASE("debug records reach a subscriber at runtime", "[core]") {
    lmx::log::init();
    std::vector<std::string> debug;
    {
        const lmx::log::SinkSubscription subscription([&](const lmx::log::Message& message) {
            if (message.level == lmx::log::Level::Debug)
                debug.emplace_back(message.text);
        });
        LMX_LOG_DEBUG("debug {} from tests", 7);
    }
    REQUIRE(debug == std::vector<std::string>{"debug 7 from tests"});
}

//======================================================================================================================
TEST_CASE("alignUp rounds to the next multiple", "[core]") {
    STATIC_REQUIRE(lmx::alignUp(0, 256) == 0);
    STATIC_REQUIRE(lmx::alignUp(1, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(256, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(257, 256) == 512);
    STATIC_REQUIRE(lmx::alignUp(144, 16) == 144);
}
