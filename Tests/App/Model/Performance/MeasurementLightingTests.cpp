//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementLightingTests.cpp
/// @brief Pins exact lighting retirement, retained failures, and separate GPU measurement scopes.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Performance/MeasurementRun.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace lmx;
namespace {
//======================================================================================================================
app::MeasurementPlan lightingPlan(uint32_t frames = 1) {
    app::MeasurementPlan plan;
    plan.warmupFrames = 0;
    plan.measuredFrames = frames;
    plan.scene = "light-lab";
    plan.localLightMode = "clustered";
    plan.labLights = 64;
    plan.unscored = true;
    return plan;
}
//======================================================================================================================
app::MeasurementCpuSample lightingSample(uint64_t frame, uint32_t sequence) {
    app::MeasurementCpuSample sample;
    sample.frameId = frame;
    sample.sequenceFrame = sequence;
    sample.lighting = {.requested = engine::LocalLightMode::Clustered,
                       .effective = engine::LocalLightMode::Clustered,
                       .frameNumber = frame,
                       .sceneGeneration = 7,
                       .liveLightCount = 64};
    return sample;
}
//======================================================================================================================
render::LightingStatus retiredLighting(uint64_t frame) {
    auto status = lightingSample(frame, 0).lighting;
    status.isRetired = true;
    status.counters = {.candidates = 7, .assigned = 7, .maxCount = 7};
    status.listBytes = 28;
    status.allocatedListBytes = 3 * render::kLightClusterIndexCapacity * sizeof(uint32_t);
    return status;
}
const std::array<rojoRHI::PassTiming, 3> kLightingTimings = {
    {{"lmx.pass.light.count", 0.25}, {"lmx.pass.light.fill", 0.125}, {"lmx.pass.scene", 2.0}}};
} // namespace

//======================================================================================================================
TEST_CASE("Measurements join lighting by exact frame independently of timing",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    REQUIRE(run.start(lightingPlan(2), {}));
    REQUIRE(run.recordCpu(lightingSample(10, 0)));
    REQUIRE(run.recordCpu(lightingSample(11, 1)));
    REQUIRE(run.retire(11, kLightingTimings));
    REQUIRE(run.retire(10, kLightingTimings));
    CHECK(run.state() == app::MeasurementState::Draining);
    REQUIRE(run.retireLighting(retiredLighting(11)));
    REQUIRE(run.retireLighting(retiredLighting(11)));
    CHECK(run.state() == app::MeasurementState::Draining);
    REQUIRE(run.retireLighting(retiredLighting(10)));
    CHECK(run.state() == app::MeasurementState::Complete);
    REQUIRE(run.samples()[0].lighting);
    CHECK(run.samples()[0].lighting->frameNumber == 10);
    CHECK(run.samples()[0].lighting->listBytes == 28);
    const auto json = run.json();
    CHECK(json.contains("\"schemaVersion\":4"));
    CHECK(json.contains("\"lightingGpuMs\":0.375"));
    CHECK(json.contains("\"sceneGpuMs\":2"));
    CHECK(json.contains("\"gpuSumMs\":2.375"));
    CHECK(json.contains("\"localLightMode\":\"clustered\""));
    CHECK(json.contains("\"labLights\":64"));
    CHECK(json.contains("\"lighting\":{\"frameId\":10"));
}

//======================================================================================================================
TEST_CASE("Contradictory lighting publications cancel and preserve partial evidence",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    REQUIRE(run.start(lightingPlan(2), {}));
    REQUIRE(run.recordCpu(lightingSample(10, 0)));
    auto status = retiredLighting(10);
    SECTION("future frame") {
        status.frameNumber = 11;
    }
    SECTION("wrong activation") {
        ++status.sceneGeneration;
    }
    SECTION("wrong live light count") {
        --status.liveLightCount;
    }
    SECTION("wrong requested mode") {
        status.requested = engine::LocalLightMode::Direct;
    }
    SECTION("wrong effective mode") {
        status.effective = engine::LocalLightMode::Direct;
    }
    SECTION("pending GPU counters") {
        status.isRetired = false;
    }
    SECTION("wrong prefix bytes") {
        ++status.listBytes;
    }
    SECTION("invalid counter partition") {
        ++status.counters.assigned;
    }
    SECTION("unexpected check") {
        status.checkEnabled = true;
    }
    SECTION("conflicting duplicate") {
        REQUIRE(run.retireLighting(status));
        ++status.counters.candidates;
        ++status.counters.droppedPerCluster;
    }
    REQUIRE_FALSE(run.retireLighting(status));
    CHECK(run.state() == app::MeasurementState::Cancelled);
    CHECK_FALSE(run.failure().empty());
    CHECK(run.json().contains("\"complete\":false"));
}

