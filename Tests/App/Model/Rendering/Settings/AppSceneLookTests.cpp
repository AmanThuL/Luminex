#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Rendering/Settings/ExposureReset.h"
#include "App/Model/Scene/SceneSession.h"
#include "Render/Renderer/SceneViewBuilder.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
template <class Settings>
constexpr bool hasLookSettings() {
    return requires(Settings s) { s.exposureEv; } ||
           requires(Settings s) { s.autoExposureEnabled; } ||
           requires(Settings s) { s.exposureLowPercentile; } ||
           requires(Settings s) { s.exposureHighPercentile; } ||
           requires(Settings s) { s.exposureTargetGrey; } ||
           requires(Settings s) { s.exposureEvMin; } || requires(Settings s) { s.exposureEvMax; } ||
           requires(Settings s) { s.exposureCompensationEv; } ||
           requires(Settings s) { s.exposureAdaptUpStopsPerSecond; } ||
           requires(Settings s) { s.exposureAdaptDownStopsPerSecond; } ||
           requires(Settings s) { s.bloomEnabled; } || requires(Settings s) { s.bloomThreshold; } ||
           requires(Settings s) { s.bloomIntensity; } || requires(Settings s) { s.shadowFilter; };
}

//======================================================================================================================
asset::SceneLook distinctLook() {
    asset::SceneLook look;
    look.exposure = {.ev = 2.0f,
                     .autoEnabled = true,
                     .lowPercentile = 10.0f,
                     .highPercentile = 80.0f,
                     .targetGrey = 0.3f,
                     .evMin = -3.0f,
                     .evMax = 4.0f,
                     .compensationEv = 1.0f,
                     .adaptUpStopsPerSecond = 7.0f,
                     .adaptDownStopsPerSecond = 5.0f};
    look.bloom = {.enabled = false, .threshold = 2.0f, .intensity = 0.7f};
    look.shadowFilter = asset::ShadowFilter::PCSS;
    return look;
}

//======================================================================================================================
void checkLook(const render::SceneView& view, const asset::SceneLook& look) {
    CHECK(view.exposureEv == look.exposure.ev);
    CHECK(view.autoExposureEnabled == look.exposure.autoEnabled);
    CHECK(view.exposureLowPercentile == look.exposure.lowPercentile);
    CHECK(view.exposureHighPercentile == look.exposure.highPercentile);
    CHECK(view.exposureTargetGrey == look.exposure.targetGrey);
    CHECK(view.exposureEvMin == look.exposure.evMin);
    CHECK(view.exposureEvMax == look.exposure.evMax);
    CHECK(view.exposureCompensationEv == look.exposure.compensationEv);
    CHECK(view.exposureAdaptUpStopsPerSecond == look.exposure.adaptUpStopsPerSecond);
    CHECK(view.exposureAdaptDownStopsPerSecond == look.exposure.adaptDownStopsPerSecond);
    CHECK(view.bloomEnabled == look.bloom.enabled);
    CHECK(view.bloomThreshold == look.bloom.threshold);
    CHECK(view.bloomIntensity == look.bloom.intensity);
    CHECK(view.shadowFilter == (look.shadowFilter == asset::ShadowFilter::PCSS
                                    ? render::ShadowFilter::PCSS
                                    : render::ShadowFilter::PCF));
}

} // namespace

//======================================================================================================================
TEST_CASE("persistent look fields leave editor rendering settings", "[app][scene-look]") {
    CHECK_FALSE(hasLookSettings<EditorRenderSettings>());
}

//======================================================================================================================
TEST_CASE("editor and headless sessions render every active look field", "[app][scene-look]") {
    engine::Scene scene;
    scene.look = distinctLook();
    SceneSession editor;
    SceneSession headless;
    editor.activate(scene, SceneActivationMotion::Reset);
    headless.activate(scene, SceneActivationMotion::PreserveLoadedMotion);
    std::vector<engine::DrawItem> editorItems, headlessItems;
    checkLook(editor.view(editorItems, false), scene.look);
    checkLook(headless.view(headlessItems, false), scene.look);
    CHECK(editor.editGeneration() == 0);
    CHECK(headless.editGeneration() == 0);
}

