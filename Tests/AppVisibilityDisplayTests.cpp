//----------------------------------------------------------------------------------------------------------------------
/// @file AppVisibilityDisplayTests.cpp
/// @brief Pins visibility formatting and scene/frame identity isolation.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/VisibilityDisplay.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lmx;
//======================================================================================================================
TEST_CASE("Visibility display rejects stale identities and frame mismatches", "[app][visibility]") {
    scene::Scene scene;
    scene::SceneObject object;
    object.id = {7, 2, 3};
    scene.objects.push_back(object);
    render::VisibilityStatus status;
    status.frameNumber = 12;
    status.sceneGeneration = 9;
    status.scene.candidates.push_back(
        {.instanceRow = 7, .state = render::VisibilityState::Rejected});
    app::VisibilityDisplay display;
    display.observe(scene, status);
    REQUIRE(display.find(object.id, status, 9) != nullptr);
    REQUIRE(display.find({7, 3, 3}, status, 9) == nullptr);
    REQUIRE(display.find({7, 2, 4}, status, 9) == nullptr);
    REQUIRE(display.find(object.id, status, 10) == nullptr);
    ++status.frameNumber;
    REQUIRE(display.find(object.id, status, 9) == nullptr);
    --status.frameNumber;
    display.clear();
    REQUIRE(display.find(object.id, status, 9) == nullptr);
}
//======================================================================================================================
TEST_CASE("Visibility fields retain bypass counts and world bounds", "[app][visibility]") {
    render::VisibilityStatus status;
    status.frameNumber = 8;
    status.scene.visible = 2;
    status.scene.rejected = 1;
    status.scene.candidates.resize(4);
    status.scene.bypassed[static_cast<size_t>(render::VisibilityReason::Disabled)] = 1;
    const auto fields = app::visibilityFields(status);
    REQUIRE(fields[0].value == "cpu");
    REQUIRE(fields[1].value == "8");
    REQUIRE(fields[2].value == "4 / 2 / 1");
    REQUIRE(fields[3].value == "1");
    render::InstanceVisibility object;
    object.state = render::VisibilityState::Bypassed;
    object.reason = render::VisibilityReason::UnreliableBounds;
    object.worldBounds = {{-1, -2, -3}, {4, 5, 6}};
    const auto details = app::objectVisibilityFields(&object);
    REQUIRE(details[0].value == "Bypassed");
    REQUIRE(details[1].value == "Unreliable bounds");
    REQUIRE(details[2].value == "-1.000, -2.000, -3.000");
    REQUIRE(app::objectVisibilityFields(nullptr).size() == 1);
    object.state = render::VisibilityState::Rejected;
    REQUIRE(app::objectVisibilityFields(&object)[1].value == "Outside camera frustum");
}

//======================================================================================================================
TEST_CASE("Selected object labels the matching declared or retired frame only",
          "[app][visibility]") {
    scene::Scene scene;
    scene::SceneObject object;
    object.id = {7, 2, 3};
    scene.objects.push_back(object);
    render::VisibilityStatus status;
    status.frameNumber = 12;
    status.sceneGeneration = 9;
    status.scene.candidates.push_back(
        {.instanceRow = 7, .state = render::VisibilityState::Rejected});
    app::VisibilityDisplay display;
    display.observe(scene, status);
    const auto cpu = display.objectFields(object.id, 9);
    REQUIRE(cpu[0].label == "Declared frame");
    REQUIRE(cpu[0].value == "12");
    REQUIRE(cpu[1].value == "Rejected");

    status.frameNumber = 13;
    status.classifyMode = render::ClassifyMode::Gpu;
    display.observe(scene, status);
    const auto pending = display.objectFields(object.id, 9);
    REQUIRE(pending.size() == 1);
    REQUIRE(pending[0].value == "Awaiting this object's rendered frame");
    status.isRetired = true;
    display.retire(status);
    const auto gpu = display.objectFields(object.id, 9);
    REQUIRE(gpu[0].label == "Retired frame");
    REQUIRE(gpu[0].value == "13");
    REQUIRE(gpu[1].value == "Rejected");
    REQUIRE(gpu[2].value == "Outside camera frustum");

    // Reused rows and scene switches must not borrow the retained frame's attribution.
    REQUIRE(display.objectFields({7, 3, 3}, 9).size() == 1);
    REQUIRE(display.objectFields(object.id, 10).size() == 1);
    display.clear();
    REQUIRE(display.objectFields(object.id, 9).size() == 1);
}

