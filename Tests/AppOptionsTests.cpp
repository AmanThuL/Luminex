#include <catch2/catch_test_macros.hpp>

#include "App/AppOptions.h"

#include <array>
#include <string_view>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("app options default to a windowed Sponza scene", "[app][options]") {
    const AppOptionsResult result = parseAppOptions({});

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Windowed);
    REQUIRE(lmx::engine::sceneIdString(result->initialScene) == "sponza");
    REQUIRE(result->screenshotPath.empty());
}

//======================================================================================================================
TEST_CASE("app options select screenshot mode and a stable scene ID", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--scene"}, std::string_view{"sponza"},
                                      std::string_view{"--screenshot"},
                                      std::string_view{"capture.bmp"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Screenshot);
    REQUIRE(lmx::engine::sceneIdString(result->initialScene) == "sponza");
    REQUIRE(result->screenshotPath == "capture.bmp");
}

//======================================================================================================================
TEST_CASE("an empty screenshot path preserves windowed mode", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--screenshot"}, std::string_view{""}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Windowed);
    REQUIRE(result->screenshotPath.empty());
}

//======================================================================================================================
TEST_CASE("app options reject unknown scene IDs", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--scene"}, std::string_view{"Sponza"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message ==
            "unknown scene ID 'Sponza'; valid IDs: sponza, damaged-helmet");
}

//======================================================================================================================
TEST_CASE("app options reject missing option values", "[app][options]") {
    constexpr std::array screenshot = {std::string_view{"--screenshot"}};
    constexpr std::array scene = {std::string_view{"--scene"}};

    const AppOptionsResult screenshotResult = parseAppOptions(screenshot);
    const AppOptionsResult sceneResult = parseAppOptions(scene);

    REQUIRE_FALSE(screenshotResult);
    REQUIRE(screenshotResult.error().message ==
            "--screenshot needs an output path: App --screenshot <out.bmp>");
    REQUIRE_FALSE(sceneResult);
    REQUIRE(sceneResult.error().message ==
            "--scene needs an ID: App --scene <sponza|damaged-helmet>");
}

//======================================================================================================================
TEST_CASE("app options reject unknown arguments", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--unknown"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message ==
            "unknown argument '--unknown'; usage: App [--screenshot <out.bmp>] [--scene "
            "<sponza|damaged-helmet>]");
}

//======================================================================================================================
TEST_CASE("app options keep the last repeated values and ignore a bare separator",
          "[app][options]") {
    constexpr std::array arguments = {
        std::string_view{"--scene"},
        std::string_view{"sponza"},
        std::string_view{"--"},
        std::string_view{"--scene"},
        std::string_view{"damaged-helmet"},
        std::string_view{"--screenshot"},
        std::string_view{"first.bmp"},
        std::string_view{"--screenshot"},
        std::string_view{"last.bmp"},
    };
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Screenshot);
    REQUIRE(lmx::engine::sceneIdString(result->initialScene) == "damaged-helmet");
    REQUIRE(result->screenshotPath == "last.bmp");
}
