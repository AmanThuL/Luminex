#include <catch2/catch_test_macros.hpp>

#include "App/DynamicResolution.h"
#include "Render/Temporal.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
render::CompiledFrameRecord recordWithId(uint64_t frameId) {
    render::CompiledFrameRecord record;
    record.frameId = frameId;
    return record;
}

//======================================================================================================================
RetainedFrame timedFrame(uint64_t frameId, std::vector<rhi::PassTiming> timings) {
    RetainedFrame frame;
    frame.record = recordWithId(frameId);
    frame.timings = std::move(timings);
    frame.timed = true;
    return frame;
}

} // namespace

//======================================================================================================================
TEST_CASE("frameGpuMilliseconds sums every pass's GPU time", "[app]") {
    const std::vector<rhi::PassTiming> timings = {
        {.label = "shadow", .gpuMilliseconds = 1.5},
        {.label = "scene", .gpuMilliseconds = 4.25},
        {.label = "display", .gpuMilliseconds = 0.75},
    };

    REQUIRE(frameGpuMilliseconds(timings) == 6.5);
}

//======================================================================================================================
TEST_CASE("frameGpuMilliseconds is zero for no passes", "[app]") {
    REQUIRE(frameGpuMilliseconds({}) == 0.0);
}

//======================================================================================================================
TEST_CASE("enabling dynamic resolution seeds the controller from the manual scale", "[app]") {
    render::ResolutionController controller;
    controller.reset(0.75f); // Some earlier value the enable must overwrite.
    DynamicResolutionState state;
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.renderScale = 0.6f;

    applyDynamicResolution(state, controller, settings, /*newestTimed=*/nullptr);

    REQUIRE(controller.scale() == 0.6f);
    REQUIRE(settings.renderScale == 0.6f);
    REQUIRE(state.wasEnabled);
}

//======================================================================================================================
TEST_CASE("a newest timed frame is observed once per frame number", "[app]") {
    render::ResolutionControllerSettings controllerSettings;
    controllerSettings.overBudgetSamples = 1; // One over-budget sample is enough to step.
    controllerSettings.settleFrames = 0;
    render::ResolutionController controller(controllerSettings);
    controller.reset(1.0f);
    controller.declared(1);

    DynamicResolutionState state;
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.renderScale = 1.0f;
    // The enabling call seeds the controller and clears its declared-frame ring, so a second call
    // is what actually attributes an observation to frame 1.
    applyDynamicResolution(state, controller, settings, nullptr);
    controller.declared(1);

    const std::vector<rhi::PassTiming> overBudget = {{.label = "scene", .gpuMilliseconds = 40.0}};
    const RetainedFrame frame = timedFrame(1, overBudget);

    applyDynamicResolution(state, controller, settings, &frame);
    const float scaleAfterFirstObservation = controller.scale();
    REQUIRE(scaleAfterFirstObservation < 1.0f); // Stepped down from the over-budget sample.
    REQUIRE(state.lastObservedFrame == 1);
    REQUIRE(state.lastObservedMilliseconds == 40.0);

    // A second call naming the same frame number must not observe again: state.lastObservedFrame
    // already names frame 1, so a further step here would mean it was judged twice.
    applyDynamicResolution(state, controller, settings, &frame);
    REQUIRE(controller.scale() == scaleAfterFirstObservation);
}

//======================================================================================================================
TEST_CASE("applyDynamicResolution writes settings.renderScale from the controller while enabled",
          "[app]") {
    render::ResolutionController controller;
    controller.reset(0.8f);
    DynamicResolutionState state;
    state.wasEnabled = true; // Already on -- this call must not re-seed from the manual slider.
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.renderScale = 1.0f; // Stale manual value the controller's own scale must overwrite.

    applyDynamicResolution(state, controller, settings, nullptr);

    REQUIRE(settings.renderScale == 0.8f);
}

//======================================================================================================================
TEST_CASE("disabling dynamic resolution leaves renderScale at the controller's last value",
          "[app]") {
    render::ResolutionController controller;
    // Deliberately different from settings.renderScale below: if the disabled branch wrongly
    // wrote settings.renderScale = controller.scale() (as the enabled branch does), this would
    // overwrite 0.65f with 0.5f and the assertion below would catch it.
    controller.reset(0.5f);
    DynamicResolutionState state;
    state.wasEnabled = true;
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = false;
    settings.renderScale = 0.65f; // What the controller last wrote before being switched off.

    applyDynamicResolution(state, controller, settings, nullptr);

    REQUIRE(settings.renderScale == 0.65f);
    REQUIRE_FALSE(state.wasEnabled);
}

