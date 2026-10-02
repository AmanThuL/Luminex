//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentPrepare.cpp
/// @brief Validates referenced document content and checked light conversions before GPU creation.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/SceneInstantiate.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Image/HdrEnvironment.h"
#include "Engine/Lights/LocalLightMath.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <optional>
#include <utility>

namespace lmx::engine {
namespace {
//======================================================================================================================
asset::AssetError malformed(std::string pointer, std::string message) {
    return {asset::AssetErrorCode::Malformed, std::move(pointer) + ": " + std::move(message)};
}
//======================================================================================================================
asset::AssetResult<std::filesystem::path> checkedReference(const std::filesystem::path& root,
                                                           const std::string& uri,
                                                           const std::string& hash,
                                                           const std::string& pointer) {
    const std::filesystem::path relative(uri);
    if (relative.is_absolute() || relative.empty())
        return std::unexpected(
            malformed(pointer + "/uri", "expected a relative path under Assets"));
    for (const auto& part : relative)
        if (part == "..")
            return std::unexpected(malformed(pointer + "/uri", "path leaves Assets"));
    const auto path = root / relative;
    const auto bytes = readWholeFile(path);
    if (!bytes)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::NotFound,
                              pointer + "/uri: '" + path.string() +
                                  "' could not be read; run xmake setup for fetched assets"});
    if (sha256Hex(*bytes) != hash)
        return std::unexpected(
            malformed(pointer + "/sha256", "content hash differs for '" + path.string() + "'"));
    return path;
}
//======================================================================================================================
std::vector<int32_t> parents(const asset::SceneDocument& document) {
    std::vector<int32_t> result(document.nodes.size(), -1);
    for (size_t node = 0; node < document.nodes.size(); ++node)
        for (uint32_t child : document.nodes[node].children) {
            LMX_ASSERT(child < result.size(), "document hierarchy must be validated");
            result[child] = static_cast<int32_t>(node);
        }
    return result;
}
//======================================================================================================================
std::optional<std::pair<double, double>> nearZeroInterval(double start, double end) {
    constexpr double kTolerance = 1e-4; // decomposeTransform's degenerate-axis threshold.
    if (start == end) {
        if (std::abs(start) <= kTolerance)
            return std::pair{0.0, 1.0};
        return std::nullopt;
    }
    const double a = (-kTolerance - start) / (end - start);
    const double b = (kTolerance - start) / (end - start);
    const double low = std::max(0.0, std::min(a, b));
    const double high = std::min(1.0, std::max(a, b));
    if (low <= high)
        return std::pair{low, high};
    return std::nullopt;
}
//======================================================================================================================
bool supportedAnimatedScale(glm::vec3 scale) {
    // Document rigid playback uses the Inspector/gizmo authoring domain. Preserve signed and
    // zero scales; the separate collapse check handles degeneracy. LINEAR interpolation stays
    // inside its endpoint magnitudes, without clamping or rewriting authored keys.
    for (int k = 0; k < 3; ++k)
        if (!(std::abs(scale[k]) <= 100.0f))
            return false;
    return true;
}
//======================================================================================================================
bool collapsedScale(glm::vec3 start, glm::vec3 end) {
    for (int first = 0; first < 3; ++first) {
        const auto a = nearZeroInterval(start[first], end[first]);
        if (!a)
            continue;
        for (int second = first + 1; second < 3; ++second) {
            const auto b = nearZeroInterval(start[second], end[second]);
            if (b && std::max(a->first, b->first) <= std::min(a->second, b->second))
                return true;
        }
    }
    return false;
}
//======================================================================================================================
asset::AssetResult<void> prepareMeshContent(const asset::SceneDocument& doc,
                                            const std::vector<int32_t>& parent) {
    const auto unsupported = [](std::string pointer,
                                std::string reason) -> asset::AssetResult<void> {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Unsupported,
                                                 std::move(pointer) + ": " + std::move(reason)});
    };
    if (!doc.meshes.empty() && !doc.content)
        return std::unexpected(malformed("/meshes", "meshes require decoded document content"));
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
        const auto& meshNode = doc.nodes[i];
        if (!meshNode.mesh)
            continue;
        const std::string pointer = "/nodes/" + std::to_string(i);
        if (meshNode.asset || meshNode.generator || meshNode.camera || meshNode.light)
            return unsupported(pointer + "/mesh",
                               "a mesh node cannot also own an asset, generator, camera or light");
        for (int32_t p = parent[i]; p >= 0; p = parent[static_cast<size_t>(p)]) {
            const auto& ancestor = doc.nodes[static_cast<size_t>(p)];
            const std::string at = "/nodes/" + std::to_string(p);
            if (ancestor.mesh || ancestor.asset || ancestor.generator || ancestor.camera ||
                ancestor.light)
                return unsupported(at, "mesh ancestors must be identity group nodes");
            if (ancestor.translation != glm::vec3(0))
                return unsupported(at + "/translation",
                                   "mesh ancestors must have identity transforms");
            if (ancestor.rotation != glm::quat(1, 0, 0, 0))
                return unsupported(at + "/rotation",
                                   "mesh ancestors must have identity transforms");
            if (ancestor.scale != glm::vec3(1))
                return unsupported(at + "/scale", "mesh ancestors must have identity transforms");
        }
    }
    if (!doc.content)
        return {};
    std::vector<std::optional<bool>> colorUse(doc.content->images.size());
    for (size_t i = 0; i < doc.materials.size(); ++i) {
        const auto& values = doc.materials[i].values;
        const std::string path = "/materials/" + std::to_string(i);
        const auto use = [&](int index, bool color,
                             std::string_view slot) -> asset::AssetResult<void> {
            if (index < 0)
                return {};
            auto& previous = colorUse[static_cast<size_t>(index)];
            if (previous && *previous != color)
                return unsupported(
                    path + std::string(slot),
                    "one document image cannot serve both color and data texture slots");
            previous = color;
            return {};
        };
        if (auto r = use(values.baseColorImage, true, "/pbrMetallicRoughness/baseColorTexture"); !r)
            return r;
        if (auto r = use(values.emissiveImage, true, "/emissiveTexture"); !r)
            return r;
        if (auto r = use(values.normalImage, false, "/normalTexture"); !r)
            return r;
        if (auto r = use(values.metallicRoughnessImage, false,
                         "/pbrMetallicRoughness/metallicRoughnessTexture");
            !r)
            return r;
        if (auto r = use(values.occlusionImage, false, "/occlusionTexture"); !r)
            return r;
    }
    return {};
}
//======================================================================================================================
asset::AssetResult<void> prepareDocumentAnimations(const asset::SceneDocument& doc,
                                                   const std::vector<int32_t>& parent) {
    struct MeshClock {
        double sampleRate;
        uint32_t keyCount;
        bool step;
        const asset::DocChannel* scale = nullptr;
        std::string pointer;
        uint32_t paths = 0;
    };
    std::vector<std::optional<MeshClock>> clocks(doc.nodes.size());
    std::vector<bool> emissive(doc.materials.size(), false);
    for (size_t a = 0; a < doc.animations.size(); ++a) {
        const auto& animation = doc.animations[a];
        for (size_t c = 0; c < animation.channels.size(); ++c) {
            const auto& channel = animation.channels[c];
            const std::string pointer =
                "/animations/" + std::to_string(a) + "/channels/" + std::to_string(c);
            if (channel.path == asset::DocChannelPath::EmissiveStrength) {
                if (emissive[*channel.material])
                    return std::unexpected(
                        asset::AssetError{asset::AssetErrorCode::Unsupported,
                                          pointer + "/target: a material can have only one "
                                                    "emissive channel across document animations"});
                emissive[*channel.material] = true;
                continue;
            }
            if (doc.nodes[channel.node].mesh) {
                if (parent[channel.node] >= 0)
                    return std::unexpected(asset::AssetError{
                        asset::AssetErrorCode::Unsupported,
                        pointer + "/target/node: animated mesh nodes must be scene roots"});
                auto& clock = clocks[channel.node];
                if (clock && (clock->sampleRate != animation.sampleRate ||
                              clock->keyCount != animation.keyCount || clock->step != channel.step))
                    return std::unexpected(
                        asset::AssetError{asset::AssetErrorCode::Unsupported,
                                          pointer + "/sampler: a mesh node's TRS channels require "
                                                    "one key grid and interpolation mode"});
                if (!clock)
                    clock = MeshClock{animation.sampleRate, animation.keyCount, channel.step,
                                      nullptr, pointer};
                const uint32_t pathBit = 1u << static_cast<uint32_t>(channel.path);
                if (clock->paths & pathBit)
                    return std::unexpected(
                        asset::AssetError{asset::AssetErrorCode::Unsupported,
                                          pointer + "/target/path: a mesh TRS path can appear only "
                                                    "once across document animations"});
                clock->paths |= pathBit;
                if (channel.path == asset::DocChannelPath::Scale) {
                    clock->scale = &channel;
                    clock->pointer = pointer;
                }
                continue;
            }
            if (channel.node != doc.camera)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Unsupported,
                                      pointer + "/target/node: runtime document animation supports "
                                                "only the selected camera or mesh nodes"});
            if (channel.path == asset::DocChannelPath::Scale)
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Unsupported,
                    pointer + "/target/path: camera scale animation is not supported"});
            if (channel.step)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Unsupported,
                                      pointer + "/sampler: camera rails require LINEAR translation "
                                                "and yaw/pitch interpolation"});
        }
    }
    for (size_t n = 0; n < clocks.size(); ++n) {
        const auto& clock = clocks[n];
        if (!clock)
            continue;
        bool collapsed = false;
        if (!clock->scale) {
            if (!supportedAnimatedScale(doc.nodes[n].scale))
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Unsupported,
                    "/nodes/" + std::to_string(n) +
                        "/scale: animated scale components must be in [-100, 100]"});
            collapsed = collapsedScale(doc.nodes[n].scale, doc.nodes[n].scale);
        } else {
            const auto& keys = clock->scale->values;
            for (size_t k = 0; k < keys.size(); ++k) {
                const auto start = glm::vec3(keys[k]);
                if (!supportedAnimatedScale(start))
                    return std::unexpected(asset::AssetError{
                        asset::AssetErrorCode::Unsupported,
                        clock->pointer +
                            "/sampler: animated scale key components must be in [-100, 100]"});
                const auto end = glm::vec3(keys[clock->step || k + 1 == keys.size() ? k : k + 1]);
                collapsed = collapsed || collapsedScale(start, end);
            }
        }
        if (collapsed)
            return std::unexpected(asset::AssetError{
                asset::AssetErrorCode::Unsupported,
                clock->pointer + ": rigid scale keys or interpolation can collapse two axes"});
    }
    return {};
}
//======================================================================================================================
asset::AssetResult<void> prepareLights(const asset::SceneDocument& document,
                                       PreparedSceneDocument& prepared) {
    for (size_t i = 0; i < document.nodes.size(); ++i) {
        const auto& node = document.nodes[i];
        if (!node.light)
            continue;
        const auto& light = document.lights[*node.light];
        const std::string pointer =
            "/extensions/KHR_lights_punctual/lights/" + std::to_string(*node.light);
        if (!std::isfinite(light.intensity) || light.intensity < 0)
            return std::unexpected(
                malformed(pointer + "/intensity", "expected finite nonnegative intensity"));
        if (light.type == asset::DocLightType::Directional) {
            const glm::vec3 strength = asset::decodeStrength({light.colour, light.intensity});
            if (!isFinite(strength))
                return std::unexpected(malformed(
                    pointer + "/intensity", "directional strength exceeds finite float range"));
            prepared.directionalLights[i] = DirectionalLight{
                strength, asset::directionForRotation(node.rotation), prepared.enabled[i]};
        } else {
            if (!light.range)
                continue;
            if (light.intensity > std::numeric_limits<float>::max())
                return std::unexpected(malformed(
                    pointer + "/intensity", "local-light intensity exceeds finite float range"));
            LocalLight converted{.type = light.type == asset::DocLightType::Spot
                                             ? LocalLightType::Spot
                                             : LocalLightType::Point,
                                 .position = node.translation,
                                 .colour = glm::vec3(light.colour),
                                 .intensity = static_cast<float>(light.intensity),
                                 .range = *light.range,
                                 .direction = asset::directionForRotation(node.rotation),
                                 .innerCone = light.innerCone,
                                 .outerCone = light.outerCone,
                                 .enabled = prepared.enabled[i]};
            // Validate as enabled as well: disabled rows must not conceal overflowing products.
            auto checked = converted;
            checked.enabled = true;
            const auto row = makeLightRow(checked);
            if (!row) {
                const auto suffix = row.error().message.find("innerCone") != std::string::npos
                                        ? "/spot/innerConeAngle"
                                    : row.error().message.find("outerCone") != std::string::npos
                                        ? "/spot/outerConeAngle"
                                        : "/range";
                return std::unexpected(malformed(pointer + suffix, row.error().message));
            }
            if (!isFinite(row->strength))
                return std::unexpected(malformed(
                    pointer + "/intensity", "decoded light strength exceeds finite float range"));
            if (!isFinite(row->boundCentre) || !std::isfinite(row->boundRadius))
                return std::unexpected(malformed(pointer + "/range",
                                                 "decoded light bounds exceed finite float range"));
            prepared.localLights[i] = converted;
        }
    }
    return {};
}
} // namespace

