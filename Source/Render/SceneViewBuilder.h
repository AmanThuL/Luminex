//----------------------------------------------------------------------------------------------------------------------
/// @file SceneViewBuilder.h
/// @brief Declares how Render composes one frame's SceneView from a scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Types/DrawItem.h"
#include "Render/SceneView.h"

#include <vector>

namespace lmx::scene {
class Scene;
}

namespace lmx::render {

/// Fills `items` from `scene` (cleared first, one DrawItem per object, in object order) and
/// returns the SceneView Render consumes this frame: the scene's tables, coverage epoch, analytic
/// lights, bounding sphere, sky pair and IBL set, with `filter` and `wireframe`. The returned view,
/// `items` and the scene's resources must remain alive through pass declaration and graph
/// execution.
SceneView buildSceneView(const scene::Scene& scene, std::vector<DrawItem>& items,
                         ShadowFilter filter, bool wireframe);

} // namespace lmx::render
