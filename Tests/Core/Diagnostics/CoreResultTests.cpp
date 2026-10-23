#include <catch2/catch_test_macros.hpp>

#include "Core/Diagnostics/Result.h"

#include <string>
#include <utility>

//======================================================================================================================
TEST_CASE("lmx::Result carries a value or an error", "[core]") {
    lmx::Result<int> value = 7;
    REQUIRE(value.has_value());
    CHECK(*value == 7);

    lmx::Result<int> failure = std::unexpected(lmx::Error{lmx::ErrorCode::NotFound, "missing"});
    REQUIRE_FALSE(failure.has_value());
    CHECK(failure.error().code == lmx::ErrorCode::NotFound);
}

//======================================================================================================================
TEST_CASE("lmx::Error keeps code and message", "[core]") {
    const lmx::Error error{lmx::ErrorCode::Parse, "bad token"};
    CHECK(error.code == lmx::ErrorCode::Parse);
    CHECK(error.message == "bad token");
}
