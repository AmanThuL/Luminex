//----------------------------------------------------------------------------------------------------------------------
/// @file SceneInstantiate.cpp
/// @brief Assembles one unfinished scene in document order and publishes its source bindings.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/SceneInstantiate.h"

#include "Core/Diagnostics/Log.h"
#include "Core/Math/Color.h"
#include "Core/Math/Sphere.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/RepositoryAsset.h"
#include "Engine/Upload/IblUpload.h"
#include "Engine/Upload/SceneEnvironment.h"

#include <algorithm>
#include <array>
#include <limits>

namespace lmx::engine {
namespace {
//======================================================================================================================
asset::AssetResult<void> attachDocumentEnvironment(rojoRHI::Device& device, Scene& scene,
                                                   const PreparedSceneDocument& prepared) {
    const auto& environment = scene.look.environment;
    if (prepared.hdri) {
        auto sky = ibl::uploadCubemap(device, *prepared.hdri, scene.name + ".sky");
        if (!sky)
            return std::unexpected(
                asset::AssetError{asset::AssetErrorCode::UploadFailed, sky.error().message});
        ibl::GenerationOptions options;
        options.specularBaseFaceSize = environment.hdri->faceSize;
        options.irradianceSource = &*prepared.diffuseHdri;
        return attachEnvironment(device, scene, std::move(*sky), *prepared.hdri, scene.name,
                                 options);
    }
    const auto& rgb = environment.skySrgb8;
    const std::array<uint8_t, 4> pixel{rgb[0], rgb[1], rgb[2], 255};
    const rojoRHI::TextureMip face{.data = pixel.data(), .bytesPerRow = 4};
    const std::array<rojoRHI::TextureMip, 6> faces{face, face, face, face, face, face};
    auto sky = device.createTexture({.width = 1,
                                     .height = 1,
                                     .format = rojoRHI::Format::RGBA8Unorm_sRGB,
                                     .kind = rojoRHI::TextureKind::Cube,
                                     .mipLevels = 1,
                                     .sampled = true,
                                     .label = scene.name + ".sky"},
                                    faces);
    if (!sky)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::UploadFailed, sky.error().message});
    const glm::vec3 radiance =
        srgbToLinear(glm::vec3(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]),
                               static_cast<float>(rgb[2])) /
                     255.0f);
    return attachEnvironment(device, scene, std::move(*sky),
                             asset::ibl::makeConstantCubemap(radiance, 1), scene.name);
}
//======================================================================================================================
void bindAsset(SceneBinding& binding, uint32_t root, size_t objectBase,
               PreparedDocumentAsset& prepared) {
    const auto& source = prepared.source;
    const uint32_t importedBase = static_cast<uint32_t>(binding.importedNodes.size());
    for (uint32_t n = 0; n < source.nodes.size(); ++n) {
        const auto& node = source.nodes[n];
        ImportedNodeBinding imported{.assetRoot = root,
                                     .sourceNode = n,
                                     .parent = node.parent,
                                     .name = node.name,
                                     .animated = node.animated,
                                     .enabled = prepared.enabled[n],
                                     .effective = prepared.effective[n]};
        for (uint32_t instance : node.instances)
            imported.objects.push_back(objectBase + instance);
        binding.importedNodes.push_back(std::move(imported));
    }
    for (size_t i = 0; i < source.instances.size(); ++i) {
        const auto sourceNode = source.instances[i].node;
        binding.nodes[root].objects.push_back(objectBase + i);
        binding.objectNode.push_back(root);
        binding.objectImportedNode.push_back(importedBase + sourceNode);
        binding.objectGeneratorNode.push_back(kGeneratedNode);
        binding.objectEffective.push_back(prepared.effective[sourceNode]);
    }
    binding.assets.push_back({root, objectBase, prepared.rootWorld,
                              std::move(prepared.source.nodes), std::move(prepared.source.clips)});
}
//======================================================================================================================
std::optional<uint32_t> localLightGroup(const asset::SceneDocument& doc) {
    const auto onlyLocal = [&](auto&& self, uint32_t node) -> bool {
        const auto& value = doc.nodes[node];
        if (value.asset || value.generator || value.camera)
            return false;
        if (value.light)
            return doc.lights[*value.light].type != asset::DocLightType::Directional;
        if (value.children.empty())
            return false;
        return std::all_of(value.children.begin(), value.children.end(),
                           [&](uint32_t child) { return self(self, child); });
    };
    for (uint32_t root : doc.rootNodes) {
        if (!doc.nodes[root].light && onlyLocal(onlyLocal, root))
            return root;
    }
    return {};
}
} // namespace

