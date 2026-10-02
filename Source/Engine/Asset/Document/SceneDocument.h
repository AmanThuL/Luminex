//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocument.h
/// @brief Declares CPU scene-document data, deterministic serialization and checked disk I/O.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Asset/Document/SceneDocumentContent.h"
#include "Engine/Asset/Document/SceneLook.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::asset {

/// Editable world pose of an imported object, expressed in the editor's own Euler convention.
struct ObjectPose {
    glm::vec3 translation{0.0f};  ///< World-space translation in metres.
    glm::vec3 eulerDegrees{0.0f}; ///< World-space XYZ Euler angles in degrees.
    glm::vec3 scale{1.0f};        ///< World-space scale.
};

/// Authored changes applying to every primitive instance of one imported glTF node.
struct DocOverride {
    uint32_t node = 0;              ///< Original glTF node index, including empty ancestors.
    std::string name;               ///< Expected original name; mismatches fail instantiation.
    std::optional<bool> enabled;    ///< Authored own-enabled state, independent of ancestors.
    std::optional<ObjectPose> pose; ///< World pose; animated nodes reject pose overrides.
};

/// Referenced source asset; meshes and material data stay in this external file.
struct DocAsset {
    /// Decoded filesystem path relative to Assets/; JSON reading/writing decodes/encodes once.
    std::string uri;
    std::string sha256; ///< Lowercase SHA-256 of the referenced file.
};

/// Lab registry entry with insertion-ordered numeric parameters.
struct DocGenerator {
    std::string name;                                   ///< Generator registry name.
    std::vector<std::pair<std::string, double>> params; ///< Ordered parameter names and values.
};

/// Punctual-light type in the glTF CPU vocabulary.
enum class DocLightType {
    Directional, ///< Infinite light with a node-local -Z direction.
    Point,       ///< Finite-range point light.
    Spot,        ///< Finite-range cone with a node-local -Z direction.
};

/// Standard KHR_lights_punctual definition; placement and enabled state belong to its node.
struct DocLight {
    std::string name;                              ///< Authored light name.
    DocLightType type = DocLightType::Directional; ///< Punctual-light type.
    glm::dvec3 colour{1.0};                        ///< Linear RGB in [0,1].
    double intensity = 1.0;                        ///< Nonnegative luminous intensity/illuminance.
    std::optional<float> range;                    ///< Positive metres; required for local lights.
    float innerCone = 0.0f;                        ///< Spot inner cone, radians.
    float outerCone = 0.7853981633974483f;         ///< Spot outer cone, radians.
};

/// Standard perspective camera; its transform belongs to the referencing document node.
struct DocCamera {
    std::string name;                   ///< Authored camera name.
    float fovY = 1.0471975511965976f;   ///< Vertical field of view in radians.
    float nearZ = 0.1f;                 ///< Positive near distance in metres.
    std::optional<float> farZ = 100.0f; ///< Optional far plane; absence means infinite.
    std::optional<float> aspectRatio;   ///< Optional fixed viewport aspect.
};

/// Document hierarchy node; indices reference the owning SceneDocument arrays.
struct DocNode {
    std::string name;               ///< Display name.
    std::vector<uint32_t> children; ///< Ordered child node indices.
    glm::vec3 translation{0.0f};    ///< Node-local metres.
    /// Node-local glTF orientation, not normalized on read.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};                 ///< Node-local per-axis scale.
    std::optional<uint32_t> camera;        ///< Perspective camera index.
    std::optional<uint32_t> light;         ///< Punctual-light index.
    std::optional<uint32_t> mesh;          ///< Saved mesh index; schema 2 only.
    DocMotion motion = DocMotion::Rigid;   ///< Authored motion-vector behavior.
    bool enabled = true;                   ///< Authored own-enabled state.
    std::optional<DocAsset> asset;         ///< External asset instantiated below this node.
    std::vector<DocOverride> overrides;    ///< Imported-node edits, valid only with asset.
    std::optional<DocGenerator> generator; ///< Procedural lab instantiated below this node.
    std::optional<std::string> role;       ///< Directional role: key, fill or rim.
    bool castsShadow = false;              ///< Selects the one directional shadow caster.
};

/// Supported glTF target of a uniformly sampled document channel.
enum class DocChannelPath {
    Translation,      ///< XYZ node-local position; fourth sample component is zero.
    Rotation,         ///< XYZW quaternion components, retained exactly.
    Scale,            ///< XYZ node-local scale; fourth sample component is zero.
    EmissiveStrength, ///< Scalar material emission multiplier; other sample components are zero.
};

