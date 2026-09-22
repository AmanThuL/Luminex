//----------------------------------------------------------------------------------------------------------------------
/// @file AppOcclusionTests.cpp
/// @brief Pins occlusion CLI dependencies, retired diagnostics and measurement isolation.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Options/AppOptions.h"
#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/VisibilityDiagnostics.h"
#include "App/Model/VisibilityDisplay.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace lmx;

//======================================================================================================================
TEST_CASE("Occlusion options preserve defaults and enforce coherent GPU diagnostics",
          "[app][options][occlusion]") {
    const auto defaults = app::parseAppOptions({});
    REQUIRE(defaults);
    REQUIRE_FALSE(defaults->occlusionEnabled);
    REQUIRE_FALSE(defaults->occlusionCheck);
    REQUIRE(defaults->hzbDebugLevel == -1);
    REQUIRE(defaults->labOccluders == 0);
    const std::vector<std::string_view> valid{
        "--scene", "visibility-lab",    "--classify",  "gpu",        "--occlusion",
        "on",      "--occlusion-check", "--hzb-level", "2",          "--lab-occluders",
        "8",       "--measure",         "run.json",    "--unscored", "--temporal",
        "off"};
    const auto parsed = app::parseAppOptions(valid);
    REQUIRE(parsed);
    REQUIRE(parsed->occlusionEnabled);
    REQUIRE(parsed->occlusionCheck);
    REQUIRE(parsed->hzbDebugLevel == 2);
    REQUIRE(parsed->labOccluders == 8);
    const std::vector<std::vector<std::string_view>> invalid{
        {"--occlusion"},
        {"--occlusion", "maybe"},
        {"--occlusion", "on"},
        {"--classify", "gpu", "--occlusion", "on", "--visibility", "off"},
        {"--classify", "gpu", "--occlusion-check"},
        {"--classify", "gpu", "--occlusion", "on", "--occlusion-check", "--measure", "out.json"},
        {"--classify", "gpu", "--occlusion", "on", "--hzb-level", "0", "--measure", "out.json"},
        {"--hzb-level", "0"},
        {"--hzb-level", "-1"},
        {"--hzb-level", "31"},
        {"--classify", "gpu", "--occlusion", "on", "--hzb-level", "0", "--temporal-view", "motion"},
        {"--lab-occluders", "1"},
        {"--scene", "visibility-lab", "--lab-occluders", "1025"},
        {"--scene", "visibility-lab", "--lab-occluders", "-1"}};
    for (const auto& args : invalid) {
        CAPTURE(args);
        REQUIRE_FALSE(app::parseAppOptions(args));
    }
    const std::array<std::string_view, 2> off{"--occlusion", "off"};
    REQUIRE(app::parseAppOptions(off));
}

//======================================================================================================================
TEST_CASE("Occlusion diagnostics distinguish geometry recovery from strict visibility",
          "[app][occlusion]") {
    render::VisibilityStatus status;
    status.classifyMode = render::ClassifyMode::Gpu;
    status.isRetired = true;
    status.frameNumber = 4;
    status.occlusionEnabled = true;
    status.occlusionCheckEnabled = true;
    status.occlusionSourceFrame = 3;
    status.occlusionInvalidReason = render::OcclusionInvalidReason::None;
    status.pyramidBytes = 2048;
    status.sceneCounters = {
        .candidates = 2, .visible = 1, .rejected = 1, .emittedRows = 1, .emittedCommands = 1};
    status.sceneCounters.occluded = 1;
    status.sceneCounters.occlusionTested = 2;
    auto& check = status.occlusionCheck;
    check.enabled = true;
    check.falselyRejectedInstances = 1;
    check.falselyRejectedPixels = 15;
    check.maximumMissingStreak = 1;
    check.missing.push_back(
        {.instanceIdentity = 123, .instanceRow = 2, .visiblePixels = 15, .consecutiveFrames = 1});
    REQUIRE(app::visibilityFailure(status).empty());
    const auto json = app::visibilityDiagnosticsJson(status);
    REQUIRE(json.contains("\"occlusionSourceFrame\":3"));
    REQUIRE(json.contains("\"occlusionHistoryValid\":true"));
    REQUIRE(json.contains("\"pyramidBytes\":2048"));
    REQUIRE(json.contains("\"occluded\":1"));
    REQUIRE(json.contains("\"pixels\":15,\"streak\":1"));
    check.strict = true;
    REQUIRE(app::visibilityFailure(status).contains("occlusion reference failed"));
    check.strict = false;
    check.maximumMissingStreak = 2;
    REQUIRE_FALSE(app::visibilityFailure(status).empty());
    check.maximumMissingStreak = 0;
    check.falselyRejectedInstances = 0;
    check.invalidReferencePixels = 1;
    REQUIRE_FALSE(app::visibilityFailure(status).empty());
}

