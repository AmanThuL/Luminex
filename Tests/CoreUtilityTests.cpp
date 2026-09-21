#include "Core/IO/File.h"
#include "Core/IO/Json.h"
#include "Core/Math/Align.h"
#include "Core/Math/Scalar.h"
#include "Core/Parse.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

//======================================================================================================================
TEST_CASE("alignment contracts keep general multiples and zero distinct", "[core]") {
    STATIC_REQUIRE(lmx::alignUpMultiple(0, 0) == 0);
    STATIC_REQUIRE(lmx::alignUpMultiple(17, 0) == 17);
    STATIC_REQUIRE(lmx::alignUpMultiple(17, 3) == 18);
    STATIC_REQUIRE(lmx::alignUpMultiple(18, 3) == 18);
    STATIC_REQUIRE(lmx::alignUpMultiple(17, 1) == 17);
    for (uint64_t alignment : {1u, 2u, 16u, 256u}) {
        for (uint64_t value : {0u, 1u, 17u, 256u, 257u}) {
            REQUIRE(lmx::alignUp(value, alignment) == lmx::alignUpMultiple(value, alignment));
        }
    }
}

//======================================================================================================================
TEST_CASE("integer dispatch division rounds up without adding an empty group", "[core]") {
    STATIC_REQUIRE(lmx::divRoundUp(0, 8) == 0);
    STATIC_REQUIRE(lmx::divRoundUp(1, 8) == 1);
    STATIC_REQUIRE(lmx::divRoundUp(8, 8) == 1);
    STATIC_REQUIRE(lmx::divRoundUp(9, 8) == 2);
    STATIC_REQUIRE(lmx::divRoundUp(100, 1) == 100);
}

//======================================================================================================================
TEST_CASE("JSON escaping preserves framing control spelling and UTF-8 bytes", "[core]") {
    std::string bytes;
    for (int value = 0; value < 256; ++value) {
        bytes += static_cast<char>(value);
    }
    const std::string escaped = lmx::jsonEscape(bytes);
    const std::string controls = "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
                                 "\\u0008\\u0009\\u000a\\u000b\\u000c\\u000d\\u000e\\u000f"
                                 "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
                                 "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f";
    REQUIRE(escaped.starts_with(controls));
    REQUIRE(escaped.substr(escaped.size() - 128) == bytes.substr(128));
    REQUIRE(lmx::jsonEscape("\"\\\t\n\r") == "\\\"\\\\\\u0009\\u000a\\u000d");
    REQUIRE(lmx::jsonEscape("场景") == "场景");
    REQUIRE(lmx::jsonEscape("").empty());
    std::string framed = "prefix:";
    lmx::appendJsonEscaped(framed, "\"x\"");
    REQUIRE(framed == "prefix:\\\"x\\\"");
}

//======================================================================================================================
TEST_CASE("numeric parsing leaves domain range and finiteness at the caller", "[core]") {
    uint32_t count = 0;
    REQUIRE(lmx::parseNumber("0", count));
    REQUIRE(count == 0);
    REQUIRE(lmx::parseNumber("4294967295", count));
    REQUIRE(count == UINT32_MAX);
    for (const std::string_view text : {"", "+1", "-1", " 1", "1 ", "1x", "4294967296"}) {
        REQUIRE_FALSE(lmx::parseNumber(text, count));
    }
    int signedCount = 0;
    REQUIRE(lmx::parseNumber("-1", signedCount));
    REQUIRE(signedCount == -1);
    float number = 0.0f;
    REQUIRE(lmx::parseNumber("1.25e2", number));
    REQUIRE(number == 125.0f);
    REQUIRE(lmx::parseNumber("-0", number));
    REQUIRE(std::signbit(number));
    REQUIRE(lmx::parseNumber("inf", number));
    REQUIRE(std::isinf(number));
    REQUIRE(lmx::parseNumber("nan", number));
    REQUIRE(std::isnan(number));
    REQUIRE_FALSE(lmx::parseNumber("0.5tail", number));
    REQUIRE_FALSE(lmx::parseNumber("1e999", number));
    REQUIRE_FALSE(lmx::parseNumber(std::string_view{}, number));
}

//======================================================================================================================
TEST_CASE("whole-file reading distinguishes missing files from empty binary content", "[core]") {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("luminex-core-file-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path = directory / "bytes.bin";
    const auto missing = lmx::readWholeFile(path);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error() == lmx::FileReadError::Open);
    {
        std::ofstream empty(path, std::ios::binary);
    }
    const auto empty = lmx::readWholeFile(path);
    REQUIRE(empty);
    REQUIRE(empty->empty());
    const std::array<char, 5> bytes{0, '\r', '\n', static_cast<char>(0xff), 'x'};
    {
        std::ofstream binary(path, std::ios::binary);
        binary.write(bytes.data(), bytes.size());
    }
    const auto binary = lmx::readWholeFile(path);
    REQUIRE(binary);
    REQUIRE(binary->size() == bytes.size());
    for (size_t index = 0; index < bytes.size(); ++index) {
        REQUIRE((*binary)[index] == static_cast<std::byte>(bytes[index]));
    }
    std::filesystem::remove_all(directory);
}
