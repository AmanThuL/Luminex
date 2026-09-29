//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocuments.h
/// @brief Declares catalog document loading, generator overrides and authored session state.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Scene/SceneInstantiate.h"
#include <map>

namespace lmx::scenes {
/// Explicit CLI generator changes; absent entries preserve the document's authored parameters.
struct GeneratorOverrides {
    std::optional<uint32_t> instances; ///< VisibilityLab population override.
    std::optional<uint32_t> occluders; ///< VisibilityLab slab override.
    std::optional<uint32_t> lights;    ///< LightLab grid override.
    std::optional<uint32_t> pile;      ///< LightLab pile override.
};
/// Separate authored flags and saved camera; effective rendering/CLI masks never overwrite these.
struct SessionDocumentState {
    std::vector<bool> nodeEnabled;     ///< Document own-enabled flags.
    std::vector<bool> importedEnabled; ///< Imported binding own flags, including empty ancestors.
    /// Immutable load-time world poses by imported binding, absent for animated or empty nodes.
    /// Capture before editing/playback; a subsequently saved document override takes precedence.
    std::vector<std::optional<asset::ObjectPose>> importedPoseBaseline;
    std::optional<engine::SceneCamera>
        sceneCamera; ///< Explicitly changed saved camera, absent initially.
};
/// Copies authored flags and static imported pose baselines before editing or playback begins.
/// Ancestor/CLI masks are excluded; retain this state until its loaded scene is replaced.
SessionDocumentState initialDocumentState(const engine::LoadedScene& loaded);
/// Resolves one known catalog id to its checked-in document under the discovered Assets root.
asset::AssetResult<std::filesystem::path> catalogDocumentPath(std::string_view stableId);
/// Reads a catalog document without GPU creation.
asset::AssetResult<asset::SceneDocument> readCatalogDocument(std::string_view stableId);
/// Validates generator names/parameters, creates their scoped registry, and constructs a snapshot.
asset::AssetResult<engine::LoadedScene> loadSceneDocument(rojoRHI::Device& device,
                                                          const std::filesystem::path& path,
                                                          const GeneratorOverrides& overrides = {});
/// Checks effective generator parameters and combined generated/authored light capacity without GPU
/// use.
asset::AssetResult<void> validateSceneGenerators(const asset::SceneDocument& document,
                                                 const GeneratorOverrides& overrides);
/// Validates every generator and builds callables that append into the supplied unfinished scene.
asset::AssetResult<std::map<std::string, engine::SceneGenerator>>
sceneGenerators(rojoRHI::Device& device, const asset::SceneDocument& document,
                const GeneratorOverrides& overrides);
} // namespace lmx::scenes
