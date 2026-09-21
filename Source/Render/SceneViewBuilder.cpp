//----------------------------------------------------------------------------------------------------------------------
/// @file SceneViewBuilder.cpp
/// @brief Composes a SceneView from a scene's draw items and public frame inputs.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/SceneViewBuilder.h"

#include "Core/Assert.h"
#include "Engine/Scene/Scene.h"

#include <iterator>

namespace lmx::render {

//======================================================================================================================
SceneView buildSceneView(const scene::Scene& scene, std::vector<DrawItem>& items,
                         ShadowFilter filter, bool wireframe) {
    scene.fillDrawItems(items);

    SceneView sceneView;
    sceneView.items = items;
    sceneView.tables = scene.tables();
    sceneView.coverageEpoch = scene.coverageEpoch();
    for (size_t i = 0; i < std::size(sceneView.lights); ++i) {
        sceneView.lights[i] = scene.lights[i];
    }
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
    sceneView.shadowFilter = filter;
    sceneView.wireframe = wireframe;
    return sceneView;
}

} // namespace lmx::render
