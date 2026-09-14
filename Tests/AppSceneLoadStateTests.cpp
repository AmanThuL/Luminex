//----------------------------------------------------------------------------------------------------------------------
/// @file AppSceneLoadStateTests.cpp
/// @brief Tests deferred scene requests and explicit retry after a persistent loading failure.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/Model/EditorSelection.h"
#include "App/Model/SceneLoadState.h"

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("scene load failure preserves the current selection and requires explicit retry",
          "[app]") {
    SceneLoadState loading;
    const scene::SceneId active{0};
    const scene::SceneId requested{4};
    const EditorSelection selected{.sceneId = active, .subject = EditorSubject::Object, .index = 2};
    loading.request(requested);
    REQUIRE(loading.consumeRequest() == requested);
    REQUIRE_FALSE(loading.consumeRequest());
    loading.fail(requested, "Missing source texture: restore assets and retry.");
    const auto retained = sceneSwitchOutcome(false, active, requested, selected, "arch");
    REQUIRE(retained.selection.sceneId == active);
    REQUIRE(retained.selection.subject == EditorSubject::Object);
    REQUIRE(retained.selection.index == 2);
    REQUIRE(retained.filter == "arch");
    REQUIRE(loading.failedScene() == requested);
    REQUIRE(loading.failureMessage() == "Missing source texture: restore assets and retry.");
    REQUIRE_FALSE(loading.consumeRequest());
    REQUIRE(loading.failedScene() == requested);
    loading.request(*loading.failedScene());
    REQUIRE(loading.failureMessage().empty());
    REQUIRE_FALSE(loading.failedScene());
    REQUIRE(loading.consumeRequest() == requested);
    const auto switched = sceneSwitchOutcome(true, active, requested, selected, "arch");
    REQUIRE(switched.selection.sceneId == requested);
    REQUIRE(switched.selection.subject == EditorSubject::Camera);
    REQUIRE(switched.filter.empty());
}

//======================================================================================================================
TEST_CASE("choosing another scene replaces a failed attempt without retrying it", "[app]") {
    SceneLoadState loading;
    loading.fail(scene::SceneId{3}, "Decode failed");
    loading.request(scene::SceneId{5});
    REQUIRE_FALSE(loading.failedScene());
    REQUIRE(loading.consumeRequest() == scene::SceneId{5});
    REQUIRE_FALSE(loading.consumeRequest());
}
