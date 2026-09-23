#include "Core/Util/Stopwatch.h"
#include "Core/Util/String.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

//======================================================================================================================
TEST_CASE("Stopwatch elapsed time is non-negative and grows across sleep", "[core]") {
    lmx::Stopwatch stopwatch;
    const double initialElapsed = stopwatch.elapsedMilliseconds();
    REQUIRE(initialElapsed >= 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const double afterSleepElapsed = stopwatch.elapsedMilliseconds();
    REQUIRE(afterSleepElapsed > initialElapsed);
}

//======================================================================================================================
TEST_CASE("Stopwatch restart lowers elapsed time", "[core]") {
    lmx::Stopwatch stopwatch;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const double beforeRestart = stopwatch.elapsedMilliseconds();
    stopwatch.restart();
    const double afterRestart = stopwatch.elapsedMilliseconds();
    REQUIRE(afterRestart < beforeRestart);
}

//======================================================================================================================
TEST_CASE("toLowerAscii converts only ASCII uppercase letters", "[core]") {
    const std::string result = lmx::toLowerAscii("AbC-Ω1");
    REQUIRE(result == "abc-Ω1");
}

//======================================================================================================================
TEST_CASE("Stopwatch captured boundary stays fixed and shares adjacent intervals", "[core]") {
    lmx::Stopwatch start;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lmx::Stopwatch boundary;
    const double captured = start.elapsedMillisecondsUntil(boundary);
    REQUIRE(captured > 0);
    REQUIRE(start.elapsedMillisecondsUntil(start) == 0);
    REQUIRE(boundary.elapsedMillisecondsUntil(start) == -captured);
    REQUIRE(start.elapsedMilliseconds() >= captured);
    REQUIRE(start.elapsedMillisecondsUntil(boundary) == captured);
    start.restart();
    REQUIRE(start.elapsedMillisecondsUntil(boundary) <= 0);
}
