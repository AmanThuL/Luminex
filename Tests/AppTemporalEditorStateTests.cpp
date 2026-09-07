#include <catch2/catch_test_macros.hpp>

#include "App/TemporalEditorState.h"
#include "Engine/SceneLibrary.h"

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
engine::SceneId temporalLabId() {
    const std::optional<engine::SceneId> id = engine::parseSceneId("temporal-lab");
    REQUIRE(id.has_value());
    return *id;
}

} // namespace

//======================================================================================================================
TEST_CASE("onSceneSelected bumps the generation on the first call", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;

    onSceneSelected(state, settings, engine::defaultSceneId());

    REQUIRE(state.sceneGeneration == 1);
}

//======================================================================================================================
TEST_CASE("onSceneSelected bumps the generation on every call, including a reselect", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;

    onSceneSelected(state, settings, engine::defaultSceneId());
    onSceneSelected(state, settings, engine::defaultSceneId());
    onSceneSelected(state, settings, temporalLabId());

    REQUIRE(state.sceneGeneration == 3);
}

//======================================================================================================================
TEST_CASE("selecting TemporalLab the first time turns on temporal inputs and the Motion view",
          "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    REQUIRE_FALSE(settings.temporalEnabled);

    onSceneSelected(state, settings, temporalLabId());

    REQUIRE(settings.temporalEnabled);
    REQUIRE(settings.temporalDebugView == render::TemporalDebugView::MotionVectors);
    REQUIRE(state.temporalLabDefaultsApplied);
}

//======================================================================================================================
TEST_CASE("selecting a non-TemporalLab scene never applies TemporalLab's defaults", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;

    onSceneSelected(state, settings, engine::defaultSceneId());

    REQUIRE_FALSE(settings.temporalEnabled);
    REQUIRE(settings.temporalDebugView == render::TemporalDebugView::Off);
    REQUIRE_FALSE(state.temporalLabDefaultsApplied);
}

//======================================================================================================================
// The user may turn temporal back off after TemporalLab's first-selection default switched it on;
// a later reselect of the same scene must not override that choice.
TEST_CASE("reselecting TemporalLab does not override the user's own later choice", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;

    onSceneSelected(state, settings, temporalLabId());
    settings.temporalEnabled = false;
    settings.temporalDebugView = render::TemporalDebugView::Off;

    onSceneSelected(state, settings, engine::defaultSceneId());
    onSceneSelected(state, settings, temporalLabId());

    REQUIRE_FALSE(settings.temporalEnabled);
    REQUIRE(settings.temporalDebugView == render::TemporalDebugView::Off);
}

//======================================================================================================================
TEST_CASE("a camera cut is reported exactly once", "[app]") {
    TemporalEditorState state;

    REQUIRE_FALSE(consumeCameraCut(state));

    requestCameraCut(state);

    REQUIRE(consumeCameraCut(state));
    REQUIRE_FALSE(consumeCameraCut(state));
}