//======================================================================================================================
TEST_CASE("Lighting retirement is required for zero-light frames in every requested mode",
          "[app][measurement][lighting-measurement]") {
    REQUIRE(app::MeasurementPlan{}.localLightMode == "clustered");
    for (const auto mode : {engine::LocalLightMode::Off, engine::LocalLightMode::Direct,
                            engine::LocalLightMode::Clustered}) {
        app::MeasurementRun run;
        auto plan = lightingPlan();
        plan.scene = "sponza";
        plan.localLightMode = mode == engine::LocalLightMode::Off      ? "off"
                              : mode == engine::LocalLightMode::Direct ? "direct"
                                                                       : "clustered";
        REQUIRE(run.start(plan, {}));
        auto sample = lightingSample(4, 0);
        sample.lighting.requested = mode;
        sample.lighting.effective = engine::LocalLightMode::Off;
        sample.lighting.liveLightCount = 0;
        REQUIRE(run.recordCpu(sample));
        const std::array<rojoRHI::PassTiming, 1> timing = {{{"lmx.pass.scene", 1}}};
        REQUIRE(run.retire(4, timing));
        CHECK(run.state() == app::MeasurementState::Draining);
        auto status = sample.lighting;
        status.isRetired = true;
        REQUIRE(run.retireLighting(status));
        CHECK(run.finishDrain());
        REQUIRE(run.samples()[0].lighting->effective == engine::LocalLightMode::Off);
    }
}

//======================================================================================================================
TEST_CASE("Missing lighting retirement cannot complete a measurement",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    REQUIRE(run.start(lightingPlan(), {}));
    REQUIRE(run.recordCpu(lightingSample(1, 0)));
    REQUIRE(run.retire(1, kLightingTimings));
    REQUIRE_FALSE(run.finishDrain());
    CHECK(run.json().contains("\"lighting\":null"));
}

//======================================================================================================================
TEST_CASE("Overflow remains a measurement diagnostic and checks preserve failed counters",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    auto plan = lightingPlan();
    auto sample = lightingSample(1, 0);
    auto status = retiredLighting(1);
    bool checkFailure = false;
    SECTION("bounded overflow is reported") {
        status.counters.candidates = 10;
        status.counters.droppedPerCluster = 3;
        status.counters.truncatedFroxels = 1;
    }
    SECTION("unscored failed check remains failed") {
        plan.lightCheck = true;
        sample.lighting.checkEnabled = true;
        status.checkEnabled = true;
        status.check.indexMismatches = 1;
        checkFailure = true;
    }
    REQUIRE(run.start(plan, {}));
    REQUIRE(run.recordCpu(sample));
    REQUIRE(run.retireLighting(status));
    REQUIRE(run.retire(1, kLightingTimings));
    CHECK(run.finishDrain() == !checkFailure);
    REQUIRE(run.samples()[0].lighting);
    CHECK(run.samples()[0].lighting->counters.candidates == status.counters.candidates);
    CHECK(run.samples()[0].lighting->check.indexMismatches == status.check.indexMismatches);
}

