#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"

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
    REQUIRE(settings.localLightMode == lmx::engine::LocalLightMode::Clustered);
    settings.localLightMode = lmx::engine::LocalLightMode::Off;
    settings.lightDebugView = lmx::engine::LightDebugView::Missed;
    settings.lightCheck = true;
    settings.exposureEv = 3.0f;
    settings.renderScale = 0.5f;
    REQUIRE(lmx::app::renderingGroupChanged(settings, lmx::app::EditorRenderGroup::Lighting));
    lmx::app::resetRenderingGroup(settings, lmx::app::EditorRenderGroup::Lighting);
    REQUIRE_FALSE(lmx::app::renderingGroupChanged(settings, lmx::app::EditorRenderGroup::Lighting));
    REQUIRE(settings.localLightMode == lmx::engine::LocalLightMode::Clustered);
    REQUIRE_FALSE(settings.lightCheck);
    REQUIRE(settings.lightDebugView == lmx::engine::LightDebugView::Missed);
    REQUIRE(settings.exposureEv == 3.0f);
    REQUIRE(settings.renderScale == 0.5f);
}

//======================================================================================================================
TEST_CASE("scoped resets neither restore nor count debug views", "[app][render-defaults]") {
    EditorRenderSettings settings;
    settings.temporalDebugView = lmx::render::TemporalDebugView::RejectionMask;
    settings.lightDebugView = lmx::engine::LightDebugView::Count;
    CHECK_FALSE(renderingGroupChanged(settings, EditorRenderGroup::Reconstruction));
    CHECK_FALSE(renderingGroupChanged(settings, EditorRenderGroup::Lighting));
    settings.reconstruction = lmx::render::ReconstructionMode::Raw;
    settings.localLightMode = lmx::engine::LocalLightMode::Direct;
    resetRenderingGroup(settings, EditorRenderGroup::Reconstruction);
    resetRenderingGroup(settings, EditorRenderGroup::Lighting);
    CHECK(settings.temporalDebugView == lmx::render::TemporalDebugView::RejectionMask);
    CHECK(settings.lightDebugView == lmx::engine::LightDebugView::Count);
    // The restored defaults keep both active views valid: Native TAA and Clustered.
    CHECK(settings.temporalEnabled);
    CHECK(settings.reconstruction == lmx::render::ReconstructionMode::NativeTaa);
    CHECK(settings.localLightMode == lmx::engine::LocalLightMode::Clustered);
}

//======================================================================================================================
TEST_CASE("each Rendering topic maps to its documented reset scope", "[app][render-defaults]") {
    CHECK(renderingTopicResetGroup(RenderingCategory::Reconstruction) ==
          EditorRenderGroup::Reconstruction);
    CHECK(renderingTopicResetGroup(RenderingCategory::Resolution) == EditorRenderGroup::Resolution);
    CHECK(renderingTopicResetGroup(RenderingCategory::Lighting) == EditorRenderGroup::Lighting);
    CHECK(renderingTopicResetGroup(RenderingCategory::Exposure) == EditorRenderGroup::Exposure);
    CHECK(renderingTopicResetGroup(RenderingCategory::Bloom) == EditorRenderGroup::Bloom);
    CHECK(renderingTopicResetGroup(RenderingCategory::Shadows) == EditorRenderGroup::Shadows);
    CHECK(renderingTopicResetGroup(RenderingCategory::Display) == EditorRenderGroup::Display);
    for (const auto topic : {RenderingCategory::Overview, RenderingCategory::Visibility,
                             RenderingCategory::Occlusion, RenderingCategory::Submission,
                             RenderingCategory::SceneTables, RenderingCategory::Count}) {
        CAPTURE(static_cast<int>(topic));
        CHECK_FALSE(renderingTopicResetGroup(topic).has_value());
    }
}
