//----------------------------------------------------------------------------------------------------------------------
/// @file AppGpuVisibilityTests.cpp
/// @brief Pins delayed visibility identity, exact measurement joins and CLI/capture refusal.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/CaptureMetadata.h"
#include "App/Model/Options/AppOptions.h"
#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/VisibilityDiagnostics.h"
#include "App/Model/VisibilityDisplay.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace lmx;
namespace {
//======================================================================================================================
render::VisibilityStatus retiredStatus(uint64_t frame = 10) {
    render::VisibilityStatus status;
    status.classifyMode = render::ClassifyMode::Gpu;
    status.isRetired = true;
    status.frameNumber = frame;
    status.sceneGeneration = 7;
    status.sceneCounters = {
        .candidates = 2, .visible = 1, .rejected = 1, .emittedRows = 1, .emittedCommands = 1};
    status.shadowCounters = {
        .candidates = 2, .bypassed = {0, 0, 2, 0, 0}, .emittedRows = 2, .emittedCommands = 2};
    status.submission.listBytes = 12;
    status.submission.allocatedListBytes = 128;
    return status;
}
//======================================================================================================================
app::MeasurementProvenance provenance() {
    return {.device = "GPU",
            .os = "OS",
            .buildMode = "release",
            .executableHash = std::string(64, 'a'),
            .shaderHashes = {{"shader", std::string(64, 'b')}}};
}
} // namespace

//======================================================================================================================
TEST_CASE("GPU classifier CLI defaults and incompatible frontends are explicit", "[app][options]") {
    const auto defaults = app::parseAppOptions({});
    REQUIRE(defaults);
    REQUIRE(defaults->classifyMode == render::ClassifyMode::Cpu);
    REQUIRE_FALSE(defaults->classifyCheck);
    const std::array<std::string_view, 7> valid = {
        "--classify", "gpu", "--classify-check", "--measure", "run.json", "--unscored", "--"};
    REQUIRE(app::parseAppOptions(valid));
    const std::vector<std::vector<std::string_view>> invalid = {
        {"--classify"},
        {"--classify", "unknown"},
        {"--classify-check"},
        {"--classify", "gpu", "--submission", "direct"},
        {"--classify", "gpu", "--classify-check", "--measure", "run.json"},
        {"--classify", "gpu", "--submission", "icb"},
        {"--classify", "gpu", "--classify-check", "--classify", "cpu"}};
    for (const auto& args : invalid) {
        CAPTURE(args);
        REQUIRE_FALSE(app::parseAppOptions(args));
    }
    for (std::string_view frontend : {"--screenshot", "--capture-sequence"}) {
        const std::array<std::string_view, 7> args = {frontend, "capture.png",      "--classify",
                                                      "gpu",    "--classify-check", "--submission",
                                                      "batched"};
        const auto parsed = app::parseAppOptions(args);
        REQUIRE(parsed);
        REQUIRE(parsed->classifyMode == render::ClassifyMode::Gpu);
        REQUIRE(parsed->classifyCheck);
    }
}

//======================================================================================================================
TEST_CASE("Delayed GPU display never attributes old rows to replacement identities",
          "[app][visibility]") {
    engine::Scene scene;
    engine::SceneObject object;
    object.id = {3, 2, 8};
    scene.objects.push_back(object);
    auto pending = retiredStatus();
    pending.isRetired = false;
    pending.scene.candidates.push_back({.instanceRow = 3});
    app::VisibilityDisplay display;
    display.observe(scene, pending);
    REQUIRE(display.find(object.id, display.status(), 7) == nullptr);
    scene.objects[0].id = {3, 3, 8};
    ++pending.frameNumber;
    display.observe(scene, pending);
    auto retired = retiredStatus();
    retired.scene.candidates.push_back(
        {.instanceRow = 3, .state = render::VisibilityState::Rejected});
    display.retire(retired);
    REQUIRE(display.find(object.id, display.status(), 7) != nullptr);
    REQUIRE(display.find(scene.objects[0].id, display.status(), 7) == nullptr);
    REQUIRE(display.find(object.id, display.status(), 8) == nullptr);
    const std::array<rojoRHI::PassTiming, 1> timing = {{{"lmx.pass.visibility.classify", 0.25}}};
    display.observeTimings(9, timing);
    REQUIRE(display.timings().empty());
    display.observeTimings(10, timing);
    REQUIRE(display.timings().size() == 1);
    const auto fields = app::visibilityFields(display.status(), display.timings());
    REQUIRE(std::ranges::any_of(
        fields, [](const auto& f) { return f.label == "Retired frame" && f.value == "10"; }));
    REQUIRE(std::ranges::any_of(fields, [](const auto& f) {
        return f.label == "lmx.pass.visibility.classify" && f.value == "0.250 ms";
    }));
    REQUIRE(std::ranges::any_of(fields, [](const auto& f) {
        return f.label == "Valid row / argument payload" && f.value == "12 / 0 B";
    }));
    REQUIRE(std::ranges::any_of(fields, [](const auto& f) {
        return f.label == "List / argument allocation" && f.value == "128 / 0 B";
    }));
    display.clear();
    display.retire(retired);
    REQUIRE(display.status().frameNumber == 0);
}

