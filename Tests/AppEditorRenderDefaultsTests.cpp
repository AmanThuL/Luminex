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
    settings.animationPlaying = false;
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
    CHECK_FALSE(settings.animationPlaying);
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
