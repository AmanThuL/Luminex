//----------------------------------------------------------------------------------------------------------------------
/// @file SponzaLightRig.cpp
/// @brief Authors and toggles deterministic static point and spot lights in Sponza.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Catalog/SponzaLightRig.h"

#include "Core/Color.h"

#include <array>
#include <utility>

namespace lmx::engine {

namespace {

constexpr size_t kRigLightCount = 16;

//======================================================================================================================
std::array<engine::LocalLight, kRigLightCount> rigLights() {
    std::array<engine::LocalLight, kRigLightCount> lights;
    constexpr std::array<float, 4> kColumnX = {-12.0f, -4.0f, 4.0f, 12.0f};
    const glm::vec3 warm = srgbToLinear(glm::vec3(1.0f, 0.72f, 0.45f));
    const glm::vec3 cool = srgbToLinear(glm::vec3(0.45f, 0.7f, 1.0f));
    for (size_t i = 0; i < lights.size(); ++i) {
        auto& light = lights[i];
        const float side = (i / kColumnX.size()) % 2 == 0 ? -1.0f : 1.0f;
        light.colour = i % 2 == 0 ? warm : cool;
        light.position = glm::vec3(kColumnX[i % kColumnX.size()], 2.5f, side * 3.5f);
        light.intensity = 45.0f;
        light.range = 6.0f;
        if (i >= lights.size() / 2) {
            light.type = engine::LocalLightType::Spot;
            light.position.y = 6.0f;
            light.position.z = side * 6.5f;
            light.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -side * 0.6f));
            light.innerCone = glm::radians(22.0f);
            light.outerCone = glm::radians(40.0f);
            light.intensity = 75.0f;
            light.range = 10.0f;
        }
    }
    return lights;
}

} // namespace

//======================================================================================================================
rojoRHI::Result<void> SponzaLightRig::setEnabled(Scene& scene, bool enabled) {
    if (scene.name != "Sponza") {
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Local-light rig is available only in Sponza"});
    }
    if (m_scene && m_scene != &scene) {
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Local-light rig belongs to a different scene"});
    }
    if (scene.m_sponzaLightIds.empty()) {
        if (!enabled)
            return {};
        if (scene.localLights().size() + kRigLightCount > engine::kMaxLocalLights) {
            return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                                  "Sponza rig requires 16 free local-light slots"});
        }
        std::vector<LightId> authored;
        for (const auto& light : rigLights()) {
            const auto id = scene.addLight(light);
            if (!id) {
                for (const auto added : authored)
                    scene.removeLight(added);
                return std::unexpected(id.error());
            }
            authored.push_back(*id);
        }
        scene.m_sponzaLightIds = std::move(authored);
    }
    m_scene = &scene;
    for (const auto id : lightIds()) {
        if (const auto* current = scene.light(id)) {
            auto light = *current;
            light.enabled = enabled;
            if (auto result = scene.updateLight(id, light); !result)
                return result;
        }
    }
    return {};
}

//======================================================================================================================
bool SponzaLightRig::enabled() const {
    for (const auto id : lightIds()) {
        if (const auto* light = m_scene->light(id); light && light->enabled)
            return true;
    }
    return false;
}

//======================================================================================================================
std::span<const LightId> SponzaLightRig::lightIds() const {
    return m_scene ? m_scene->sponzaLightIds() : std::span<const LightId>{};
}

} // namespace lmx::engine