//======================================================================================================================
uint64_t sceneLightKey(LightId id) {
    return (uint64_t{id.store} << 48) | (uint64_t{id.generation} << 32) | id.slot;
}
//======================================================================================================================
std::vector<bool> effectiveDocumentEnabled(const asset::SceneDocument& document,
                                           const std::vector<bool>& own) {
    LMX_ASSERT(own.size() == document.nodes.size(), "one own-enabled flag per document node");
    const auto parent = parents(document);
    std::vector<bool> result(own.size());
    for (size_t i = 0; i < own.size(); ++i) {
        bool enabled = own[i];
        for (int32_t p = parent[i]; p >= 0; p = parent[static_cast<size_t>(p)])
            enabled = enabled && own[static_cast<size_t>(p)];
        result[i] = enabled;
    }
    return result;
}
//======================================================================================================================
asset::AssetResult<void> validateIndependentAssetClips(const asset::GltfScene& source,
                                                       const glm::mat4& rootWorld,
                                                       std::string_view assetPointer) {
    const auto ancestorOf = [&](uint32_t ancestor, uint32_t descendant) {
        for (int32_t p = source.nodes[descendant].parent; p >= 0;
             p = source.nodes[static_cast<size_t>(p)].parent) {
            if (static_cast<uint32_t>(p) == ancestor)
                return true;
        }
        return false;
    };
    const auto affectsInstance = [&](uint32_t node) {
        for (const auto& instance : source.instances)
            if (instance.node == node || ancestorOf(node, instance.node))
                return std::optional<uint32_t>(instance.node);
        return std::optional<uint32_t>{};
    };
    const auto nonuniform = [](const asset::GltfAnimationChannel& channel) {
        for (const auto& key : channel.keys) {
            const auto scale = glm::vec3(key.value);
            if (std::abs(scale.x - scale.y) > 1e-5f || std::abs(scale.y - scale.z) > 1e-5f)
                return true;
        }
        return false;
    };
    const auto nonuniformMatrix = [](const glm::mat4& matrix) {
        const float x = glm::length(glm::vec3(matrix[0]));
        const float y = glm::length(glm::vec3(matrix[1]));
        const float z = glm::length(glm::vec3(matrix[2]));
        return std::abs(x - y) > 1e-5f || std::abs(y - z) > 1e-5f ||
               std::abs(glm::dot(glm::vec3(matrix[0]), glm::vec3(matrix[1]))) > 1e-5f ||
               std::abs(glm::dot(glm::vec3(matrix[1]), glm::vec3(matrix[2]))) > 1e-5f ||
               std::abs(glm::dot(glm::vec3(matrix[0]), glm::vec3(matrix[2]))) > 1e-5f;
    };
    const auto staticAnisotropyAbove = [&](uint32_t node) {
        if (nonuniformMatrix(rootWorld))
            return true;
        for (int32_t p = source.nodes[node].parent; p >= 0;
             p = source.nodes[static_cast<size_t>(p)].parent) {
            const auto& ancestor = source.nodes[static_cast<size_t>(p)];
            if (ancestor.matrix ? nonuniformMatrix(*ancestor.matrix)
                                : std::abs(ancestor.scale.x - ancestor.scale.y) > 1e-5f ||
                                      std::abs(ancestor.scale.y - ancestor.scale.z) > 1e-5f)
                return true;
        }
        return false;
    };
    const auto label = [&](uint32_t node) {
        const auto& name = source.nodes[node].name;
        return name.empty() ? std::to_string(node)
                            : "'" + name + "' (" + std::to_string(node) + ")";
    };
    for (const auto& clip : source.clips) {
        for (const auto& channel : clip.channels) {
            if (channel.path != asset::GltfAnimationPath::Scale || !affectsInstance(channel.node))
                continue;
            // STEP keys and a lone key are constant segments: each key is a zero-length interval.
            const bool pointwise = channel.step || channel.keys.size() == 1;
            for (size_t i = pointwise ? 0 : 1; i < channel.keys.size(); ++i) {
                const auto& a = channel.keys[pointwise ? i : i - 1].value;
                const auto& b = channel.keys[i].value;
                for (int first = 0; first < 3; ++first) {
                    const auto firstZero = nearZeroInterval(a[first], b[first]);
                    if (!firstZero)
                        continue;
                    for (int second = first + 1; second < 3; ++second) {
                        const auto secondZero = nearZeroInterval(a[second], b[second]);
                        if (secondZero && std::max(firstZero->first, secondZero->first) <=
                                              std::min(firstZero->second, secondZero->second))
                            return std::unexpected(
                                asset::AssetError{asset::AssetErrorCode::Unsupported,
                                                  std::string(assetPointer) + ": local scale " +
                                                      (pointwise ? "key" : "interpolation") +
                                                      " can collapse two axes at source node " +
                                                      label(channel.node)});
                    }
                }
            }
        }
    }
    // The local sampler interpolates between baked keys and wraps each clip separately. A baked
    // shared-clock proof cannot cover those phases, even with just one short clip. Reject the
    // structural scale-above-rotation hazard instead of relying on a finite sampling grid.
    for (const auto& clip : source.clips) {
        for (const auto& rotation : clip.channels) {
            if (rotation.path != asset::GltfAnimationPath::Rotation)
                continue;
            const auto affected = affectsInstance(rotation.node);
            if (!affected)
                continue;
            if (staticAnisotropyAbove(rotation.node))
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Unsupported,
                    std::string(assetPointer) + ": local clip interpolation can shear source " +
                        "node " + label(rotation.node) + " affecting " + label(*affected) +
                        " beneath static nonuniform ancestry"});
            for (const auto& scaleClip : source.clips) {
                for (const auto& scale : scaleClip.channels) {
                    if (scale.path != asset::GltfAnimationPath::Scale || !nonuniform(scale) ||
                        !ancestorOf(scale.node, rotation.node))
                        continue;
                    return std::unexpected(asset::AssetError{
                        asset::AssetErrorCode::Unsupported,
                        std::string(assetPointer) + ": local clip interpolation can shear source " +
                            "node " + label(rotation.node) + " beneath nonuniform scale at " +
                            label(scale.node)});
                }
            }
        }
    }
    return {};
}
//======================================================================================================================
asset::AssetResult<PreparedSceneDocument>
prepareSceneDocument(const asset::SceneDocument& document,
                     const std::filesystem::path& assetsRoot) {
    PreparedSceneDocument prepared;
    const size_t count = document.nodes.size();
    prepared.assets.resize(count);
    prepared.localLights.resize(count);
    prepared.directionalLights.resize(count);
    std::vector<bool> own;
    for (const auto& node : document.nodes)
        own.push_back(node.enabled);
    prepared.enabled = effectiveDocumentEnabled(document, own);
    const auto parent = parents(document);
    if (auto content = prepareMeshContent(document, parent); !content)
        return std::unexpected(content.error());
    if (auto animations = prepareDocumentAnimations(document, parent); !animations)
        return std::unexpected(animations.error());
    if (auto lights = prepareLights(document, prepared); !lights)
        return std::unexpected(lights.error());
    for (size_t i = 0; i < count; ++i) {
        if (!document.nodes[i].generator)
            continue;
        for (int32_t p = static_cast<int32_t>(i); p >= 0; p = parent[static_cast<size_t>(p)]) {
            const auto& node = document.nodes[static_cast<size_t>(p)];
            const std::string pointer = "/nodes/" + std::to_string(p);
            if (node.translation != glm::vec3(0))
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::Unsupported,
                                      pointer + "/translation: generator roots and ancestors must "
                                                "have identity transforms"});
            if (node.rotation != glm::quat(1, 0, 0, 0))
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Unsupported,
                    pointer +
                        "/rotation: generator roots and ancestors must have identity transforms"});
            if (node.scale != glm::vec3(1))
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Unsupported,
                    pointer +
                        "/scale: generator roots and ancestors must have identity transforms"});
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const auto& node = document.nodes[i];
        if (!node.asset)
            continue;
        const std::string pointer = "/nodes/" + std::to_string(i) + "/extensions/LMX_scene";
        auto path =
            checkedReference(assetsRoot, node.asset->uri, node.asset->sha256, pointer + "/asset");
        if (!path)
            return std::unexpected(path.error());
        auto source = asset::loadGltf(path->string());
        if (!source)
            return std::unexpected(malformed(pointer + "/asset/uri", source.error().message));
        PreparedDocumentAsset asset{.path = *path, .source = std::move(*source)};
        asset.enabled.assign(asset.source.nodes.size(), true);
        asset.effective.resize(asset.enabled.size());
        for (size_t o = 0; o < node.overrides.size(); ++o) {
            const auto& override = node.overrides[o];
            const std::string at = pointer + "/overrides/" + std::to_string(o);
            if (override.node >= asset.source.nodes.size())
                return std::unexpected(malformed(at + "/node", "source node is out of range"));
            const auto& imported = asset.source.nodes[override.node];
            if (override.name != imported.name)
                return std::unexpected(malformed(at + "/name", "expected '" + override.name +
                                                                   "', source node is named '" +
                                                                   imported.name + "'"));
            if (override.pose && imported.animated)
                return std::unexpected(
                    malformed(at + "/pose", "animated source nodes cannot have a pose override"));
            if (override.pose &&
                std::ranges::none_of(asset.source.instances, [&](const auto& instance) {
                    return instance.node == override.node;
                }))
                return std::unexpected(
                    malformed(at + "/pose", "pose override requires a mesh node"));
            if (override.enabled)
                asset.enabled[override.node] = *override.enabled;
        }
        for (size_t n = 0; n < asset.enabled.size(); ++n) {
            bool enabled = prepared.enabled[i] && asset.enabled[n];
            for (int32_t p = asset.source.nodes[n].parent; p >= 0;
                 p = asset.source.nodes[static_cast<size_t>(p)].parent)
                enabled = enabled && asset.enabled[static_cast<size_t>(p)];
            asset.effective[n] = enabled;
        }
        std::vector<size_t> chain;
        for (int32_t p = static_cast<int32_t>(i); p >= 0; p = parent[static_cast<size_t>(p)])
            chain.push_back(static_cast<size_t>(p));
        for (auto p = chain.rbegin(); p != chain.rend(); ++p) {
            const auto& transform = document.nodes[*p];
            asset.rootWorld *= glm::translate(glm::mat4(1), transform.translation) *
                               glm::mat4_cast(transform.rotation) *
                               glm::scale(glm::mat4(1), transform.scale);
        }
        if (auto phases =
                validateIndependentAssetClips(asset.source, asset.rootWorld, pointer + "/asset");
            !phases)
            return std::unexpected(phases.error());
        const bool identityRoot = asset.rootWorld == glm::mat4(1.0f);
        for (auto& instance : asset.source.instances) {
            if (!identityRoot)
                instance.world = asset.rootWorld * instance.world;
            for (const auto& override : node.overrides) {
                if (override.node == instance.node && override.pose) {
                    const auto& pose = *override.pose;
                    instance.world =
                        composeTransform({pose.translation, pose.eulerDegrees, pose.scale});
                }
            }
            if (!decomposeTransform(instance.world))
                return std::unexpected(
                    malformed(pointer + "/asset", "world pose cannot be decomposed"));
        }
        if (!identityRoot)
            for (auto& track : asset.source.tracks)
                for (auto& key : track.keys) {
                    const auto world =
                        asset.rootWorld * glm::translate(glm::mat4(1), key.translation) *
                        glm::mat4_cast(key.rotation) * glm::scale(glm::mat4(1), key.scale);
                    const auto pose = decomposeTransform(world);
                    if (!pose)
                        return std::unexpected(malformed(
                            pointer + "/asset", "animated world pose cannot be decomposed"));
                    key.translation = pose->position;
                    key.rotation = glm::quat_cast(
                        glm::mat3(composeTransform({{}, pose->eulerDegrees, glm::vec3(1)})));
                    key.scale = pose->scale;
                }
        prepared.assets[i] = std::move(asset);
    }
    if (const auto& hdri = document.look.environment.hdri) {
        const std::string pointer = "/extensions/LMX_scene/look/environment/hdri";
        auto path = checkedReference(assetsRoot, hdri->uri, hdri->sha256, pointer);
        if (!path)
            return std::unexpected(path.error());
        auto image = asset::loadRadianceHdr(path->string());
        if (!image)
            return std::unexpected(malformed(pointer + "/uri", image.error().message));
        auto sky = asset::equirectangularToCubemap(*image, hdri->faceSize, hdri->yaw, hdri->scale);
        if (!sky)
            return std::unexpected(malformed(pointer, sky.error().message));
        auto diffuse =
            asset::equirectangularToCubemap(*image, hdri->diffuseFaceSize, hdri->yaw, hdri->scale);
        if (!diffuse)
            return std::unexpected(malformed(pointer, diffuse.error().message));
        prepared.hdri = std::move(*sky);
        prepared.diffuseHdri = std::move(*diffuse);
    }
    return prepared;
}

} // namespace lmx::engine