/// One standard animation channel with samples in source order.
struct DocChannel {
    uint32_t node = 0;                                 ///< Target document node.
    DocChannelPath path = DocChannelPath::Translation; ///< Target transform component.
    /// Material index for EmissiveStrength only; node is unused for that path.
    std::optional<uint32_t> material;
    bool step = false;             ///< STEP interpolation; otherwise LINEAR.
    std::vector<glm::vec4> values; ///< Exactly DocAnimation::keyCount samples.
};

/// Uniform animation clock; time of key k is exactly double(k)/sampleRate.
struct DocAnimation {
    std::string name;         ///< Authored clip name used in diagnostics.
    double sampleRate = 60.0; ///< Positive samples per second.
    uint32_t keyCount = 0;    ///< Shared number of samples in all channels.
    /// Source order; each node/path or emissive-material target occurs at most once in this clip.
    std::vector<DocChannel> channels;
};

/// Owned glTF scene-document model with no GPU objects or live editor state.
struct SceneDocument {
    uint32_t schemaVersion = 1;                ///< Supported LMX_scene schema version.
    std::string name;                          ///< glTF scene label.
    uint32_t camera = 0;                       ///< Initial camera's document node index.
    bool loop = true;                          ///< Whether the one scene clock loops.
    SceneLook look;                            ///< Authored look.
    std::vector<uint32_t> rootNodes;           ///< Ordered scene roots.
    std::vector<DocNode> nodes;                ///< Original document node order.
    std::vector<DocCamera> cameras;            ///< Standard perspective camera definitions.
    std::vector<DocLight> lights;              ///< Standard punctual lights in document order.
    std::vector<DocAnimation> animations;      ///< Standard animation clips in source order.
    std::vector<DocMesh> meshes;               ///< Saved meshes in source order.
    std::vector<DocMaterial> materials;        ///< Saved material rows in source order.
    std::shared_ptr<const DocContent> content; ///< Immutable geometry/images; null without meshes.
    std::optional<std::pair<glm::vec3, glm::vec3>> bounds; ///< Authored scene-space min/max bounds.
    std::vector<std::string> warnings; ///< Read diagnostics; excluded from canonical output.
    /// Validated decoded buffer URI from the read source. Provenance only: excluded from canonical
    /// output and dirty comparison; successful save adoption replaces it with the saved source URI.
    std::optional<std::string> sourceBufferUri;
};

/// Reads and validates a document, animation buffer and hash-verified schema 2 geometry/images.
/// Geometry uses consecutive vertices/indices in physical buffer order with no unused bytes;
/// PNGs sit directly in the texture folder with unique safe names matching filename stems.
/// PNG pixels are decoded from the verified byte snapshot. All malformed
/// field errors name a JSON pointer; skipped unbounded local lights produce one warning each.
/// Referenced asset/HDRI URIs and hashes are syntax-checked here; instantiation resolves the files
/// and verifies their content hashes before creating GPU resources.
AssetResult<SceneDocument> readSceneDocument(const std::filesystem::path& path);
/// Returns canonical glTF JSON for a valid model. bufferUri is a decoded relative filesystem path;
/// this function percent-encodes it once, just like asset/HDRI paths stored in the model.
std::string sceneDocumentJson(const SceneDocument& doc, std::string_view bufferUri);
/// Checks the model invariants a write depends on: finite numbers, valid enums, bounded local
/// lights and consistent animations. Errors name a JSON pointer; sceneDocumentJson requires
/// success.
AssetResult<void> validateSceneDocumentModel(const SceneDocument& doc);
/// Returns standard little-endian FLOAT animation data in deterministic accessor order.
std::vector<std::byte> sceneDocumentBuffer(const SceneDocument& doc);
/// Returns immutable vertices then indices for each geometry, four-byte aligned in model order.
/// Empty when content is absent; preserves the native little-endian VertexPNTU bytes.
std::vector<std::byte> sceneDocumentGeometry(const SceneDocument& doc);
/// Stages the glTF and, for animated documents only, its companion beside path, checks target
/// permissions and rolls back reported replacement failures. An existing companion that the target
/// document does not reference is never overwritten. This is not a crash-atomic transaction.
/// Geometry and PNG companions are installed only when absent; existing hashes must match.
/// All companion conflicts fail before staging; newly installed files participate in rollback.
/// Invalid models and filesystem errors fail.
AssetResult<void> saveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path);
/// Returns the decoded path of a glTF's external animation buffer without reading that file.
/// No animation buffer returns no path, including a schema 2 geometry-only document. Schema 2
/// permits only the named animation/geometry pair; malformed JSON, buffer shape or URI fails.
AssetResult<std::optional<std::filesystem::path>>
sceneDocumentBufferPath(std::string_view gltfJson, const std::filesystem::path& document);
/// Hashes on-disk glTF bytes followed only by the referenced animation bytes, if present.
/// Geometry and images are covered by recorded hashes; their files are not read here.
AssetResult<std::string> sceneDocumentHash(const std::filesystem::path& path);

} // namespace lmx::asset
