//----------------------------------------------------------------------------------------------------------------------
/// @file AppActivityModelTests.cpp
/// @brief Verifies activity priority, measurement progress, and controller visibility duration.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Workspace/ActivityModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("idle editor has no current activity", "[app][activity]") {
    REQUIRE_FALSE(currentActivity(ActivityInputs{}));
}

//======================================================================================================================
TEST_CASE("measurement warmup takes priority and reports frame progress", "[app][activity]") {
    ActivityInputs inputs{.measure = MeasureProgress{MeasurementState::Warmup, 8, 32},
                          .capturePending = true,
                          .documentWork = "Loading scene",
                          .controller = ScaleChange{1.0f, 0.75f, 10.0},
                          .now = 10.5};
    const auto activity = currentActivity(inputs);
    REQUIRE(activity);
    REQUIRE(activity->actor == Actor::Operator);
    REQUIRE(activity->verb == "Warmup");
    REQUIRE(activity->progress);
    REQUIRE(*activity->progress == Catch::Approx(0.25f));
    REQUIRE(activity->stoppable);
    REQUIRE(activity->tooltip.find("Measurement") != std::string::npos);
    REQUIRE(activity->tooltip.find("8 / 32") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("measuring progress includes both phase endpoints", "[app][activity]") {
    for (const auto done : {0u, 9u, 10u}) {
        const auto activity = currentActivity(
            ActivityInputs{.measure = MeasureProgress{MeasurementState::Measuring, done, 10}});
        REQUIRE(activity);
        REQUIRE(activity->verb == "Measuring");
        REQUIRE(activity->progress);
        REQUIRE(*activity->progress == Catch::Approx(static_cast<float>(done) / 10.0f));
        REQUIRE(activity->stoppable);
        REQUIRE(activity->tooltip.find(std::to_string(done) + " / 10") != std::string::npos);
    }
}

//======================================================================================================================
TEST_CASE("measurement draining stays finishing without invented retirement progress",
          "[app][activity]") {
    const auto activity = currentActivity(
        ActivityInputs{.measure = MeasureProgress{MeasurementState::Draining, 10, 10}});
    REQUIRE(activity);
    REQUIRE(activity->verb == "Finishing");
    REQUIRE_FALSE(activity->progress);
    REQUIRE(activity->stoppable);
    REQUIRE(activity->tooltip.find("retirement") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("zero planned measurement frames have no numeric progress", "[app][activity]") {
    const auto activity =
        currentActivity(ActivityInputs{.measure = MeasureProgress{MeasurementState::Warmup, 0, 0}});
    REQUIRE(activity);
    REQUIRE_FALSE(activity->progress);
    REQUIRE(activity->stoppable);
}

//======================================================================================================================
TEST_CASE("terminal measurement states do not invent an active run", "[app][activity]") {
    for (const auto phase :
         {MeasurementState::Idle, MeasurementState::Complete, MeasurementState::Cancelled}) {
        ActivityInputs inputs{.measure = MeasureProgress{phase, 10, 10}};
        REQUIRE_FALSE(currentActivity(inputs));
        inputs.capturePending = true;
        const auto capture = currentActivity(inputs);
        REQUIRE(capture);
        REQUIRE(capture->verb == "Capturing");
        REQUIRE_FALSE(capture->stoppable);
    }
}

//======================================================================================================================
TEST_CASE("pending capture takes priority over document work and controller changes",
          "[app][activity]") {
    const auto activity = currentActivity(ActivityInputs{.capturePending = true,
                                                         .documentWork = "Loading scene",
                                                         .controller = ScaleChange{1, 0.75f, 10},
                                                         .now = 10.5});
    REQUIRE(activity);
    REQUIRE(activity->actor == Actor::Operator);
    REQUIRE(activity->verb == "Capturing");
    REQUIRE_FALSE(activity->progress);
    REQUIRE_FALSE(activity->stoppable);
    REQUIRE(activity->tooltip.find("capture") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document work preserves its source and takes priority over the controller",
          "[app][activity]") {
    ActivityInputs inputs{.documentWork = "Loading Assets/Scenes/sponza.scene.gltf",
                          .controller = ScaleChange{1, 0.75f, 10},
                          .now = 10.5};
    const auto activity = currentActivity(inputs);
    REQUIRE(activity);
    REQUIRE(activity->actor == Actor::System);
    REQUIRE(activity->verb == *inputs.documentWork);
    REQUIRE_FALSE(activity->progress);
    REQUIRE_FALSE(activity->stoppable);
    inputs.documentWork->clear();
    REQUIRE(activity->verb == "Loading Assets/Scenes/sponza.scene.gltf");
    REQUIRE(activity->tooltip.find("Assets/Scenes/sponza.scene.gltf") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("controller activity lasts two seconds and names both scales", "[app][activity]") {
    ActivityInputs inputs{.controller = ScaleChange{1, 0.75f, 10}, .now = 10};
    for (const double now : {10.0, 10.5, 11.999}) {
        inputs.now = now;
        const auto activity = currentActivity(inputs);
        REQUIRE(activity);
        REQUIRE(activity->actor == Actor::System);
        REQUIRE_FALSE(activity->progress);
        REQUIRE_FALSE(activity->stoppable);
        REQUIRE(activity->tooltip.find("Dynamic resolution") != std::string::npos);
        REQUIRE(activity->tooltip.find("1.000") != std::string::npos);
        REQUIRE(activity->tooltip.find("0.750") != std::string::npos);
    }
    for (const double now : {9.999, 12.0, 12.001, 20.0}) {
        inputs.now = now;
        REQUIRE_FALSE(currentActivity(inputs));
    }
}

//======================================================================================================================
TEST_CASE("activity priority can fall through all four existing sources", "[app][activity]") {
    ActivityInputs inputs{.measure = MeasureProgress{MeasurementState::Measuring, 1, 4},
                          .capturePending = true,
                          .documentWork = "Saving scene",
                          .controller = ScaleChange{1, 0.75f, 10},
                          .now = 11};
    REQUIRE(currentActivity(inputs)->stoppable);
    inputs.measure.reset();
    REQUIRE(currentActivity(inputs)->verb == "Capturing");
    REQUIRE_FALSE(currentActivity(inputs)->stoppable);
    inputs.capturePending = false;
    REQUIRE(currentActivity(inputs)->verb == "Saving scene");
    REQUIRE_FALSE(currentActivity(inputs)->stoppable);
    inputs.documentWork.reset();
    REQUIRE(currentActivity(inputs)->actor == Actor::System);
    REQUIRE_FALSE(currentActivity(inputs)->stoppable);
    inputs.now = 12;
    REQUIRE_FALSE(currentActivity(inputs));
}
