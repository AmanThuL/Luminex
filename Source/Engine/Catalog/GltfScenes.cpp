//----------------------------------------------------------------------------------------------------------------------
/// @file GltfScenes.cpp
/// @brief Implements the catalog scenes loaded from fetched glTF assets.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Catalog/CatalogScenes.h"

#include "Engine/Asset/RepositoryAsset.h"
#include "Engine/Catalog/SponzaCameraTour.h"
#include "Engine/Catalog/SponzaLightRig.h"
#include "Engine/Scene/Scene.h"

#include <glm/glm.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace lmx::engine {

namespace {

//======================================================================================================================
asset::AssetError missingAsset(std::string_view sceneName, std::string_view relative) {
    return asset::AssetError{asset::AssetErrorCode::NotFound,
                             std::string(sceneName) + " scene: " + std::string(relative) +
                                 " not found from this working directory -- run `xmake setup`"};
}

//======================================================================================================================
// Resolves a catalog scene's repo-relative asset before handing it to the public loader, so a
// missing fetch reports the `xmake setup` hint instead of a bare file-not-found.
asset::AssetResult<std::unique_ptr<Scene>>
loadCatalogGltfScene(rojoRHI::Device& device, std::string_view relativeAssetPath,
                     std::string_view sceneName, const SceneAuthoring& beforeFinalize = {}) {
    const auto path = asset::findRepositoryAsset(relativeAssetPath);
    if (!path) {
        return std::unexpected(missingAsset(sceneName, relativeAssetPath));
    }
    return loadGltfScene(device, path->string(), sceneName, beforeFinalize);
}

//======================================================================================================================
// Authors Sponza's static local-light rig, enabled, before the scene is finalized.
rojoRHI::Result<void> authorSponzaLightRig(Scene& scene) {
    SponzaLightRig rig;
    return rig.setEnabled(scene, true);
}

} // namespace

//======================================================================================================================
asset::AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rojoRHI::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/Sponza/Sponza.gltf", "Sponza",
                                      authorSponzaLightRig);
    if (!scene) {
        return std::unexpected(scene.error());
    }

    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.05f;
    (*scene)->initialCamera.farZ = (*scene)->boundingSphere.w * 20.0f;
    authorSponzaCameraTour(**scene);

    return std::move(*scene);
}

//======================================================================================================================
asset::AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rojoRHI::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/DamagedHelmet/DamagedHelmet.glb",
                                      "DamagedHelmet");
    if (!scene) {
        return std::unexpected(scene.error());
    }

    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    // A +Z showcase view looks toward the model with the default -Z forward vector.
    (*scene)->initialCamera.position = center + glm::vec3(0.0f, 0.0f, radius * 2.5f);
    (*scene)->initialCamera.yaw = 0.0f;
    (*scene)->initialCamera.pitch = 0.0f;
    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.01f;
    (*scene)->initialCamera.farZ = radius * 20.0f;

    return std::move(*scene);
}

//======================================================================================================================
asset::AssetResult<std::unique_ptr<Scene>> loadMilkTruckScene(rojoRHI::Device& device) {
    auto scene = loadCatalogGltfScene(device, "Assets/Fetched/CesiumMilkTruck/CesiumMilkTruck.glb",
                                      "CesiumMilkTruck");
    if (!scene) {
        return std::unexpected(scene.error());
    }

    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    // A +Z showcase view looks toward the model with the default -Z forward vector.
    (*scene)->initialCamera.position = center + glm::vec3(0.0f, 0.0f, radius * 2.5f);
    (*scene)->initialCamera.yaw = 0.0f;
    (*scene)->initialCamera.pitch = 0.0f;
    (*scene)->initialCamera.fovY = glm::radians(45.0f);
    (*scene)->initialCamera.nearZ = 0.01f;
    (*scene)->initialCamera.farZ = radius * 20.0f;

    return std::move(*scene);
}

} // namespace lmx::engine
