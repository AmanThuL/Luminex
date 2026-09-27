#include <catch2/catch_test_macros.hpp>

#include "Core/IO/JsonWriter.h"

#include <bit>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

//======================================================================================================================
TEST_CASE("JSON writer emits stable nested document bytes", "[core][json-writer]") {
    lmx::JsonWriter writer;
    writer.beginObject();
    writer.key("name");
    writer.string("scene \"A\"\\\n");
    writer.key("look");
    writer.beginObject();
    writer.key("gain");
    writer.number(0.5f);
    writer.key("enabled");
    writer.boolean(true);
    writer.endObject();
    writer.key("position");
    writer.beginArray(true);
    writer.number(-0.0f);
    writer.number(1.25);
    writer.integer(-2);
    writer.endArray();
    writer.key("nodes");
    writer.beginArray();
    writer.beginObject();
    writer.key("id");
    writer.integer(std::numeric_limits<uint64_t>::max());
    writer.endObject();
    writer.beginArray();
    writer.endArray();
    writer.endArray();
    writer.key("empty");
    writer.beginObject();
    writer.endObject();
    writer.endObject();
    REQUIRE(writer.take() == "{\n"
                             "  \"name\": \"scene \\\"A\\\"\\\\\\u000a\",\n"
                             "  \"look\": {\n"
                             "    \"gain\": 0.5,\n"
                             "    \"enabled\": true\n"
                             "  },\n"
                             "  \"position\": [-0, 1.25, -2],\n"
                             "  \"nodes\": [\n"
                             "    {\n"
                             "      \"id\": 18446744073709551615\n"
                             "    },\n"
                             "    []\n"
                             "  ],\n"
                             "  \"empty\": {}\n"
                             "}\n");
    writer.boolean(false);
    REQUIRE(writer.take() == "false\n");
}

//======================================================================================================================
TEST_CASE("JSON inline arrays keep nested containers on one line", "[core][json-writer]") {
    lmx::JsonWriter writer;
    writer.beginArray(true);
    writer.beginObject();
    writer.key("x");
    writer.beginArray();
    writer.integer(1);
    writer.integer(2);
    writer.endArray();
    writer.endObject();
    writer.endArray();
    REQUIRE(writer.take() == "[{\"x\": [1, 2]}]\n");
}

//======================================================================================================================
TEST_CASE("Shortest JSON floats round trip every sampled finite bit pattern",
          "[core][json-writer]") {
    std::mt19937 random(0x4c4d58);
    size_t finiteCount = 0;
    while (finiteCount < 100000) {
        const auto bits = static_cast<uint32_t>(random());
        const float original = std::bit_cast<float>(bits);
        if (!std::isfinite(original)) {
            continue;
        }
        const std::string text = lmx::formatShortest(original);
        float parsed = 0.0f;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        REQUIRE(result.ec == std::errc{});
        REQUIRE(result.ptr == text.data() + text.size());
        REQUIRE(std::bit_cast<uint32_t>(parsed) == bits);
        ++finiteCount;
    }
}

//======================================================================================================================
TEST_CASE("JSON writer asserts on non-finite numbers", "[core][json-writer]") {
    for (int variant = 0; variant < 3; ++variant) {
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            std::signal(SIGABRT, SIG_DFL);
            const rlimit limit{0, 0};
            setrlimit(RLIMIT_CORE, &limit);
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
            lmx::JsonWriter writer;
            if (variant == 0) {
                writer.number(std::numeric_limits<float>::infinity());
            } else if (variant == 1) {
                writer.number(-std::numeric_limits<double>::infinity());
            } else {
                writer.number(std::numeric_limits<float>::quiet_NaN());
            }
            _exit(0);
        }
        int status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        REQUIRE(WIFSIGNALED(status));
        REQUIRE(WTERMSIG(status) == SIGABRT);
    }
}
