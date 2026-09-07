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
// Selecting any scene, including TemporalLab, leaves the render settings exactly as the caller set
// them -- TemporalLab no longer applies a once-only default (spec section 9).
TEST_CASE("selecting any scene leaves the render settings untouched", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    settings.temporalEnabled = false;
    settings.temporalDebugView = render::TemporalDebugView::Off;

    onSceneSelected(state, settings, temporalLabId());

    REQUIRE_FALSE(settings.temporalEnabled);
    REQUIRE(settings.temporalDebugView == render::TemporalDebugView::Off);

    onSceneSelected(state, settings, engine::defaultSceneId());

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