//======================================================================================================================
TEST_CASE("GPU measurement needs independent exact visibility and timing retirement",
          "[app][measurement]") {
    app::MeasurementRun run;
    REQUIRE(run.start({.warmupFrames = 0, .measuredFrames = 2, .classify = "gpu"}, provenance()));
    REQUIRE(run.retireVisibility(retiredStatus(8)));
    REQUIRE(run.recordCpu({.frameId = 10,
                           .sequenceFrame = 0,
                           .candidates = 2,
                           .reservedListBytes = 16,
                           .listBytes = 16,
                           .classifyMode = render::ClassifyMode::Gpu,
                           .lighting = {.frameNumber = 10}}));
    REQUIRE(run.recordCpu({.frameId = 11,
                           .sequenceFrame = 1,
                           .candidates = 2,
                           .reservedListBytes = 16,
                           .listBytes = 16,
                           .classifyMode = render::ClassifyMode::Gpu,
                           .lighting = {.frameNumber = 11}}));
    REQUIRE(run.retireVisibility(retiredStatus(9)));
    REQUIRE(run.retireLighting({.frameNumber = 10, .isRetired = true}));
    REQUIRE(run.retireLighting({.frameNumber = 11, .isRetired = true}));
    const std::array<rojoRHI::PassTiming, 1> timing = {{{"lmx.pass.visibility.classify", 0.25}}};
    REQUIRE(run.retire(11, timing));
    REQUIRE(run.retireVisibility(retiredStatus(10)));
    REQUIRE(run.retireVisibility(retiredStatus(10)));
    REQUIRE(run.retire(10, timing));
    REQUIRE(run.state() == app::MeasurementState::Draining);
    SECTION("last visibility result is drained without a new declaration") {
        REQUIRE(run.retireVisibility(retiredStatus(11)));
        REQUIRE(run.finishDrain());
        REQUIRE(run.json().contains("\"schemaVersion\":4"));
        REQUIRE(run.json().contains("\"visibilityGpuMs\":0.25"));
        REQUIRE(run.json().contains("\"effectiveClassify\":\"gpu\""));
        REQUIRE(run.samples()[0].cpu.visible == 0);
        REQUIRE(run.samples()[0].cpu.listBytes == 12);
        REQUIRE(run.samples()[0].cpu.reservedListBytes == 16);
        REQUIRE(run.json().contains("\"listBytes\":12,\"reservedListBytes\":16"));
        REQUIRE(run.samples()[0].visibility->sceneCounters.emittedRows == 1);
    }
    SECTION("missing final result fails") {
        REQUIRE_FALSE(run.finishDrain());
    }
    SECTION("overflow invalidates scoring") {
        auto status = retiredStatus(11);
        status.overflow = true;
        status.sceneCounters.overflowedRows = 1;
        REQUIRE_FALSE(run.retireVisibility(status));
        REQUIRE(run.failure().contains("overflow"));
    }
    SECTION("unrequested diagnostic mismatches also fail") {
        auto status = retiredStatus(11);
        status.rowMismatches = 1;
        REQUIRE_FALSE(run.retireVisibility(status));
    }
    SECTION("future visibility is not attached to the latest sample") {
        REQUIRE_FALSE(run.retireVisibility(retiredStatus(12)));
    }
}

//======================================================================================================================
TEST_CASE("Check diagnostics and vendor fallback cannot produce scored measurements",
          "[app][measurement]") {
    app::MeasurementRun run;
    REQUIRE_FALSE(run.start({.classify = "gpu", .classifyCheck = true}, provenance()));
    REQUIRE(run.start({.classify = "gpu", .classifyCheck = true, .unscored = true}, provenance()));
    run.cancel();
    REQUIRE(run.start({.warmupFrames = 0, .measuredFrames = 1}, provenance()));
    REQUIRE_FALSE(
        run.recordCpu({.frameId = 1, .vendorFallback = 1, .lighting = {.frameNumber = 1}}));
}

//======================================================================================================================
TEST_CASE("Capture diagnostics preserve retirement identity and reject invalid work",
          "[app][capture]") {
    auto status = retiredStatus();
    REQUIRE(app::visibilityFailure(status).empty());
    const auto json = app::visibilityDiagnosticsJson(status);
    REQUIRE(json.contains("\"frameId\":10"));
    REQUIRE(json.contains("\"bypassed\":[0,2,0,0]"));
    render::SceneView view;
    view.classifyMode = render::ClassifyMode::Gpu;
    view.classifyCheck = true;
    status.checkEnabled = true;
    const auto record =
        app::captureRecordJson(0, 0, {}, view, {}, "frame.png", app::TemporalMode::Taa, &status);
    REQUIRE(record.contains("\"classify\":\"gpu\""));
    REQUIRE(record.contains("\"classifyCheck\":true"));
    REQUIRE(record.contains("\"visibility\":{\"frameId\":10"));
    SECTION("pending") {
        status.isRetired = false;
        REQUIRE(app::visibilityDiagnosticsJson(status).contains("\"scene\":null"));
    }
    SECTION("overflow") {
        status.sceneCounters.overflowedCommands = 1;
    }
    SECTION("check mismatch") {
        status.argumentMismatches = 1;
    }
    SECTION("counter mismatch") {
        ++status.shadowCounters.emittedRows;
    }
    REQUIRE_FALSE(app::visibilityFailure(status).empty());
}
