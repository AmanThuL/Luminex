#include "App/Model/Rendering/Lighting/LightingDiagnostics.h"
#include <catch2/catch_test_macros.hpp>

//======================================================================================================================
TEST_CASE("lighting reports preserve retirement identity and fail closed", "[light-check]") {
    lmx::render::LightingStatus status;
    REQUIRE(status.requested == lmx::engine::LocalLightMode::Clustered);
    REQUIRE(status.effective == lmx::engine::LocalLightMode::Off);
    REQUIRE(status.liveLightCount == 0);
    status.frameNumber = 42;
    status.sceneGeneration = 9;
    status.checkEnabled = true;
    REQUIRE_FALSE(status.checkPassed());
    REQUIRE_FALSE(lmx::app::lightingFailure(status).empty());
    REQUIRE(lmx::app::lightingDiagnosticsJson(status).find("\"counters\":null") !=
            std::string::npos);
    status.isRetired = true;
    REQUIRE(status.checkPassed());
    REQUIRE(lmx::app::lightingFailure(status).empty());
    const auto json = lmx::app::lightingDiagnosticsJson(status);
    REQUIRE(json.find("\"frameId\":42") != std::string::npos);
    REQUIRE(json.find("\"sceneGeneration\":9") != std::string::npos);
    status.counters.candidates = 4;
    status.counters.assigned = 3;
    REQUIRE_FALSE(lmx::app::lightingFailure(status).empty());
    status.counters.droppedPerCluster = 1;
    REQUIRE(lmx::app::lightingFailure(status).empty());
    status.check.gridMismatches = 1;
    REQUIRE_FALSE(lmx::app::lightingFailure(status).empty());
}
