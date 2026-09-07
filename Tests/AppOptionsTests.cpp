#include <catch2/catch_test_macros.hpp>

#include "App/AppOptions.h"
#include "Render/Renderer.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("app options default to a windowed Sponza scene", "[app][options]") {
    const AppOptionsResult result = parseAppOptions({});

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Windowed);
    REQUIRE(lmx::engine::sceneIdString(result->initialScene) == "sponza");
    REQUIRE(result->screenshotPath.empty());
    REQUIRE(result->maximized);
}

//======================================================================================================================
TEST_CASE("--windowed clears the maximized default", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--windowed"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE_FALSE(result->maximized);
}

//======================================================================================================================
TEST_CASE("--windowed composes with --scene", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--scene"},
                                      std::string_view{"damaged-helmet"},
                                      std::string_view{"--windowed"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE_FALSE(result->maximized);
    REQUIRE(lmx::engine::sceneIdString(result->initialScene) == "damaged-helmet");
}

//======================================================================================================================
TEST_CASE("--windowed is irrelevant but accepted in screenshot mode", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--windowed"},
                                      std::string_view{"--screenshot"},
                                      std::string_view{"capture.bmp"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->mode == RunMode::Screenshot);
    REQUIRE_FALSE(result->maximized);
    REQUIRE(result->screenshotPath == "capture.bmp");
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
            "unknown scene ID 'Sponza'; valid IDs: sponza, damaged-helmet, milk-truck, "
            "material-lab, temporal-lab");
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
            "--scene needs an ID: App --scene "
            "<sponza|damaged-helmet|milk-truck|material-lab|temporal-lab>");
}

//======================================================================================================================
TEST_CASE("app options reject unknown arguments", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--unknown"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message ==
            "unknown argument '--unknown'; usage: App [--screenshot <out.bmp>] [--scene "
            "<sponza|damaged-helmet|milk-truck|material-lab|temporal-lab>] [--windowed] "
            "[--frames <N>] [--temporal <off|raw|taa>] "
            "[--temporal-view <off|motion|reprojection|reprojected|rejection|weight|age>] "
            "(--frames N renders N frames and captures the last, including temporal warmup)");
}

//======================================================================================================================
TEST_CASE("app options default to one frame with native TAA on", "[app][options]") {
    const AppOptionsResult result = parseAppOptions({});

    REQUIRE(result);
    REQUIRE(result->frames == 1);
    REQUIRE(result->temporal == TemporalMode::Taa);
    REQUIRE(result->temporalView == render::TemporalDebugView::Off);
}

//======================================================================================================================
TEST_CASE("--frames sets the frame count", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--frames"}, std::string_view{"4"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->frames == 4);
}

//======================================================================================================================
TEST_CASE("--frames rejects zero, negative and non-numeric counts", "[app][options]") {
    constexpr std::array zero = {std::string_view{"--frames"}, std::string_view{"0"}};
    constexpr std::array negative = {std::string_view{"--frames"}, std::string_view{"-1"}};
    constexpr std::array nonNumeric = {std::string_view{"--frames"}, std::string_view{"four"}};

    const AppOptionsResult zeroResult = parseAppOptions(zero);
    const AppOptionsResult negativeResult = parseAppOptions(negative);
    const AppOptionsResult nonNumericResult = parseAppOptions(nonNumeric);

    REQUIRE_FALSE(zeroResult);
    REQUIRE(zeroResult.error().message ==
            "--frames needs a positive integer: App --frames <N> (N >= 1)");
    REQUIRE_FALSE(negativeResult);
    REQUIRE(negativeResult.error().message ==
            "--frames needs a positive integer: App --frames <N> (N >= 1)");
    REQUIRE_FALSE(nonNumericResult);
    REQUIRE(nonNumericResult.error().message ==
            "--frames needs a positive integer: App --frames <N> (N >= 1)");
}

//======================================================================================================================
TEST_CASE("--frames needs a value", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--frames"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message == "--frames needs a count: App --frames <N> (N >= 1)");
}

//======================================================================================================================
TEST_CASE("bare --temporal means taa", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->temporal == TemporalMode::Taa);
    REQUIRE(result->temporalView == render::TemporalDebugView::Off);
}

//======================================================================================================================
TEST_CASE("--temporal accepts off, raw and taa", "[app][options]") {
    constexpr std::array off = {std::string_view{"--temporal"}, std::string_view{"off"}};
    constexpr std::array raw = {std::string_view{"--temporal"}, std::string_view{"raw"}};
    constexpr std::array taa = {std::string_view{"--temporal"}, std::string_view{"taa"}};

    const AppOptionsResult offResult = parseAppOptions(off);
    const AppOptionsResult rawResult = parseAppOptions(raw);
    const AppOptionsResult taaResult = parseAppOptions(taa);

    REQUIRE(offResult);
    REQUIRE(offResult->temporal == TemporalMode::Off);
    REQUIRE(rawResult);
    REQUIRE(rawResult->temporal == TemporalMode::Raw);
    REQUIRE(taaResult);
    REQUIRE(taaResult->temporal == TemporalMode::Taa);
}

//======================================================================================================================
// A following token starting with "--" is the next option, not a value -- --temporal defaults to
// taa when followed immediately by another flag.
TEST_CASE("--temporal followed by another flag defaults to taa", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal"},
                                      std::string_view{"--windowed"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->temporal == TemporalMode::Taa);
    REQUIRE_FALSE(result->maximized);
}

