//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocuments.cpp
/// @brief Loads catalog documents and retains authored session defaults.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/SceneDocuments.h"
#include "Engine/Asset/RepositoryAsset.h"
#include "Scenes/SceneLibrary.h"

namespace lmx::scenes {
//======================================================================================================================
SessionDocumentState initialDocumentState(const engine::LoadedScene& loaded) {
    SessionDocumentState state;
    for (const auto& node : loaded.document.nodes)
        state.nodeEnabled.push_back(node.enabled);
    for (const auto& node : loaded.binding.importedNodes) {
        state.importedEnabled.push_back(node.enabled);
        std::optional<asset::ObjectPose> pose;
        if (!node.animated && !node.objects.empty()) {
            const auto& object = loaded.scene->objects.at(node.objects.front());
            pose = {object.position, object.eulerDegrees, object.scale};
        }
        state.importedPoseBaseline.push_back(pose);
    }
    return state;
}
//======================================================================================================================
asset::AssetResult<std::filesystem::path> catalogDocumentPath(std::string_view stableId) {
    if (!parseSceneId(stableId))
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::NotFound,
                              "unknown catalog scene '" + std::string(stableId) + "'"});
    const auto path =
        asset::findRepositoryAsset("Assets/Scenes/" + std::string(stableId) + ".scene.gltf");
    if (!path)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::NotFound,
                              "catalog document '" + std::string(stableId) + "' was not found"});
    return *path;
}
//======================================================================================================================
asset::AssetResult<asset::SceneDocument> readCatalogDocument(std::string_view stableId) {
    const auto path = catalogDocumentPath(stableId);
    if (!path)
        return std::unexpected(path.error());
    return asset::readSceneDocument(*path);
}
//======================================================================================================================
asset::AssetResult<engine::LoadedScene> loadSceneDocument(rojoRHI::Device& device,
                                                          const std::filesystem::path& path,
                                                          const GeneratorOverrides& overrides) {
    auto document = asset::readSceneDocument(path);
    if (!document)
        return std::unexpected(document.error());
    auto registry = sceneGenerators(device, *document, overrides);
    if (!registry)
        return std::unexpected(registry.error());
    const engine::SceneGeneratorLookup lookup =
        [&](std::string_view name) -> const engine::SceneGenerator* {
        const auto found = registry->find(std::string(name));
        return found == registry->end() ? nullptr : &found->second;
    };
    return engine::instantiateSceneDocument(device, std::move(*document), path, lookup);
}
} // namespace lmx::scenes
