#include <catch2/catch_test_macros.hpp>

#include "App/ExposureReset.h"
#include "Engine/SceneLibrary.h"

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
// A context naming a loaded scene, otherwise at EditorShell's own defaults -- the state every case
// below starts from unless the point of the case is that field itself.
ExposureResetContext loaded(engine::SceneId scene, bool autoExposureEnabled = false,
                            uint32_t width = 1920, uint32_t height = 1080) {
    return {.sceneId = scene,
            .autoExposureEnabled = autoExposureEnabled,
            .width = width,
            .height = height};
}

} // namespace

//======================================================================================================================
TEST_CASE("the first frame resets: no scene has loaded yet", "[app]") {
    const ExposureResetContext previous; // sceneId unset -- construction default.
    const ExposureResetContext current = loaded(engine::defaultSceneId());

    REQUIRE(shouldResetExposure(previous, current));
}

//======================================================================================================================
TEST_CASE("a scene switch resets", "[app]") {
    const engine::SceneId first = engine::defaultSceneId();
    const engine::SceneId second{.catalogIndex = first.catalogIndex + 1};

    const ExposureResetContext previous = loaded(first);
    const ExposureResetContext current = loaded(second);

    REQUIRE(shouldResetExposure(previous, current));
}

//======================================================================================================================
TEST_CASE("enabling auto exposure resets", "[app]") {
    const ExposureResetContext previous =
        loaded(engine::defaultSceneId(), /*autoExposureEnabled=*/false);
    ExposureResetContext current = previous;
    current.autoExposureEnabled = true;

    REQUIRE(shouldResetExposure(previous, current));
}

//======================================================================================================================
TEST_CASE("a successful resize resets", "[app]") {
    const ExposureResetContext previous = loaded(engine::defaultSceneId());
    ExposureResetContext current = previous;
    current.width = previous.width + 1;

    REQUIRE(shouldResetExposure(previous, current));
}

//======================================================================================================================
// Height alone is enough, symmetrically with width above -- the two are independent fields and a
// change in either must be caught.
TEST_CASE("a resize that only changes height resets", "[app]") {
    const ExposureResetContext previous = loaded(engine::defaultSceneId());
    ExposureResetContext current = previous;
    current.height = previous.height + 1;

    REQUIRE(shouldResetExposure(previous, current));
}

//======================================================================================================================
// Manual mode never reads the feedback buffer, so disabling auto-exposure has nothing to restart --
// unlike enabling it, this is not one of spec 9's four triggers.
TEST_CASE("disabling auto exposure does not reset", "[app]") {
    const ExposureResetContext previous =
        loaded(engine::defaultSceneId(), /*autoExposureEnabled=*/true);
    ExposureResetContext current = previous;
    current.autoExposureEnabled = false;

    REQUIRE_FALSE(shouldResetExposure(previous, current));
}

//======================================================================================================================
// A failed resize never takes effect (Renderer::resize() returned an error and the prior targets
// stayed live), so EditorShell never builds a `current` with different dimensions for it --
// modelled here as comparing a context against an identical copy of itself, which is what every
// call site that decides nothing changed effectively does.
TEST_CASE("an unchanged context does not reset (a failed resize, or any other no-op)", "[app]") {
    const ExposureResetContext previous = loaded(engine::defaultSceneId());
    const ExposureResetContext current = previous;

    REQUIRE_FALSE(shouldResetExposure(previous, current));
}
