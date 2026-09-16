//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementRunTests.cpp
/// @brief Tests deterministic measurement plans, strict timing joins and scored refusal.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/AppOptions.h"
#include "App/Model/MeasurementRun.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace lmx::app;
namespace {

//======================================================================================================================
MeasurementProvenance testProvenance() {
    return {.device = "gpu",
            .os = "os",
            .buildMode = "release",
            .executableHash = std::string(64, 'a'),
            .shaderHashes = {{"scene", std::string(64, 'b')}}};
}
} // namespace

//======================================================================================================================
TEST_CASE("Measurement plans drain only after every exact frame retires", "[app][measurement]") {
    MeasurementRun run;
    REQUIRE(run.state() == MeasurementState::Idle);
    REQUIRE(run.start({.warmupFrames = 1, .measuredFrames = 2}, testProvenance()));
    REQUIRE(run.state() == MeasurementState::Warmup);
    REQUIRE_FALSE(run.nextFrame()->ordinal);
    REQUIRE(run.recordCpu({.frameId = 8, .sequenceFrame = 0}));
    REQUIRE(run.nextFrame()->ordinal == 0);
    REQUIRE(run.recordCpu({.frameId = 9, .sequenceFrame = 1}));
    REQUIRE(run.recordCpu({.frameId = 10, .sequenceFrame = 2}));
    REQUIRE(run.state() == MeasurementState::Draining);
    REQUIRE_FALSE(run.nextFrame());
    const std::array<lmx::rhi::PassTiming, 1> timings = {{{"scene", 1.5}}};
    REQUIRE(run.retire(10, timings));
    REQUIRE(run.state() == MeasurementState::Draining);
    REQUIRE(run.retire(10, timings));
    REQUIRE(run.retire(9, timings));
    REQUIRE(run.state() == MeasurementState::Complete);
    REQUIRE(run.finishDrain());
    REQUIRE(run.samples().size() == 2);
    REQUIRE(run.json().find("\"schemaVersion\":2") != std::string::npos);
    REQUIRE(run.json().find("\"scored\":true") != std::string::npos);
    REQUIRE(run.json().find("serialized-retirement") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("Missing or contradictory measurement evidence invalidates a run", "[app][measurement]") {
    MeasurementRun run;
    REQUIRE(run.start({.warmupFrames = 0, .measuredFrames = 2}, testProvenance()));
    REQUIRE(run.recordCpu({.frameId = 1, .sequenceFrame = 0}));
    SECTION("missing retirement") {
        REQUIRE(run.recordCpu({.frameId = 2, .sequenceFrame = 1}));
        REQUIRE_FALSE(run.finishDrain());
    }
    SECTION("wrong sequence") {
        REQUIRE_FALSE(run.recordCpu({.frameId = 2, .sequenceFrame = 9}));
    }
    SECTION("skipped CPU frame") {
        REQUIRE_FALSE(run.recordCpu({.frameId = 3, .sequenceFrame = 1}));
    }
    SECTION("future GPU frame") {
        const std::array<lmx::rhi::PassTiming, 1> future = {{{"scene", 1}}};
        REQUIRE_FALSE(run.retire(2, future));
    }
    SECTION("duplicate CPU frame") {
        REQUIRE_FALSE(run.recordCpu({.frameId = 1, .sequenceFrame = 1}));
    }
    SECTION("empty GPU timing") {
        REQUIRE_FALSE(run.retire(1, {}));
    }
    SECTION("conflicting GPU duplicate") {
        const std::array<lmx::rhi::PassTiming, 1> first = {{{"scene", 1}}};
        const std::array<lmx::rhi::PassTiming, 1> second = {{{"scene", 2}}};
        REQUIRE(run.retire(1, first));
        REQUIRE_FALSE(run.retire(1, second));
    }
    SECTION("nonfinite CPU timing") {
        REQUIRE_FALSE(run.recordCpu({.frameId = 2,
                                     .sequenceFrame = 1,
                                     .encodeMs = std::numeric_limits<double>::infinity()}));
    }
    REQUIRE(run.state() == MeasurementState::Cancelled);
    REQUIRE_FALSE(run.failure().empty());
    REQUIRE(run.json().find("\"scored\":false") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("Declared pass inventory is required at retirement", "[app][measurement]") {
    MeasurementRun run;
    REQUIRE(run.start({.warmupFrames = 0, .measuredFrames = 1}, testProvenance()));
    REQUIRE(run.recordCpu({.frameId = 1, .expectedPasses = {"shadow", "scene"}}));
    const std::array<lmx::rhi::PassTiming, 1> missing = {{{"scene", 1}}};
    REQUIRE_FALSE(run.retire(1, missing));
    REQUIRE(run.state() == MeasurementState::Cancelled);
}

//======================================================================================================================
TEST_CASE("Measurement instrumentation and interactive scoring are explicit",
          "[app][measurement]") {
    MeasurementProvenance provenance;
    provenance.environment = {{"MTL_DEBUG_LAYER", "0"}, {"MTL_CAPTURE_ENABLED", ""}};
    REQUIRE_FALSE(measurementEnvironmentInstrumented(provenance));
    provenance.environment[0].second = "1";
    REQUIRE(measurementEnvironmentInstrumented(provenance));
    MeasurementRun run;
    REQUIRE_FALSE(run.start({}, provenance));
    REQUIRE(run.start({.interactive = true}, provenance));
    run.cancel();
    REQUIRE(run.start({.unscored = true}, provenance));
    REQUIRE_FALSE(run.start({}, testProvenance()));
    run.cancel("scene changed");
    run.cancel("later failure");
    REQUIRE(run.failure() == "scene changed");
}

//======================================================================================================================
TEST_CASE("Visibility and measurement options share all run modes", "[app][options][measurement]") {
    const std::array<std::string_view, 16> args = {
        "--measure",       "/tmp/run.json", "--scene",          "visibility-lab",
        "--lab-instances", "16384",         "--visibility",     "off",
        "--submission",    "batched",       "--warmup",         "32",
        "--frames",        "256",           "--measure-camera", "initial"};
    const auto parsed = parseAppOptions(args);
    REQUIRE(parsed);
    REQUIRE(parsed->mode == RunMode::Measure);
    REQUIRE_FALSE(parsed->visibilityEnabled);
    REQUIRE(parsed->submission == lmx::render::SubmissionMode::Batched);
    REQUIRE(parsed->labInstances == 16384);
    REQUIRE_FALSE(parsed->measurementTrack);
    const std::array<std::string_view, 4> conflict = {"--measure", "run.json", "--screenshot",
                                                      "out.png"};
    REQUIRE_FALSE(parseAppOptions(conflict));
    const std::array<std::string_view, 2> wrongScene = {"--lab-instances", "1024"};
    REQUIRE_FALSE(parseAppOptions(wrongScene));
    const std::array<std::string_view, 1> unscored = {"--unscored"};
    REQUIRE_FALSE(parseAppOptions(unscored));
    const std::array<std::string_view, 2> badMode = {"--submission", "icb"};
    REQUIRE_FALSE(parseAppOptions(badMode));
}

//======================================================================================================================
TEST_CASE("Scored measurement requires real runtime provenance", "[app][measurement]") {
    MeasurementRun run;
    REQUIRE_FALSE(run.start({}, {}));
    REQUIRE(run.failure().find("provenance") != std::string::npos);
    auto provenance = testProvenance();
    provenance.shaderHashes.front().second = "unavailable";
    REQUIRE_FALSE(run.start({}, provenance));
    REQUIRE(run.start({.unscored = true}, provenance));
}
