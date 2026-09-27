//----------------------------------------------------------------------------------------------------------------------
/// @file Generators.cpp
/// @brief Checks document generator parameters and binds append-only lab functions.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/CatalogScenes.h"
#include "Scenes/SceneDocuments.h"
#include <cmath>

namespace lmx::scenes {
namespace {
//======================================================================================================================
asset::AssetResult<uint32_t> parameter(const asset::DocGenerator& generator, std::string_view name,
                                       uint32_t fallback, uint32_t minimum, uint32_t maximum) {
    for (const auto& [key, value] : generator.params) {
        if (key != name)
            continue;
        if (!std::isfinite(value) || std::floor(value) != value || value < minimum ||
            value > maximum)
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                     std::string(name) + " must be an integer in " +
                                                         std::to_string(minimum) + ".." +
                                                         std::to_string(maximum)});
        return static_cast<uint32_t>(value);
    }
    return fallback;
}
} // namespace
//======================================================================================================================
asset::AssetResult<void> validateSceneGenerators(const asset::SceneDocument& document,
                                                 const GeneratorOverrides& overrides) {
    uint64_t totalLights = 0;
    for (size_t n = 0; n < document.nodes.size(); ++n) {
        const auto& node = document.nodes[n];
        if (node.light) {
            const auto& light = document.lights[*node.light];
            if (light.type != asset::DocLightType::Directional && light.range)
                ++totalLights;
            if (totalLights > engine::kMaxLocalLights)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Malformed,
                                      "/nodes/" + std::to_string(n) +
                                          "/extensions/KHR_lights_punctual/light: combined local "
                                          "light capacity exceeds 4096"});
        }
        if (!node.generator)
            continue;
        const auto& g = *node.generator;
        const std::string pointer =
            "/nodes/" + std::to_string(n) + "/extensions/LMX_scene/generator";
        if (g.name != "material-lab" && g.name != "temporal-lab" && g.name != "visibility-lab" &&
            g.name != "light-lab")
            return std::unexpected(
                asset::AssetError{asset::AssetErrorCode::Unsupported,
                                  pointer + "/name: unknown generator '" + g.name + "'"});
        for (const auto& [name, value] : g.params) {
            const bool allowed =
                (g.name == "visibility-lab" && (name == "instances" || name == "occluders")) ||
                (g.name == "light-lab" && (name == "lights" || name == "pile"));
            if (!allowed)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Malformed,
                                      pointer + "/params/" + name + ": unknown parameter"});
            const auto checked =
                parameter(g, name, 0, name == "instances" || name == "lights" ? 1 : 0,
                          name == "instances"   ? 1048576
                          : name == "occluders" ? 1024
                                                : engine::kMaxLocalLights);
            if (!checked)
                return std::unexpected(
                    asset::AssetError{checked.error().code, pointer + "/params/" + name + ": " +
                                                                checked.error().message});
        }
        if (g.name == "light-lab") {
            const uint64_t lights =
                overrides.lights.value_or(*parameter(g, "lights", 256, 1, engine::kMaxLocalLights));
            const uint64_t pile =
                overrides.pile.value_or(*parameter(g, "pile", 0, 0, engine::kMaxLocalLights));
            if (lights == 0 || lights + pile > engine::kMaxLocalLights)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Malformed,
                                      pointer + "/params: lights plus pile must be 1..4096"});
            totalLights += lights + pile;
            if (totalLights > engine::kMaxLocalLights)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Malformed,
                                      pointer + "/params: combined generated and authored local "
                                                "light capacity exceeds 4096"});
        }
        if (g.name == "visibility-lab" &&
            ((overrides.instances &&
              (*overrides.instances == 0 || *overrides.instances > 1048576)) ||
             (overrides.occluders && *overrides.occluders > 1024)))
            return std::unexpected(
                asset::AssetError{asset::AssetErrorCode::Malformed,
                                  pointer + "/params: invalid VisibilityLab override"});
    }
    return {};
}
//======================================================================================================================
asset::AssetResult<std::map<std::string, engine::SceneGenerator>>
sceneGenerators(rojoRHI::Device& device, const asset::SceneDocument& document,
                const GeneratorOverrides& overrides) {
    if (auto checked = validateSceneGenerators(document, overrides); !checked)
        return std::unexpected(checked.error());
    std::map<std::string, engine::SceneGenerator> result;
    result["material-lab"] = [&device](engine::Scene& scene, const asset::DocGenerator&,
                                       const engine::EnvironmentHook& environment) {
        return appendMaterialLab(device, scene, environment);
    };
    result["temporal-lab"] = [&device](engine::Scene& scene, const asset::DocGenerator&,
                                       const engine::EnvironmentHook& environment) {
        return appendTemporalLab(device, scene, environment);
    };
    result["visibility-lab"] = [&device, overrides](engine::Scene& scene,
                                                    const asset::DocGenerator& g,
                                                    const engine::EnvironmentHook& environment) {
        return appendVisibilityLab(
            device, scene,
            overrides.instances.value_or(*parameter(g, "instances", 4096, 1, 1048576)),
            overrides.occluders.value_or(*parameter(g, "occluders", 0, 0, 1024)), environment);
    };
    result["light-lab"] = [overrides](engine::Scene& scene, const asset::DocGenerator& g,
                                      const engine::EnvironmentHook& environment) {
        return appendLightLab(
            scene,
            overrides.lights.value_or(*parameter(g, "lights", 256, 1, engine::kMaxLocalLights)),
            overrides.pile.value_or(*parameter(g, "pile", 0, 0, engine::kMaxLocalLights)),
            environment);
    };
    return result;
}
} // namespace lmx::scenes