//======================================================================================================================
void applyDocumentCamera(Scene& scene, const asset::SceneDocument& doc) {
    const auto& node = doc.nodes[doc.camera];
    const auto& camera = doc.cameras[*node.camera];
    const auto angles = asset::cameraAnglesForRotation(node.rotation, 0.0f);
    scene.initialCamera = {
        node.translation, angles.x,
        angles.y,         camera.fovY,
        camera.nearZ,     camera.farZ.value_or(std::numeric_limits<float>::infinity())};
    std::vector<asset::CameraKey> positions;
    std::vector<asset::CameraKey> rotations;
    for (const auto& animation : doc.animations) {
        if (animation.keyCount)
            scene.animation.duration = std::max(
                scene.animation.duration, double(animation.keyCount - 1) / animation.sampleRate);
        for (const auto& channel : animation.channels) {
            if (channel.node != doc.camera || channel.path == asset::DocChannelPath::Scale)
                continue;
            auto& values =
                channel.path == asset::DocChannelPath::Translation ? positions : rotations;
            values.clear();
            float previousYaw = 0.0f;
            for (uint32_t k = 0; k < animation.keyCount; ++k) {
                asset::CameraKey key{.time = double(k) / animation.sampleRate};
                const auto& value = channel.values[k];
                if (channel.path == asset::DocChannelPath::Translation)
                    key.position = glm::vec3(value);
                else {
                    const auto decoded = asset::cameraAnglesForRotation(
                        {value.w, value.x, value.y, value.z}, previousYaw);
                    previousYaw = decoded.x;
                    key.yaw = decoded.x;
                    key.pitch = decoded.y;
                }
                values.push_back(key);
            }
        }
    }
    auto& keys = scene.animation.cameraTrack;
    keys.clear();
    std::vector<double> times;
    for (const auto& key : positions)
        times.push_back(key.time);
    for (const auto& key : rotations)
        times.push_back(key.time);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    const auto sample = [](const std::vector<asset::CameraKey>& values, double time) {
        const auto exact = std::lower_bound(values.begin(), values.end(), time,
                                            [](const auto& key, double t) { return key.time < t; });
        if (exact != values.end() && exact->time == time)
            return *exact;
        return asset::sampleCameraTrack(values, time);
    };
    for (double time : times) {
        asset::CameraKey key{time, node.translation, angles.x, angles.y};
        if (!positions.empty())
            key.position = sample(positions, time).position;
        if (!rotations.empty()) {
            const auto value = sample(rotations, time);
            key.yaw = value.yaw;
            key.pitch = value.pitch;
        }
        keys.push_back(key);
    }
}
//======================================================================================================================
asset::AssetResult<LoadedScene> instantiateSceneDocument(rojoRHI::Device& device,
                                                         asset::SceneDocument document,
                                                         const std::filesystem::path& path,
                                                         const SceneGeneratorLookup& generators) {
    const auto assetsRoot = asset::findRepositoryAsset("Assets");
    if (!assetsRoot)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::NotFound,
                              "Assets root was not found from this working directory"});
    auto hash = asset::sceneDocumentHash(path);
    if (!hash)
        return std::unexpected(hash.error());
    auto prepared = prepareSceneDocument(document, *assetsRoot);
    if (!prepared)
        return std::unexpected(prepared.error());
    for (size_t i = 0; i < document.nodes.size(); ++i) {
        const auto& generator = document.nodes[i].generator;
        if (generator && (!generators || !generators(generator->name)))
            return std::unexpected(
                asset::AssetError{asset::AssetErrorCode::Unsupported,
                                  "/nodes/" + std::to_string(i) +
                                      "/extensions/LMX_scene/generator/name: unknown generator '" +
                                      generator->name + "'"});
    }
    LoadedScene loaded{.scene = std::make_unique<Scene>(),
                       .document = std::move(document),
                       .path = path,
                       .hash = std::move(*hash)};
    Scene& scene = *loaded.scene;
    const auto& doc = loaded.document;
    scene.name = doc.name;
    scene.look = doc.look;
    auto& binding = loaded.binding;
    binding.nodes.resize(doc.nodes.size());
    binding.localLightGroup = localLightGroup(doc);
    bool attached = false;
    const EnvironmentHook environment = [&](Scene& target) -> asset::AssetResult<void> {
        if (attached)
            return {};
        auto result = attachDocumentEnvironment(device, target, *prepared);
        if (result)
            attached = true;
        return result;
    };
    for (uint32_t n = 0; n < doc.nodes.size(); ++n) {
        const auto& node = doc.nodes[n];
        if (auto& imported = prepared->assets[n]) {
            const size_t objectBase = scene.objects.size();
            const Aabb boundsBefore = scene.authoredBounds;
            if (auto result = appendGltfScene(device, scene, imported->source, imported->path,
                                              scene.name, scene.authoredBounds);
                !result)
                return std::unexpected(result.error());
            bool hasPoseOverride = false;
            for (const auto& override : node.overrides) {
                if (!override.pose)
                    continue;
                hasPoseOverride = true;
                for (uint32_t instance : imported->source.nodes[override.node].instances) {
                    auto& object = scene.objects[objectBase + instance];
                    object.position = override.pose->translation;
                    object.eulerDegrees = override.pose->eulerDegrees;
                    object.scale = override.pose->scale;
                }
            }
            if (hasPoseOverride) {
                scene.authoredBounds = boundsBefore;
                for (size_t i = 0; i < imported->source.instances.size(); ++i) {
                    const auto model = scene.objects[objectBase + i].modelMatrix();
                    const auto& mesh =
                        imported->source.meshes[imported->source.instances[i].meshIndex];
                    for (const auto& vertex : mesh.vertices)
                        expand(scene.authoredBounds,
                               glm::vec3(model * glm::vec4(vertex.px, vertex.py, vertex.pz, 1)));
                }
                scene.boundingSphere = toVec4(boundingSphere(scene.authoredBounds));
            }
            bindAsset(binding, n, objectBase, *imported);
            if (auto result = environment(scene); !result)
                return std::unexpected(result.error());
        }
        if (node.generator) {
            const size_t objectBase = scene.objects.size();
            const size_t lightBase = scene.localLights().size();
            const size_t populationBase = scene.lightLabPopulations.size();
            if (auto result =
                    (*generators(node.generator->name))(scene, *node.generator, environment);
                !result)
                return std::unexpected(asset::AssetError{
                    result.error().code,
                    "/nodes/" + std::to_string(n) +
                        "/extensions/LMX_scene/generator: " + result.error().message});
            for (size_t i = populationBase; i < scene.lightLabPopulations.size(); ++i)
                scene.lightLabPopulations[i].documentNode = n;
            for (size_t i = objectBase; i < scene.objects.size(); ++i) {
                binding.nodes[n].objects.push_back(i);
                binding.objectNode.push_back(kGeneratedNode);
                binding.objectImportedNode.push_back(kGeneratedNode);
                binding.objectGeneratorNode.push_back(n);
                binding.objectEffective.push_back(prepared->enabled[n]);
            }
            for (size_t i = lightBase; i < scene.localLights().size(); ++i) {
                const auto id = scene.localLights()[i];
                binding.lightGeneratorNode.emplace(sceneLightKey(id), n);
                if (!prepared->enabled[n]) {
                    auto light = *scene.light(id);
                    light.enabled = false;
                    if (auto updated = scene.updateLight(id, light); !updated)
                        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                                 updated.error().message});
                }
            }
        }
    }
    if (auto result = environment(scene); !result)
        return std::unexpected(result.error());
    // The environment creates no authored rig; absent roles remain inert but retain pass shape.
    for (auto& light : scene.lights) {
        light.strength = glm::vec3(0);
        light.enabled = false;
    }
    scene.shadowCaster.reset();
    std::vector<LightId> rigIds;
    std::vector<bool> groupOwn(doc.nodes.size(), true);
    if (binding.localLightGroup)
        groupOwn[*binding.localLightGroup] = false;
    const auto outsideGroup = effectiveDocumentEnabled(doc, groupOwn);
    for (uint32_t n = 0; n < doc.nodes.size(); ++n) {
        const auto& node = doc.nodes[n];
        if (prepared->directionalLights[n]) {
            const uint32_t slot = node.role == "fill" ? 1 : node.role == "rim" ? 2 : 0;
            scene.lights[slot] = *prepared->directionalLights[n];
            binding.nodes[n].directional = slot;
            if (node.castsShadow)
                scene.shadowCaster = slot;
        }
        if (prepared->localLights[n]) {
            auto id = scene.addLight(*prepared->localLights[n]);
            if (!id)
                return std::unexpected(
                    asset::AssetError{asset::AssetErrorCode::UploadFailed, id.error().message});
            binding.nodes[n].light = *id;
            binding.lightNode.emplace(sceneLightKey(*id), n);
            if (!outsideGroup[n])
                rigIds.push_back(*id);
        }
    }
    scene.setRigLightIds(std::move(rigIds));
    applyDocumentCamera(scene, doc);
    binding.nodes[doc.camera].camera = true;
    scene.animation.loop = doc.loop;
    scene.resetMotion();
    if (auto finalized = scene.finalize(device); !finalized)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::UploadFailed, finalized.error().message});
    for (const auto& warning : doc.warnings)
        LMX_LOG_WARN("{}", warning);
    return loaded;
}

} // namespace lmx::engine
