//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentContent.cpp
/// @brief Uploads shared document resources and appends exact authored mesh poses and tracks.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/SceneDocumentContent.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Sphere.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Texture/TextureBake.h"

#include <algorithm>
#include <array>
#include <optional>

namespace lmx::engine {
namespace {
//======================================================================================================================
asset::AssetResult<std::vector<TextureId>> uploadImages(rojoRHI::Device& device, Scene& scene,
                                                        const asset::SceneDocument& doc) {
    std::vector<bool> srgb(doc.content->images.size(), false);
    for (const auto& material : doc.materials)
        for (const int index : {material.values.baseColorImage, material.values.emissiveImage})
            if (index >= 0)
                srgb[static_cast<size_t>(index)] = true;
    std::vector<TextureId> textures;
    for (size_t i = 0; i < doc.content->images.size(); ++i) {
        const auto& image = doc.content->images[i];
        const auto bytes =
            std::span(reinterpret_cast<const uint8_t*>(image.rgba8.data()), image.rgba8.size());
        const auto format =
            srgb[i] ? rojoRHI::Format::RGBA8Unorm_sRGB : rojoRHI::Format::RGBA8Unorm;
        const std::string label = scene.name + ".document.image" + std::to_string(i);
        auto uploaded = [&]() {
            if (image.mipmapped) {
                const auto baked =
                    asset::bakeMips(bytes, image.width, image.height,
                                    srgb[i] ? asset::BakeMode::Srgb : asset::BakeMode::Linear);
                return device.createTexture({.width = image.width,
                                             .height = image.height,
                                             .format = format,
                                             .mipLevels = baked.mipLevels,
                                             .sampled = true,
                                             .label = label},
                                            baked.mips);
            }
            const rojoRHI::TextureMip level{.data = image.rgba8.data(),
                                            .bytesPerRow = image.width * 4};
            return device.createTexture({.width = image.width,
                                         .height = image.height,
                                         .format = format,
                                         .mipLevels = 1,
                                         .sampled = true,
                                         .label = label},
                                        std::span(&level, 1));
        }();
        if (!uploaded)
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::UploadFailed,
                                                     "/images/" + std::to_string(i) + ": " +
                                                         uploaded.error().message});
        textures.push_back(scene.addTexture(std::move(*uploaded)));
    }
    return textures;
}
//======================================================================================================================
MaterialRecord materialRecord(const asset::GltfMaterial& source,
                              std::span<const TextureId> textures) {
    const auto texture = [&](int index) -> std::optional<TextureId> {
        return index >= 0 ? std::optional(textures[static_cast<size_t>(index)]) : std::nullopt;
    };
    return {.diffuse = texture(source.baseColorImage),
            .normalMap = texture(source.normalImage),
            .metallicRoughness = texture(source.metallicRoughnessImage),
            .occlusion = texture(source.occlusionImage),
            .emissiveMap = texture(source.emissiveImage),
            .albedo = source.baseColorFactor,
            .roughness = source.roughness,
            .metallic = source.metallic,
            .occlusionStrength = source.occlusionStrength,
            .emissive = source.emissiveFactor,
            .alphaMode = source.alphaMode == asset::GltfAlphaMode::Mask ? AlphaMode::Mask
                                                                        : AlphaMode::Opaque,
            .alphaCutoff = source.alphaCutoff,
            .doubleSided = source.doubleSided};
}
//======================================================================================================================
void appendTracks(Scene& scene, const asset::SceneDocument& doc, uint32_t nodeIndex,
                  uint32_t objectIndex) {
    const auto& node = doc.nodes[nodeIndex];
    const uint32_t material = doc.meshes[*node.mesh].material;
    std::optional<asset::RigidTrack> rigid;
    std::optional<asset::EmissiveTrack> emissive;
    for (const auto& animation : doc.animations) {
        for (const auto& channel : animation.channels) {
            if (channel.path == asset::DocChannelPath::EmissiveStrength) {
                if (channel.material != material)
                    continue;
                emissive.emplace();
                emissive->objectIndex = objectIndex;
                for (uint32_t k = 0; k < animation.keyCount; ++k)
                    emissive->keys.push_back(
                        {double(k) / animation.sampleRate, channel.values[k].x});
            } else {
                if (channel.node != nodeIndex)
                    continue;
                if (!rigid) {
                    rigid.emplace();
                    rigid->objectIndex = objectIndex;
                    rigid->step = channel.step;
                    for (uint32_t k = 0; k < animation.keyCount; ++k)
                        rigid->keys.push_back({double(k) / animation.sampleRate, node.translation,
                                               node.rotation, node.scale});
                }
                for (uint32_t k = 0; k < animation.keyCount; ++k) {
                    const auto& value = channel.values[k];
                    auto& key = rigid->keys[k];
                    if (channel.path == asset::DocChannelPath::Translation)
                        key.translation = glm::vec3(value);
                    else if (channel.path == asset::DocChannelPath::Rotation)
                        key.rotation = {value.w, value.x, value.y, value.z};
                    else
                        key.scale = glm::vec3(value);
                }
            }
            if (animation.keyCount)
                scene.animation.duration =
                    std::max(scene.animation.duration,
                             double(animation.keyCount - 1) / animation.sampleRate);
        }
    }
    if (rigid)
        scene.animation.tracks.push_back(std::move(*rigid));
    if (emissive)
        scene.animation.emissiveTracks.push_back(std::move(*emissive));
}
} // namespace