//======================================================================================================================
TEST_CASE("--temporal rejects an unknown value, naming the accepted ones", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal"}, std::string_view{"sideways"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message == "--temporal needs one of off|raw|taa, got 'sideways'");
}

//======================================================================================================================
TEST_CASE("--temporal-view accepts every named diagnostic without changing the mode",
          "[app][options]") {
    constexpr std::array off = {std::string_view{"--temporal-view"}, std::string_view{"off"}};
    constexpr std::array motion = {std::string_view{"--temporal-view"}, std::string_view{"motion"}};
    constexpr std::array reprojection = {std::string_view{"--temporal-view"},
                                         std::string_view{"reprojection"}};
    constexpr std::array reprojected = {std::string_view{"--temporal-view"},
                                        std::string_view{"reprojected"}};
    constexpr std::array rejection = {std::string_view{"--temporal-view"},
                                      std::string_view{"rejection"}};
    constexpr std::array weight = {std::string_view{"--temporal-view"}, std::string_view{"weight"}};
    constexpr std::array age = {std::string_view{"--temporal-view"}, std::string_view{"age"}};

    const AppOptionsResult offResult = parseAppOptions(off);
    const AppOptionsResult motionResult = parseAppOptions(motion);
    const AppOptionsResult reprojectionResult = parseAppOptions(reprojection);
    const AppOptionsResult reprojectedResult = parseAppOptions(reprojected);
    const AppOptionsResult rejectionResult = parseAppOptions(rejection);
    const AppOptionsResult weightResult = parseAppOptions(weight);
    const AppOptionsResult ageResult = parseAppOptions(age);

    REQUIRE(offResult);
    REQUIRE(offResult->temporal == TemporalMode::Taa);
    REQUIRE(offResult->temporalView == render::TemporalDebugView::Off);
    REQUIRE(motionResult);
    REQUIRE(motionResult->temporal == TemporalMode::Taa);
    REQUIRE(motionResult->temporalView == render::TemporalDebugView::MotionVectors);
    REQUIRE(reprojectionResult);
    REQUIRE(reprojectionResult->temporalView == render::TemporalDebugView::ReprojectionError);
    REQUIRE(reprojectedResult);
    REQUIRE(reprojectedResult->temporalView == render::TemporalDebugView::ReprojectedHistory);
    REQUIRE(rejectionResult);
    REQUIRE(rejectionResult->temporalView == render::TemporalDebugView::RejectionMask);
    REQUIRE(weightResult);
    REQUIRE(weightResult->temporalView == render::TemporalDebugView::BlendWeight);
    REQUIRE(ageResult);
    REQUIRE(ageResult->temporalView == render::TemporalDebugView::HistoryAge);
}

//======================================================================================================================
TEST_CASE("--temporal-view rejects an unknown value, naming the accepted ones", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal-view"},
                                      std::string_view{"sideways"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message ==
            "--temporal-view needs one of "
            "off|motion|reprojection|reprojected|rejection|weight|age, got 'sideways'");
}

//======================================================================================================================
TEST_CASE("--temporal-view needs a value", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal-view"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message == "--temporal-view needs a value: App --temporal-view "
                                      "<off|motion|reprojection|reprojected|rejection|weight|age>");
}

//======================================================================================================================
TEST_CASE("--temporal off combined with a non-off view is an error naming both flags",
          "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal"}, std::string_view{"off"},
                                      std::string_view{"--temporal-view"},
                                      std::string_view{"motion"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().message ==
            "--temporal off conflicts with --temporal-view motion: the temporal path must run to "
            "draw a diagnostic view");
}

//======================================================================================================================
TEST_CASE("--temporal off combined with --temporal-view off is not an error", "[app][options]") {
    constexpr std::array arguments = {std::string_view{"--temporal"}, std::string_view{"off"},
                                      std::string_view{"--temporal-view"}, std::string_view{"off"}};
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->temporal == TemporalMode::Off);
    REQUIRE(result->temporalView == render::TemporalDebugView::Off);
}

//======================================================================================================================
TEST_CASE("repeated --temporal and --temporal-view keep the last value", "[app][options]") {
    constexpr std::array arguments = {
        std::string_view{"--temporal"},      std::string_view{"raw"},
        std::string_view{"--temporal-view"}, std::string_view{"motion"},
        std::string_view{"--temporal"},      std::string_view{"taa"},
        std::string_view{"--temporal-view"}, std::string_view{"age"},
    };
    const AppOptionsResult result = parseAppOptions(arguments);

    REQUIRE(result);
    REQUIRE(result->temporal == TemporalMode::Taa);
    REQUIRE(result->temporalView == render::TemporalDebugView::HistoryAge);
}

//======================================================================================================================
// The CLI text is generated from the catalog (Source/App/AppOptions.cpp's sceneIdList), not a
// second hardcoded list -- this pins the catalog's own order/content so the two cannot drift.
TEST_CASE("the scene catalog's stable IDs match what the CLI advertises", "[app][options]") {
    const std::array<std::string_view, 5> expected = {"sponza", "damaged-helmet", "milk-truck",
                                                      "material-lab", "temporal-lab"};
    const std::span<const std::string_view> ids = lmx::engine::sceneStableIds();

    REQUIRE(ids.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(ids[i] == expected[i]);
    }
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
