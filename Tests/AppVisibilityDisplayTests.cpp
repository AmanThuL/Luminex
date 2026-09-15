//----------------------------------------------------------------------------------------------------------------------
/// @file AppVisibilityDisplayTests.cpp
/// @brief Pins visibility formatting and scene/frame identity isolation.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/VisibilityDisplay.h"
#include <catch2/catch_test_macros.hpp>

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
    REQUIRE(fields[0].value == "8");
    REQUIRE(fields[1].value == "4 / 2 / 1");
    REQUIRE(fields[2].value == "1");
    render::InstanceVisibility object;
    object.state = render::VisibilityState::Bypassed;
    object.reason = render::VisibilityReason::UnreliableBounds;
    object.worldBounds = {{-1, -2, -3}, {4, 5, 6}};
    const auto details = app::objectVisibilityFields(&object);
    REQUIRE(details[0].value == "Bypassed");
    REQUIRE(details[1].value == "Unreliable bounds");
    REQUIRE(details[2].value == "-1.000, -2.000, -3.000");
    REQUIRE(app::objectVisibilityFields(nullptr).size() == 1);
    REQUIRE(app::visibilityBadge(render::VisibilityState::Visible) == "[V]");
    REQUIRE(app::visibilityBadge(render::VisibilityState::Rejected) == "[R]");
    REQUIRE(app::visibilityBadge(render::VisibilityState::Bypassed) == "[B]");
    object.state = render::VisibilityState::Rejected;
    REQUIRE(app::objectVisibilityFields(&object)[1].value == "Outside camera frustum");
}
