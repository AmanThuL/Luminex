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
