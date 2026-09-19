//----------------------------------------------------------------------------------------------------------------------
/// @file SponzaLightRig.h
/// @brief Declares the permanent static local-light rig for the authored Sponza scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Scene/Scene.h"

#include <span>
#include <vector>

namespace lmx::scene {

/// Binds the scene-owned rig; toggles preserve identities and all other authored/editor values.
/// The scene owns light storage and must outlive this controller.
class SponzaLightRig {
public:
    /// Authors the deterministic rig on first enable, or changes existing lights' enabled flags.
    /// Rejects non-Sponza/foreign scenes and insufficient initial capacity with InvalidDesc.
    /// A failed initial enable leaves no partial rig; removed identities are never recreated.
    rhi::Result<void> setEnabled(Scene& scene, bool enabled);
    /// True when at least one surviving rig light is enabled, including individual editor changes.
    bool enabled() const;
    /// Scene-owned immutable rig identities, including disabled or subsequently removed lights.
    std::span<const LightId> lightIds() const;

private:
    const Scene* m_scene = nullptr;
};

} // namespace lmx::scene
