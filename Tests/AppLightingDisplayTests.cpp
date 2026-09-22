//----------------------------------------------------------------------------------------------------------------------
/// @file AppLightingDisplayTests.cpp
/// @brief Tests lighting publication cadence, retirement context and exact-frame timing joins.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Rendering/Lighting/LightingDiagnostics.h"
#include "App/Model/Rendering/Lighting/LightingDisplay.h"
#include <catch2/catch_test_macros.hpp>

using namespace lmx;

//======================================================================================================================
TEST_CASE("LightingDisplay keeps counters and timings coherent while warnings stay immediate",
          "[app][lighting-display]") {
    app::LightingDisplay display;
    render::LightingStatus status{.requested = engine::LocalLightMode::Clustered,
                                  .effective = engine::LocalLightMode::Clustered,
                                  .frameNumber = 7,
                                  .sceneGeneration = 2,
                                  .liveLightCount = 256};
    display.observe(status);
    display.publishReadings(0.0);
    REQUIRE_FALSE(display.readingsStatus().isRetired);
    status.isRetired = true;
    status.counters = {.candidates = 10, .assigned = 10};
    const std::vector<rojoRHI::PassTiming> timings{{"lmx.pass.light.count", 1.5}};
    display.observeTimings(7, timings);
    display.retire(status);
    display.publishReadings(0.01);
    REQUIRE(display.readingsStatus().frameNumber == 7);
    REQUIRE(display.readingsTimings().size() == 1);
    REQUIRE(display.readingsTimings()[0].gpuMilliseconds == 1.5);
    status.frameNumber = 8;
    status.counters.droppedPerCluster = 2;
    status.counters.assigned = 8;
    status.counters.truncatedFroxels = 1;
    display.retire(status);
    display.publishReadings(0.1);
    REQUIRE(display.status().counters.truncatedFroxels == 1);
    REQUIRE(display.readingsStatus().frameNumber == 7);
    display.publishReadings(0.26);
    REQUIRE(display.readingsStatus().frameNumber == 8);
    REQUIRE(display.readingsTimings().empty());
    status.sceneGeneration = 3;
    status.frameNumber = 9;
    status.isRetired = false;
    status.counters = {};
    display.observe(status);
    display.publishReadings(0.27);
    REQUIRE(display.readingsStatus().sceneGeneration == 3);
    REQUIRE_FALSE(display.readingsStatus().isRetired);
    auto stale = status;
    stale.sceneGeneration = 2;
    stale.isRetired = true;
    display.retire(stale);
    REQUIRE_FALSE(display.status().isRetired);
}

//======================================================================================================================
TEST_CASE("LightingDisplay mode and check changes cannot expose stale warnings",
          "[app][lighting-display]") {
    app::LightingDisplay display;
    render::LightingStatus status{.requested = engine::LocalLightMode::Clustered,
                                  .effective = engine::LocalLightMode::Clustered,
                                  .frameNumber = 1,
                                  .sceneGeneration = 1,
                                  .checkEnabled = true};
    display.observe(status);
    status.isRetired = true;
    status.check.indexMismatches = 1;
    display.retire(status);
    display.publishReadings(0.0);
    REQUIRE_FALSE(app::lightingFailure(display.status()).empty());
    status.frameNumber = 2;
    status.checkEnabled = false;
    status.check = {};
    status.isRetired = false;
    display.observe(status);
    display.publishReadings(0.001);
    REQUIRE_FALSE(display.readingsStatus().checkEnabled);
    REQUIRE_FALSE(display.readingsStatus().isRetired);
    status.requested = status.effective = engine::LocalLightMode::Off;
    display.observe(status);
    display.publishReadings(0.002);
    REQUIRE(display.readingsStatus().effective == engine::LocalLightMode::Off);
    display.clear();
    REQUIRE(display.readingsTimings().empty());
}