//======================================================================================================================
TEST_CASE("editing the GPU budget updates the controller's settings", "[app]") {
    render::ResolutionController controller;
    DynamicResolutionState state;
    state.wasEnabled = true;
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.gpuBudgetMilliseconds = 20.0f;

    applyDynamicResolution(state, controller, settings, nullptr);

    REQUIRE(controller.settings().budgetMilliseconds == 20.0f);
}

//======================================================================================================================
TEST_CASE("dynamic resolution is active only while temporal is on as well", "[app]") {
    EditorRenderSettings settings;

    settings.dynamicResolutionEnabled = true;
    settings.temporalEnabled = true;
    CHECK(dynamicResolutionActive(settings));

    settings.temporalEnabled = false;
    CHECK_FALSE(dynamicResolutionActive(settings));

    settings.dynamicResolutionEnabled = false;
    settings.temporalEnabled = true;
    CHECK_FALSE(dynamicResolutionActive(settings));
}

//======================================================================================================================
// The renderer rasterises at scale 1 whenever temporal is off, so a frame timed in that period says
// nothing about the controller's own scale. Judging one would drive the greyed-out slider from
// measurements of a picture the controller never asked for.
TEST_CASE("over-budget frames are not judged while temporal is off", "[app]") {
    render::ResolutionControllerSettings controllerSettings;
    controllerSettings.overBudgetSamples = 1;
    controllerSettings.settleFrames = 0;
    render::ResolutionController controller(controllerSettings);
    controller.reset(0.8f);

    DynamicResolutionState state;
    state.wasEnabled = true; // Was active before temporal was switched off.
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.temporalEnabled = false;
    settings.renderScale = 0.8f;

    const std::vector<rhi::PassTiming> overBudget = {{.label = "scene", .gpuMilliseconds = 40.0}};
    for (uint64_t frameId = 1; frameId <= 8; ++frameId) {
        controller.declared(frameId);
        const RetainedFrame frame = timedFrame(frameId, overBudget);
        applyDynamicResolution(state, controller, settings, &frame);
    }

    CHECK(settings.renderScale == 0.8f);
    CHECK(controller.scale() == 0.8f);
    CHECK_FALSE(state.wasEnabled);
}

//======================================================================================================================
// Switching temporal back on is the off->on edge of the *active* state: the controller reseeds from
// the retained slider value, and a frame that retired while it was idle is not consumed afterwards.
TEST_CASE("re-enabling temporal reseeds from the retained scale and drops the idle period's frame",
          "[app]") {
    render::ResolutionControllerSettings controllerSettings;
    controllerSettings.overBudgetSamples = 1; // One judged sample would be enough to step.
    controllerSettings.settleFrames = 0;
    render::ResolutionController controller(controllerSettings);
    controller.reset(1.0f);

    DynamicResolutionState state;
    state.wasEnabled = true;
    EditorRenderSettings settings;
    settings.dynamicResolutionEnabled = true;
    settings.temporalEnabled = false;
    settings.renderScale = 0.8f; // The value retained from when the controller last ran.

    const std::vector<rhi::PassTiming> overBudget = {{.label = "scene", .gpuMilliseconds = 40.0}};
    const RetainedFrame idleFrame = timedFrame(7, overBudget);
    controller.declared(7);
    applyDynamicResolution(state, controller, settings, &idleFrame);
    CHECK(controller.scale() == 1.0f); // Untouched: nothing was judged.

    settings.temporalEnabled = true;
    applyDynamicResolution(state, controller, settings, &idleFrame);
    CHECK(controller.scale() == 0.8f); // Reseeded from the retained slider value.
    CHECK(settings.renderScale == 0.8f);
    CHECK(state.wasEnabled);

    // Frame 7 belongs to the idle period. Even declared again at the reseeded scale, its timing
    // must not be observed now -- it was already accounted for while the controller was idle.
    controller.declared(7);
    applyDynamicResolution(state, controller, settings, &idleFrame);
    CHECK(controller.scale() == 0.8f);
}