//======================================================================================================================
TEST_CASE("Visibility categories preserve failure diagnostics and separate pyramid costs",
          "[app][visibility]") {
    using Group = app::VisibilityFieldGroup;
    render::VisibilityStatus status;
    status.classifyMode = render::ClassifyMode::Gpu;
    const auto pending = app::visibilityFields(status);
    REQUIRE(pending.size() == 3);
    REQUIRE(std::ranges::all_of(pending,
                                [](const auto& field) { return field.group == Group::Frame; }));

    status.isRetired = true;
    status.occlusionEnabled = true;
    status.occlusionCheckEnabled = true;
    status.checkEnabled = true;
    status.rowMismatches = 1;
    status.overflow = true;
    const std::array<rhi::PassTiming, 3> timings = {{{"lmx.pass.visibility.classify", 0.25},
                                                     {"lmx.pass.hzb.level0", 0.125},
                                                     {"lmx.pass.scene", 2.0}}};
    const auto fields = app::visibilityFields(status, timings);
    std::array<size_t, 4> counts{};
    for (const auto& field : fields) {
        const auto group = static_cast<size_t>(field.group);
        REQUIRE(group < counts.size());
        ++counts[group];
    }
    REQUIRE(std::ranges::all_of(counts, [](size_t count) { return count > 0; }));
    const auto requireField = [&](std::string_view label, std::string_view value, Group group) {
        const auto found = std::ranges::find(fields, label, &app::VisibilityField::label);
        REQUIRE(found != fields.end());
        REQUIRE(found->value == value);
        REQUIRE(found->group == group);
    };
    requireField("CPU oracle check", "FAILED", Group::Visibility);
    requireField("State / row / argument / counter mismatches", "0 / 1 / 0 / 0", Group::Visibility);
    requireField("Independent ID check", "Awaiting reference", Group::Occlusion);
    requireField("Overflow", "Work dropped", Group::Submission);
    requireField("lmx.pass.visibility.classify", "0.250 ms", Group::Submission);
    requireField("lmx.pass.hzb.level0", "0.125 ms", Group::Occlusion);
    REQUIRE(std::ranges::none_of(
        fields, [](const auto& field) { return field.label == "lmx.pass.scene"; }));
}

//======================================================================================================================
TEST_CASE("Inspector visibility readings publish coherent owned frames at four Hz",
          "[app][visibility]") {
    scene::Scene scene;
    app::VisibilityDisplay display;
    render::VisibilityStatus status;
    status.frameNumber = 1;
    status.sceneGeneration = 9;
    status.classifyMode = render::ClassifyMode::Gpu;
    display.observe(scene, status);
    display.publishReadings(0.0);
    REQUIRE_FALSE(display.readingsStatus().isRetired);
    REQUIRE(display.readingsTimings().empty());

    status.isRetired = true;
    status.sceneCounters.visible = 5;
    display.retire(status);
    std::array<rhi::PassTiming, 1> timings = {{{"lmx.pass.hzb.level0", 0.125}}};
    display.observeTimings(1, timings);
    display.publishReadings(0.01);
    REQUIRE(display.readingsStatus().isRetired);
    REQUIRE(display.readingsStatus().frameNumber == 1);
    REQUIRE(display.readingsTimings()[0].gpuMilliseconds == 0.125);

    // New live frames and errors remain available immediately, but cannot overwrite the owned
    // Inspector publication or evict its timings while the reader is looking at it.
    for (uint64_t frame = 2; frame <= 12; ++frame) {
        status.frameNumber = frame;
        status.isRetired = false;
        display.observe(scene, status);
        status.isRetired = true;
        status.sceneCounters.visible = 7;
        status.overflow = true;
        display.retire(status);
        timings[0].gpuMilliseconds = 0.25;
        display.observeTimings(frame, timings);
        display.publishReadings(0.02 + frame * 0.01);
    }
    REQUIRE(display.status().frameNumber == 12);
    REQUIRE(display.status().overflow);
    REQUIRE(display.readingsStatus().frameNumber == 1);
    REQUIRE(display.readingsStatus().sceneCounters.visible == 5);
    REQUIRE(display.readingsTimings()[0].gpuMilliseconds == 0.125);
    display.publishReadings(0.26);
    REQUIRE(display.readingsStatus().frameNumber == 12);
    REQUIRE(display.readingsStatus().sceneCounters.visible == 7);
    REQUIRE(display.readingsTimings()[0].gpuMilliseconds == 0.25);

    status.frameNumber = 13;
    status.isRetired = false;
    display.observe(scene, status);
    status.isRetired = true;
    display.retire(status);
    display.publishReadings(0.51);
    REQUIRE(display.readingsStatus().frameNumber == 13);
    REQUIRE(display.readingsTimings().empty());

    status.classifyMode = render::ClassifyMode::Cpu;
    status.isRetired = false;
    status.frameNumber = 14;
    display.observe(scene, status);
    display.publishReadings(0.52);
    REQUIRE(display.readingsStatus().classifyMode == render::ClassifyMode::Cpu);
    REQUIRE(display.readingsStatus().frameNumber == 14);
    ++status.sceneGeneration;
    status.frameNumber = 15;
    display.observe(scene, status);
    REQUIRE(display.readingsStatus().frameNumber == 0);
    display.publishReadings(0.53);
    REQUIRE(display.readingsStatus().sceneGeneration == 10);
    REQUIRE(display.readingsTimings().empty());
    display.clear();
    REQUIRE(display.readingsStatus().frameNumber == 0);
}
