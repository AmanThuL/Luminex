//----------------------------------------------------------------------------------------------------------------------
/// @file SceneInstantiate.h
/// @brief Declares document preflight, source bindings and transactional scene construction.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Model/GltfLoader.h"
#include "Engine/Asset/Texture/Ibl.h"
#include "Engine/Scene/Scene.h"

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace lmx::engine {

/// Sentinel used by objects generated for the session rather than imported from a document asset.
inline constexpr uint32_t kGeneratedNode = UINT32_MAX;

/// Runtime subjects directly owned by one document node.
struct NodeBinding {
    std::vector<size_t> objects;         ///< All instances beneath this asset or generator root.
    std::optional<LightId> light;        ///< Local light identity, including an authored-off light.
    std::optional<uint32_t> directional; ///< Fixed key/fill/rim slot.
    bool camera = false;                 ///< Whether this is the document's selected camera.
};

/// One imported source node, distinct from its document asset root and generated subjects.
struct ImportedNodeBinding {
    uint32_t assetRoot = 0;      ///< Document node owning the source asset.
    uint32_t sourceNode = 0;     ///< Original glTF node index, including empty nodes.
    int32_t parent = -1;         ///< Source parent index within this asset, or -1.
    std::string name;            ///< Exact source name, used to validate saved overrides.
    bool animated = false;       ///< Includes animated ancestry across every clip.
    bool enabled = true;         ///< Authored own flag; unaffected by ancestor or CLI masks.
    bool effective = true;       ///< Initial AND of document and imported ancestors.
    std::vector<size_t> objects; ///< Every primitive instance of this exact source node.
};

/// Retained local hierarchy and clips for subsequent independent asset playback.
struct BoundAssetAnimation {
    uint32_t documentNode = 0;          ///< Owning document asset root.
    size_t objectBase = 0;              ///< Offset added to source instance indices.
    glm::mat4 rootWorld{1.0f};          ///< Document root transform before source hierarchy.
    std::vector<asset::GltfNode> nodes; ///< Full original source table and rest local poses.
    std::vector<asset::GltfAnimationClip> clips; ///< Original clip order, channels and durations.
};

/// Document and imported identities retained independently of scene row slots.
struct SceneBinding {
    std::vector<NodeBinding> nodes;            ///< One entry per document node.
    std::vector<uint32_t> objectNode;          ///< Document asset root, or kGeneratedNode.
    std::vector<uint32_t> objectImportedNode;  ///< Imported-node binding index, or sentinel.
    std::vector<uint32_t> objectGeneratorNode; ///< Generator root for generated objects only.
    std::unordered_map<uint64_t, uint32_t> lightNode; ///< Packed LightId to document node.
    std::unordered_map<uint64_t, uint32_t>
        lightGeneratorNode;                  ///< Generated light to generator root.
    std::optional<uint32_t> localLightGroup; ///< Top-level group containing authored local lights.
    std::vector<ImportedNodeBinding> importedNodes; ///< Complete source hierarchy per asset.
    std::vector<BoundAssetAnimation> assets; ///< Local animation inputs retained for playback.
    std::vector<bool>
        objectEffective; ///< Initial effective flags applied to objects during instantiation.
};

/// Fully constructed snapshot; publish the whole value together after construction succeeds.
struct LoadedScene {
    std::unique_ptr<Scene> scene; ///< GPU resources; owner waits for retirement before destruction.
    SceneBinding binding;         ///< Stable document/source identity for this scene only.
    asset::SceneDocument document; ///< Loaded authored values, never rewritten by CLI masks.
    std::filesystem::path path;    ///< Document path as supplied by the caller.
    std::string hash;              ///< Loaded on-disk glTF and buffer hash.
};

/// GPU-free decoded asset and separately retained own/effective source-node flags.
struct PreparedDocumentAsset {
    std::filesystem::path path;  ///< Resolved path under the discovered Assets root.
    asset::GltfScene source;     ///< Decoded geometry, source nodes and every clip.
    glm::mat4 rootWorld{1.0f};   ///< Composed document asset transform.
    std::vector<bool> enabled;   ///< Source own flags, default true except explicit overrides.
    std::vector<bool> effective; ///< AND including document ancestry.
};

/// Preflight inputs ready for upload; all referenced files and light conversions have passed.
struct PreparedSceneDocument {
    std::vector<std::optional<PreparedDocumentAsset>> assets; ///< Indexed by document node.
    std::vector<std::optional<LocalLight>> localLights;       ///< Checked local lights by node.
    std::vector<std::optional<DirectionalLight>> directionalLights; ///< Checked directional values.
    std::vector<bool> enabled;                  ///< Effective document-node flags.
    std::optional<asset::ibl::CpuCubemap> hdri; ///< Required HDRI converted for sky/specular.
    std::optional<asset::ibl::CpuCubemap> diffuseHdri; ///< Same source at diffuse face size.
};

/// Attaches the document environment once, at the generator's original resource creation point.
using EnvironmentHook = std::function<asset::AssetResult<void>(Scene&)>;
/// Appends one lab into an unfinished scene; never finalizes or replaces its camera/environment.
using SceneGenerator = std::function<asset::AssetResult<void>(Scene&, const asset::DocGenerator&,
                                                              const EnvironmentHook&)>;
/// Returns a stable callable for a known generator, or null for an unsupported name.
using SceneGeneratorLookup = std::function<const SceneGenerator*(std::string_view)>;

/// Packs every component of a local-light identity without losing its store/generation.
uint64_t sceneLightKey(LightId id);
/// Computes ancestor AND from separate own flags; document hierarchy must already be valid.
std::vector<bool> effectiveDocumentEnabled(const asset::SceneDocument& document,
                                           const std::vector<bool>& own);
/// Resolves decoded relative paths beneath assetsRoot, checks hashes, source overrides and light
/// conversions, and decodes required content without creating any GPU object. The input hierarchy
/// and array indices must already have passed readSceneDocument. Runtime accepts LINEAR selected-
/// camera translation/rotation rails; other document animation and transformed generator roots
/// fail with Unsupported and a field pointer instead of being silently ignored.
asset::AssetResult<PreparedSceneDocument>
prepareSceneDocument(const asset::SceneDocument& document, const std::filesystem::path& assetsRoot);
/// Appends decoded asset content with asset-local mesh handles and rebased tracks, accumulating
/// exact posed vertex bounds; caller attaches the environment and finalizes the shared scene once.
asset::AssetResult<void> appendGltfScene(rojoRHI::Device& device, Scene& scene,
                                         asset::GltfScene& source,
                                         const std::filesystem::path& path, std::string_view label,
                                         Aabb& bounds);
/// Copies document camera and rail using sequential decoded yaw and exact double key times.
void applyDocumentCamera(Scene& scene, const asset::SceneDocument& document);
/// Constructs and finalizes a new scene without mutating the caller's active scene. Asset URIs are
/// resolved beneath the discovered repository Assets root, independent of the document's location.
asset::AssetResult<LoadedScene> instantiateSceneDocument(rojoRHI::Device& device,
                                                         asset::SceneDocument document,
                                                         const std::filesystem::path& path,
                                                         const SceneGeneratorLookup& generators);

} // namespace lmx::engine
