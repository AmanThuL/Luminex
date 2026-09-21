#include <catch2/catch_test_macros.hpp>

#include "App/Model/TemporalEditorState.h"
#include "Engine/Catalog/SceneLibrary.h"

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

//======================================================================================================================
TEST_CASE("reconstruction names follow the capability without changing native labels", "[app]") {
    const rojoRHI::TemporalScalerSupport available{
        .available = true, .minInputScale = 0.5f, .maxInputScale = 1.0f, .name = "Test Temporal"};
    REQUIRE(reconstructionName(render::ReconstructionMode::Raw, available) == "Raw");
    REQUIRE(reconstructionName(render::ReconstructionMode::NativeTaa, {}) == "Native TAA");
    REQUIRE(reconstructionName(render::ReconstructionMode::VendorTemporal, available) ==
            "Test Temporal");
    REQUIRE(reconstructionName(render::ReconstructionMode::VendorTemporal, {}) ==
            "Vendor temporal (unavailable)");
}

//======================================================================================================================
TEST_CASE(
    "native-only diagnostics clamp under effective vendor mode and remain available in fallback",
    "[app]") {
    for (auto view :
         {render::TemporalDebugView::RejectionMask, render::TemporalDebugView::BlendWeight,
          render::TemporalDebugView::HistoryAge}) {
        REQUIRE(clampTemporalDebugView(view, render::ReconstructionMode::VendorTemporal) ==
                render::TemporalDebugView::Off);
        REQUIRE(clampTemporalDebugView(view, render::ReconstructionMode::NativeTaa) == view);
        REQUIRE(clampTemporalDebugView(view, render::ReconstructionMode::Raw) == view);
    }
    for (auto view : {render::TemporalDebugView::Off, render::TemporalDebugView::MotionVectors,
                      render::TemporalDebugView::ReprojectionError,
                      render::TemporalDebugView::ReprojectedHistory}) {
        REQUIRE(clampTemporalDebugView(view, render::ReconstructionMode::VendorTemporal) == view);
    }
}

//======================================================================================================================
TEST_CASE("editor preserves the paired last reset event across normal frames", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    render::TemporalStatus status;
    status.lastReset = render::HistoryResetReason::CameraCut;
    status.lastResetFrame = 12;
    observeDeclaredTemporal(state, settings, status, 15);
    status.lastReset = render::HistoryResetReason::None;
    observeDeclaredTemporal(state, settings, status, 16);
    CHECK(state.lastResetReason == render::HistoryResetReason::CameraCut);
    CHECK(state.lastResetFrame == 12);
    CHECK(status.lastReset == render::HistoryResetReason::None);
}

//======================================================================================================================
TEST_CASE("temporal off retains requests while presenting full resolution and no history",
          "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    settings.temporalEnabled = false;
    settings.reconstruction = render::ReconstructionMode::VendorTemporal;
    settings.renderScale = 0.5f;
    settings.dynamicResolutionEnabled = true;
    render::TemporalStatus status;
    status.reconstruction = render::ReconstructionMode::VendorTemporal;
    status.renderScale = 0.5f;
    observeDeclaredTemporal(state, settings, status, 1);
    const auto presentation = temporalPresentation(state, settings, status, {}, 1920, 1080);
    CHECK(presentation.effectiveName == "Off");
    CHECK_FALSE(presentation.temporalActive);
    CHECK(presentation.effectiveScale == 1.0f);
    CHECK(presentation.extents.renderWidth == 1920);
    CHECK(presentation.extents.renderHeight == 1080);
    CHECK(settings.renderScale == 0.5f);
    CHECK(settings.dynamicResolutionEnabled);
    CHECK(settings.reconstruction == render::ReconstructionMode::VendorTemporal);
}

//======================================================================================================================
TEST_CASE("vendor fallback keeps requested and effective algorithms distinct", "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    settings.reconstruction = render::ReconstructionMode::VendorTemporal;
    render::TemporalStatus status;
    status.reconstruction = render::ReconstructionMode::NativeTaa;
    status.vendorFallback = render::VendorFallback::CreationFailed;
    const rojoRHI::TemporalScalerSupport support{.available = true, .name = "Test Temporal"};
    observeDeclaredTemporal(state, settings, status, 1);
    auto presentation = temporalPresentation(state, settings, status, support, 1280, 720);
    CHECK(presentation.requestedName == "Test Temporal");
    CHECK(presentation.effectiveName == "Native TAA");
    CHECK_FALSE(presentation.fallbackReason.empty());
    settings.reconstruction = render::ReconstructionMode::Raw;
    presentation = temporalPresentation(state, settings, status, support, 1280, 720);
    CHECK(presentation.waitingForDeclaration);
    CHECK(presentation.effectiveName == "Waiting for declaration");
    CHECK(presentation.fallbackReason.empty());
}

//======================================================================================================================
TEST_CASE("live telemetry distinguishes waiting from measured zero and invalidates scene or mode",
          "[app]") {
    TemporalEditorState state;
    EditorRenderSettings settings;
    render::TemporalStatus status;
    observeDeclaredTemporal(state, settings, status, 10);
    CHECK_FALSE(state.liveTimedPassSumMilliseconds.has_value());
    RetainedFrame frame;
    frame.record.frameId = 9;
    frame.timed = true;
    observeRetiredTemporal(state, &frame);
    CHECK_FALSE(state.liveTimedPassSumMilliseconds.has_value());
    frame.record.frameId = 10;
    observeRetiredTemporal(state, &frame);
    REQUIRE(state.liveTimedPassSumMilliseconds.has_value());
    CHECK(*state.liveTimedPassSumMilliseconds == 0.0);
    CHECK(state.liveMeasurementFrame == 10);
    settings.temporalEnabled = false;
    observeDeclaredTemporal(state, settings, status, 11);
    observeRetiredTemporal(state, &frame);
    CHECK_FALSE(state.liveTimedPassSumMilliseconds.has_value());
    frame.record.frameId = 11;
    observeRetiredTemporal(state, &frame);
    CHECK(state.liveTimedPassSumMilliseconds.has_value());
    onSceneSelected(state, settings, engine::defaultSceneId());
    CHECK_FALSE(state.liveTimedPassSumMilliseconds.has_value());
    observeDeclaredTemporal(state, settings, status, 12);
    observeRetiredTemporal(state, &frame);
    CHECK_FALSE(state.liveTimedPassSumMilliseconds.has_value());
}