//======================================================================================================================
asset::AssetResult<void> appendDocumentContent(rojoRHI::Device& device, Scene& scene,
                                               const asset::SceneDocument& doc,
                                               std::span<const uint32_t> meshNodes,
                                               DocumentContentBinding& binding) {
    LMX_ASSERT(doc.content, "document content must have passed preflight");
    if (!binding.m_uploaded) {
        binding.objectOfNode.assign(doc.nodes.size(), kGeneratedNode);
        std::vector<bool> own;
        for (const auto& node : doc.nodes)
            own.push_back(node.enabled);
        binding.m_enabled = effectiveDocumentEnabled(doc, own);
        for (size_t i = 0; i < doc.content->geometries.size(); ++i)
            binding.m_meshes.push_back(
                scene.addMesh(fromGeo(doc.content->geometries[i]),
                              scene.name + ".document.mesh" + std::to_string(i)));
        auto textures = uploadImages(device, scene, doc);
        if (!textures)
            return std::unexpected(textures.error());
        for (const auto& material : doc.materials)
            binding.m_materials.push_back(
                scene.addMaterial(materialRecord(material.values, *textures)));
        if (doc.bounds) {
            expand(scene.authoredBounds, doc.bounds->first);
            expand(scene.authoredBounds, doc.bounds->second);
        }
        binding.m_uploaded = true;
    }
    for (const uint32_t n : meshNodes) {
        LMX_ASSERT(n < doc.nodes.size() && doc.nodes[n].mesh,
                   "document content append requires mesh nodes");
        LMX_ASSERT(binding.objectOfNode[n] == kGeneratedNode, "mesh node appended twice");
        const auto& node = doc.nodes[n];
        const auto& mesh = doc.meshes[*node.mesh];
        const uint32_t objectIndex = static_cast<uint32_t>(scene.objects.size());
        scene.addObject({.name = node.name,
                         .position = node.translation,
                         .eulerDegrees = asset::eulerDegreesForRotation(node.rotation),
                         .scale = node.scale,
                         .mesh = binding.m_meshes[mesh.geometry],
                         .material = binding.m_materials[mesh.material],
                         .motionClass = node.motion == asset::DocMotion::Invalid
                                            ? MotionClass::Invalid
                                            : MotionClass::Rigid,
                         .emissiveStrength = doc.materials[mesh.material].emissiveStrength,
                         .sourceName = node.name,
                         .enabled = binding.m_enabled[n]});
        binding.objectOfNode[n] = objectIndex;
        appendTracks(scene, doc, n, objectIndex);
        if (!doc.bounds) {
            const auto model = scene.objects.back().modelMatrix();
            for (const auto& vertex : doc.content->geometries[mesh.geometry].vertices)
                expand(scene.authoredBounds,
                       glm::vec3(model * glm::vec4(vertex.px, vertex.py, vertex.pz, 1)));
        }
    }
    if (isValidAabb(scene.authoredBounds))
        scene.boundingSphere = toVec4(boundingSphere(scene.authoredBounds));
    return {};
}

} // namespace lmx::engine