//======================================================================================================================
TEST_CASE("Invalid lighting measurement plans fail before submission",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    auto plan = lightingPlan();
    SECTION("unknown mode") {
        plan.localLightMode = "automatic";
    }
    SECTION("direct check") {
        plan.localLightMode = "direct";
        plan.lightCheck = true;
    }
    SECTION("direct diagnostic") {
        plan.localLightMode = "direct";
        plan.lightDebugView = "missed";
    }
    SECTION("unknown diagnostic") {
        plan.lightDebugView = "invalid";
    }
    SECTION("invalid lab size") {
        plan.labLights = 0;
    }
    SECTION("pile exceeds hard cap") {
        plan.labLights = 4096;
        plan.labLightPile = 1;
    }
    SECTION("rig in another scene") {
        plan.localLightRig = true;
    }
    REQUIRE_FALSE(run.start(plan, {}));
}

//======================================================================================================================
TEST_CASE("Measurement reports do not retain raw light-check frame ownership",
          "[app][measurement][lighting-measurement]") {
    app::MeasurementRun run;
    auto plan = lightingPlan();
    plan.lightCheck = true;
    REQUIRE(run.start(plan, {}));
    std::weak_ptr<const render::LightClusterCheckFrame> declarationEvidence;
    {
        auto sample = lightingSample(1, 0);
        auto evidence = std::make_shared<render::LightClusterCheckFrame>();
        evidence->frameNumber = 1;
        evidence->cpu.grid.resize(render::kClusterCount);
        evidence->cpu.indices.resize(render::kLightClusterIndexCapacity);
        sample.lighting.checkEnabled = true;
        sample.lighting.checkFrame = evidence;
        declarationEvidence = evidence;
        REQUIRE(run.recordCpu(sample));
    }
    CHECK(declarationEvidence.expired());
    CHECK_FALSE(run.samples()[0].cpu.lighting.checkFrame);
    std::weak_ptr<const render::LightClusterCheckFrame> retiredEvidence;
    {
        auto status = retiredLighting(1);
        auto evidence = std::make_shared<render::LightClusterCheckFrame>();
        evidence->frameNumber = 1;
        evidence->gpu.grid.resize(render::kClusterCount);
        evidence->gpu.indices.resize(7);
        status.checkEnabled = true;
        status.checkFrame = evidence;
        retiredEvidence = evidence;
        REQUIRE(run.retireLighting(status));
    }
    CHECK(retiredEvidence.expired());
    REQUIRE(run.samples()[0].lighting);
    CHECK_FALSE(run.samples()[0].lighting->checkFrame);
    CHECK(run.samples()[0].lighting->counters.assigned == 7);
    REQUIRE(run.retire(1, kLightingTimings));
    REQUIRE(run.finishDrain());
    CHECK(declarationEvidence.expired());
    CHECK(retiredEvidence.expired());
    CHECK(run.json().contains("\"checkEnabled\":true"));
}

//======================================================================================================================
TEST_CASE("Interactive LightLab measurement allows disabled authored lights",
          "[app][measurement][lighting-measurement]") {
    auto plan = lightingPlan();
    plan.interactive = true;
    plan.unscored = true;
    app::MeasurementRun run;
    REQUIRE(run.start(plan, {}));
    auto sample = lightingSample(4, 0);
    sample.lighting.liveLightCount = 0;
    sample.lighting.effective = engine::LocalLightMode::Off;
    REQUIRE(run.recordCpu(sample));
}

//======================================================================================================================
TEST_CASE("Interactive lighting measurement freezes the initially enabled population",
          "[app][measurement][lighting-measurement]") {
    for (const auto scene : {"light-lab", "sponza"}) {
        auto plan = lightingPlan(2);
        plan.scene = scene;
        plan.interactive = true;
        plan.unscored = true;
        plan.localLightRig = plan.scene == "sponza";
        app::MeasurementRun run;
        REQUIRE(run.start(plan, {}));
        auto first = lightingSample(4, 0);
        first.lighting.liveLightCount = plan.localLightRig ? 15 : 63;
        REQUIRE(run.recordCpu(first));
        auto changed = lightingSample(5, 1);
        changed.lighting.liveLightCount = first.lighting.liveLightCount - 1;
        REQUIRE_FALSE(run.recordCpu(changed));
        REQUIRE(run.state() == app::MeasurementState::Cancelled);
    }
}
