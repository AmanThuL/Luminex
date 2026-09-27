//----------------------------------------------------------------------------------------------------------------------
/// @file SceneViewBuilder.cpp
/// @brief Composes a SceneView from a scene's draw items and public frame inputs.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/SceneViewBuilder.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Scene/Scene.h"

#include <iterator>

namespace lmx::render {

//======================================================================================================================
SceneView buildSceneView(const engine::Scene& scene, std::vector<engine::DrawItem>& items,
                         bool wireframe) {
    scene.fillDrawItems(items);

    SceneView sceneView;
    sceneView.items = items;
    sceneView.tables = scene.tables();
    sceneView.coverageEpoch = scene.coverageEpoch();
    for (size_t i = 0; i < std::size(sceneView.lights); ++i) {
        sceneView.lights[i] = scene.lights[i];
        if (!scene.lights[i].enabled)
            sceneView.lights[i].strength = glm::vec3(0.0f);
    }
    sceneView.shadowCaster = scene.shadowCaster && scene.lights[*scene.shadowCaster].enabled
                                 ? static_cast<int32_t>(*scene.shadowCaster)
                                 : -1;
    sceneView.boundingSphere = scene.boundingSphere;
    // A cubemap marks a fully constructed sky; the sphere and cubemap are published together.
    if (scene.skyCubemap != nullptr) {
        LMX_ASSERT(scene.skySphere && scene.tryMesh(*scene.skySphere),
                   "sky mesh identity is invalid");
        sceneView.skySphere = *scene.tryMesh(*scene.skySphere);
        sceneView.skyCubemap = scene.skyCubemap.get();
    }
    // The IBL set is generated from that same sky and published with it, so a scene that shows a
    // sky also lights from it. Forwarded unconditionally: unique_ptr::get() on an empty pointer is
    // the null the renderer's fallbacks already handle.
    sceneView.irradiance = scene.irradianceMap.get();
    sceneView.prefilteredEnv = scene.prefilteredEnvMap.get();
    sceneView.dfgLut = scene.dfgLut.get();
    const auto& look = scene.look;
    sceneView.shadowFilter =
        look.shadowFilter == asset::ShadowFilter::PCSS ? ShadowFilter::PCSS : ShadowFilter::PCF;
    sceneView.exposureEv = look.exposure.ev;
    sceneView.autoExposureEnabled = look.exposure.autoEnabled;
    sceneView.exposureLowPercentile = look.exposure.lowPercentile;
    sceneView.exposureHighPercentile = look.exposure.highPercentile;
    sceneView.exposureTargetGrey = look.exposure.targetGrey;
    sceneView.exposureEvMin = look.exposure.evMin;
    sceneView.exposureEvMax = look.exposure.evMax;
    sceneView.exposureCompensationEv = look.exposure.compensationEv;
    sceneView.exposureAdaptUpStopsPerSecond = look.exposure.adaptUpStopsPerSecond;
    sceneView.exposureAdaptDownStopsPerSecond = look.exposure.adaptDownStopsPerSecond;
    sceneView.bloomEnabled = look.bloom.enabled;
    sceneView.bloomThreshold = look.bloom.threshold;
    sceneView.bloomIntensity = look.bloom.intensity;
    sceneView.wireframe = wireframe;
    return sceneView;
}

} // namespace lmx::render