//======================================================================================================================
TEST_CASE("Selected occlusion fields preserve source coordinates and the rejection reason",
          "[app][occlusion]") {
    render::InstanceVisibility instance;
    instance.state = render::VisibilityState::Rejected;
    instance.reason = render::VisibilityReason::Occluded;
    instance.occlusion = {.rectangle = {10, 20, 50, 60},
                          .level = 3,
                          .zBox = 0.125f,
                          .outcome = render::OcclusionOutcome::Retained,
                          .occluded = true};
    const auto fields = app::objectVisibilityFields(&instance);
    REQUIRE(std::ranges::any_of(fields, [](const auto& field) {
        return field.label == "Reason" && field.value == "Occluded (previous-frame HZB)";
    }));
    REQUIRE(std::ranges::any_of(fields, [](const auto& field) {
        return field.label == "Source rectangle [min, max)" &&
               field.value == "(10, 20) to (50, 60)";
    }));
}

//======================================================================================================================
TEST_CASE("Measurement schema separates pyramid build cost and rejects scored reference checks",
          "[app][measurement][occlusion]") {
    app::MeasurementRun run;
    app::MeasurementPlan plan;
    plan.classify = "gpu";
    plan.occlusionEnabled = true;
    plan.occlusionCheck = true;
    REQUIRE_FALSE(run.start(plan, {}));
    plan.unscored = true;
    plan.warmupFrames = 0;
    plan.measuredFrames = 1;
    REQUIRE(run.start(plan, {}));
    REQUIRE(run.recordCpu(
        {.frameId = 1, .classifyMode = render::ClassifyMode::Gpu, .lighting = {.frameNumber = 1}}));
    render::VisibilityStatus status;
    status.classifyMode = render::ClassifyMode::Gpu;
    status.isRetired = true;
    status.frameNumber = 1;
    status.occlusionEnabled = true;
    status.occlusionCheckEnabled = true;
    status.occlusionCheck.enabled = true;
    REQUIRE(run.retireVisibility(status));
    const std::array<rojoRHI::PassTiming, 4> timings{{{"lmx.pass.visibility.classify", 0.25},
                                                      {"lmx.pass.hzb.level0", 0.5},
                                                      {"lmx.pass.hzb.publish", 0.0625},
                                                      {"lmx.pass.hzb.debug", 0.125}}};
    REQUIRE(run.retireLighting({.frameNumber = 1, .isRetired = true}));
    REQUIRE(run.retire(1, timings));
    REQUIRE(run.finishDrain());
    const auto json = run.json();
    REQUIRE(json.contains("\"schemaVersion\":4"));
    REQUIRE(json.contains("\"hzbGpuMs\":0.5625"));
    REQUIRE(json.contains("\"visibilityGpuMs\":0.25"));
    REQUIRE(json.contains("\"scored\":false"));
}

//======================================================================================================================
TEST_CASE("A failed occlusion reference retains the full measurement sequence",
          "[app][measurement][occlusion]") {
    app::MeasurementRun run;
    app::MeasurementPlan plan;
    plan.classify = "gpu";
    plan.occlusionEnabled = true;
    plan.occlusionCheck = true;
    plan.unscored = true;
    plan.warmupFrames = 0;
    plan.measuredFrames = 3;
    REQUIRE(run.start(plan, {}));
    const std::array<rojoRHI::PassTiming, 1> timings{{{"lmx.pass.visibility.classify", 0.25}}};
    for (uint32_t frame = 1; frame <= 3; ++frame) {
        REQUIRE(run.recordCpu({.frameId = frame,
                               .sequenceFrame = frame - 1,
                               .classifyMode = render::ClassifyMode::Gpu,
                               .lighting = {.frameNumber = frame}}));
        render::VisibilityStatus status;
        status.classifyMode = render::ClassifyMode::Gpu;
        status.isRetired = true;
        status.frameNumber = frame;
        status.occlusionEnabled = true;
        status.occlusionCheckEnabled = true;
        status.occlusionCheck.enabled = true;
        status.occlusionCheck.strict = true;
        status.occlusionCheck.falselyRejectedInstances = frame == 1 ? 1 : 0;
        REQUIRE(run.retireVisibility(status));
        REQUIRE(run.retireLighting({.frameNumber = frame, .isRetired = true}));
        REQUIRE(run.retire(frame, timings));
        if (frame < 3)
            REQUIRE(run.active());
    }
    REQUIRE_FALSE(run.finishDrain());
    REQUIRE(run.samples().size() == 3);
    REQUIRE(run.samples()[2].retired);
    REQUIRE(run.failure().contains("frame 1"));
    REQUIRE(run.json().contains("\"complete\":false"));
}
