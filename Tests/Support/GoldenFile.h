#pragma once

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace lmx::test {

//======================================================================================================================
// The Tests binary runs with its own target dir as CWD, so the golden files are addressed from the
// repo root the build passes in.
inline std::string goldenPath(std::string_view name) {
    return std::string(LMX_REPO_ROOT) + "/Tests/Golden/" + std::string(name);
}

//======================================================================================================================
// Compares against the golden file, and on a mismatch writes what was actually produced beside it
// so a failing run leaves the two versions to diff rather than a boolean.
inline void requireMatchesGolden(const std::string& dump, std::string_view name) {
    const std::string path = goldenPath(name);
    std::ifstream file(path, std::ios::binary);
    INFO("golden file: " + path);
    REQUIRE(file.good());

    std::ostringstream expected;
    expected << file.rdbuf();

    if (expected.str() != dump) {
        std::ofstream actual(path + ".actual", std::ios::binary | std::ios::trunc);
        actual << dump;
        INFO("actual output written to: " + path + ".actual");
        INFO("--- actual ---\n" + dump);
    }
    REQUIRE(expected.str() == dump);
}

} // namespace lmx::test