//======================================================================================================================
TEST_CASE("scene look switches and replacement reset automatic exposure", "[app][scene-look]") {
    engine::Scene first, second;
    second.look = distinctLook();
    SceneSession session;
    ExposureResetContext context;
    bool pending = false;
    std::vector<engine::DrawItem> items;
    session.activate(first, SceneActivationMotion::Reset);
    activateExposureLook(context, pending, scenes::SceneId{"first"}, session.look());
    CHECK(pending);
    pending = false;
    session.activate(second, SceneActivationMotion::Reset);
    activateExposureLook(context, pending, scenes::SceneId{"second"}, session.look());
    checkLook(session.view(items, false), second.look);
    CHECK(pending);
    CHECK(context.autoExposureEnabled);
    pending = false;
    session.invalidate(second);
    second.look.exposure.autoEnabled = false;
    second.look.exposure.ev = -2.0f;
    session.activate(second, SceneActivationMotion::Reset);
    activateExposureLook(context, pending, scenes::SceneId{"second"}, session.look());
    CHECK(pending);
    CHECK_FALSE(context.autoExposureEnabled);
    CHECK(session.lookDefault().exposure.ev == -2.0f);
    CHECK(session.editGeneration() == 0);
}

//======================================================================================================================
TEST_CASE("look reset uses loaded document and explicit saved baseline", "[app][scene-look]") {
    engine::LoadedScene loaded;
    loaded.scene = std::make_unique<engine::Scene>();
    loaded.document.look = distinctLook();
    loaded.scene->look = loaded.document.look;
    loaded.scene->look.exposure.ev = 5.0f;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    CHECK(session.lookDefault().exposure.ev == 2.0f);
    EditorRenderSettings settings;
    settings.renderScale = 0.6f;
    auto edited = session.look();
    edited.exposure = {};
    edited.bloom = {};
    edited.shadowFilter = asset::ShadowFilter::PCF;
    session.editLook(edited);
    CHECK(session.editGeneration() == 1);
    session.editLook(edited);
    CHECK(session.editGeneration() == 1);
    for (auto group :
         {EditorRenderGroup::Exposure, EditorRenderGroup::Bloom, EditorRenderGroup::Shadows}) {
        CHECK(renderingGroupChanged(settings, session, group));
        resetRenderingGroup(settings, session, group);
        CHECK_FALSE(renderingGroupChanged(settings, session, group));
    }
    CHECK(session.editGeneration() == 4);
    CHECK(session.look() == loaded.document.look);
    CHECK(settings.renderScale == 0.6f);
    edited.exposure.ev = 4.0f;
    session.adoptLookResetBaseline(edited);
    CHECK(session.editGeneration() == 4);
    resetRenderingGroup(settings, session, EditorRenderGroup::Exposure);
    CHECK(session.look().exposure.ev == 4.0f);
    CHECK(session.editGeneration() == 5);
    session.stepAnimation();
    session.resetMotion();
    CHECK(session.editGeneration() == 5);
    session.notifyPersistentEdit();
    CHECK(session.editGeneration() == 6);
}

//======================================================================================================================
TEST_CASE("auto exposure edits and document resets share the exposure reset edge",
          "[app][scene-look]") {
    engine::Scene scene;
    scene.look = distinctLook();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    ExposureResetContext context{.sceneId = scenes::SceneId{"test"}, .autoExposureEnabled = true};
    bool pending = false;
    setAutoExposureEnabled(session, context, pending, false);
    CHECK_FALSE(pending);
    CHECK_FALSE(session.look().exposure.autoEnabled);
    EditorRenderSettings settings;
    resetRenderingGroup(settings, session, EditorRenderGroup::Exposure);
    reconcileExposureLook(session.look(), context, pending);
    CHECK(pending);
    CHECK(context.autoExposureEnabled);
    CHECK(session.editGeneration() == 2);
}

//======================================================================================================================
TEST_CASE("scene look baselines and edit generations survive scene switches", "[app][scene-look]") {
    engine::Scene first, second;
    second.look = distinctLook();
    SceneSession session;
    session.activate(first, SceneActivationMotion::Reset);
    std::vector<engine::DrawItem> items;
    checkLook(session.view(items, false), asset::SceneLook{});
    auto edited = session.look();
    edited.exposure.ev = 3.0f;
    session.editLook(edited);
    session.activate(second, SceneActivationMotion::Reset);
    CHECK(session.editGeneration() == 0);
    CHECK(session.lookDefault() == distinctLook());
    session.activate(first, SceneActivationMotion::Reset);
    CHECK(session.editGeneration() == 1);
    CHECK(session.look().exposure.ev == 3.0f);
    CHECK(session.lookDefault() == asset::SceneLook{});
    EditorRenderSettings settings;
    resetRenderingGroup(settings, session, EditorRenderGroup::Exposure);
    CHECK(session.editGeneration() == 2);
    resetRenderingGroup(settings, session, EditorRenderGroup::Exposure);
    CHECK(session.editGeneration() == 2);
}
