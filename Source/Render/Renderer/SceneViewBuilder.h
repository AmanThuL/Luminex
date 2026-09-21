//----------------------------------------------------------------------------------------------------------------------
/// @file SceneViewBuilder.h
/// @brief Declares how Render composes one frame's SceneView from a scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/DrawItem.h"
#include "Render/Renderer/SceneView.h"

#include <vector>

namespace lmx::engine {
class Scene;
}

namespace lmx::render {

/// Fills `items` from `scene` (cleared first, one DrawItem per object, in object order) and
/// returns the SceneView Render consumes this frame: the scene's tables, coverage epoch, analytic
/// lights, bounding sphere, sky pair and IBL set, with `filter` and `wireframe`. The returned view,
/// `items` and the scene's resources must remain alive through pass declaration and graph
/// execution.
SceneView buildSceneView(const engine::Scene& scene, std::vector<engine::DrawItem>& items,
                         ShadowFilter filter, bool wireframe);

} // namespace lmx::render
