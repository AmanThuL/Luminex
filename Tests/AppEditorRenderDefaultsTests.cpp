#include "App/Model/EditorRenderDefaults.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("rendering resets stay within their named scope and preserve playback", "[app]") {
    EditorRenderSettings settings;
    settings.exposureEv = 3.0f;
    settings.autoExposureEnabled = true;
    settings.bloomIntensity = 0.8f;
    settings.renderScale = 0.6f;
    settings.dynamicResolutionEnabled = true;
    settings.temporalEnabled = false;
    settings.reconstruction = lmx::render::ReconstructionMode::Raw;
    settings.followCameraTrack = false;
    CHECK(renderingGroupChanged(settings, EditorRenderGroup::Exposure));
    resetRenderingGroup(settings, EditorRenderGroup::Exposure);
    CHECK_FALSE(renderingGroupChanged(settings, EditorRenderGroup::Exposure));
    CHECK(settings.bloomIntensity == 0.8f);
    CHECK(settings.renderScale == 0.6f);
    CHECK_FALSE(settings.temporalEnabled);
    resetRenderingGroup(settings, EditorRenderGroup::Reconstruction);
    CHECK(settings.temporalEnabled);
    CHECK(settings.reconstruction == lmx::render::ReconstructionMode::NativeTaa);
    CHECK(settings.renderScale == 0.6f);
    CHECK(settings.dynamicResolutionEnabled);
    resetRenderingGroup(settings, EditorRenderGroup::Resolution);
    CHECK(settings.renderScale == 1.0f);
    CHECK_FALSE(settings.dynamicResolutionEnabled);
    CHECK(settings.bloomIntensity == 0.8f);
    CHECK_FALSE(settings.followCameraTrack);
}

//======================================================================================================================
TEST_CASE("each rendering reset scope recognizes its editor defaults", "[app]") {
    for (auto group : {EditorRenderGroup::Exposure, EditorRenderGroup::Bloom,
                       EditorRenderGroup::Shadows, EditorRenderGroup::Reconstruction,
                       EditorRenderGroup::Resolution, EditorRenderGroup::Display}) {
        EditorRenderSettings settings;
        CHECK_FALSE(renderingGroupChanged(settings, group));
        resetRenderingGroup(settings, group);
        CHECK_FALSE(renderingGroupChanged(settings, group));
    }
}

//======================================================================================================================
TEST_CASE("Lighting reset is scoped to local-light settings", "[app][render-defaults]") {
    lmx::app::EditorRenderSettings settings;
    REQUIRE(settings.localLightMode == lmx::render::LocalLightMode::Clustered);
    settings.localLightMode = lmx::render::LocalLightMode::Off;
    settings.lightDebugView = lmx::render::LightDebugView::Missed;
    settings.lightCheck = true;
    settings.exposureEv = 3.0f;
    settings.renderScale = 0.5f;
    REQUIRE(lmx::app::renderingGroupChanged(settings, lmx::app::EditorRenderGroup::Lighting));
    lmx::app::resetRenderingGroup(settings, lmx::app::EditorRenderGroup::Lighting);
    REQUIRE_FALSE(lmx::app::renderingGroupChanged(settings, lmx::app::EditorRenderGroup::Lighting));
    REQUIRE(settings.localLightMode == lmx::render::LocalLightMode::Clustered);
    REQUIRE(settings.exposureEv == 3.0f);
    REQUIRE(settings.renderScale == 0.5f);
}
