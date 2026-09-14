//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionBounds.h
/// @brief Declares editor selection bounds and scale-aware camera framing.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/EditorSelection.h"

namespace lmx::app {

/// Transforms every corner of a reliable mesh-local AABB into world space. Returns no value for
/// missing, nonfinite or inverted bounds, or a nonfinite object transform.
std::optional<scene::ObjectBounds> objectWorldBounds(const scene::SceneObject& object);

/// Resolves the selected object's world bounds; non-object and out-of-range selections are N/A.
/// The caller resolves selection against the active scene before passing it here.
std::optional<scene::ObjectBounds> selectedObjectBounds(const scene::Scene& scene,
                                                        const EditorSelection& selection);

/// Fits the complete world-space bounds within the camera at `aspect` (width / height), retaining
/// yaw, pitch and field of view. Adjusts position, near plane and movement speed for subject scale.
/// Returns false without mutation for invalid bounds, lens, aspect or orientation. The caller must
/// mark a camera cut and stop camera-track following when this succeeds.
bool frameSelection(render::Camera& camera, const scene::ObjectBounds& bounds, float aspect);

} // namespace lmx::app
