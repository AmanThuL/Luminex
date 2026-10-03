//----------------------------------------------------------------------------------------------------------------------
/// @file LabContentCapture.cpp
/// @brief Captures ordered lab resource bytes and exact object animation into document companions.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/LabContentCapture.h"

#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Scene/SceneTables.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstring>
#include <format>
#include <limits>
#include <tuple>

namespace lmx::scenes {
namespace {
//======================================================================================================================
std::unexpected<asset::AssetError> fail(std::string message) {
    return std::unexpected(
        asset::AssetError{asset::AssetErrorCode::Malformed, "Lab capture: " + std::move(message)});
}
//======================================================================================================================
asset::AssetResult<asset::DocImage> captureImage(const LabTexture& source) {
    const uint64_t row = uint64_t{source.width} * 4;
    if (source.width == 0 || source.height == 0 || row > INT_MAX ||
        (row + 1) * source.height > INT_MAX || source.rgba8.size() != row * source.height)
        return fail("texture '" + source.name + "' has invalid RGBA8 dimensions");
    asset::DocImage image{.name = source.name,
                          .width = source.width,
                          .height = source.height,
                          .mipmapped = source.mipmapped,
                          .rgba8 = source.rgba8};
    const auto append = [](void* context, void* data, int length) {
        auto& bytes = *static_cast<std::vector<std::byte>*>(context);
        const auto* first = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), first, first + length);
    };
    if (!stbi_write_png_to_func(append, &image.file, static_cast<int>(image.width),
                                static_cast<int>(image.height), 4, image.rgba8.data(),
                                static_cast<int>(row)))
        return fail("PNG encoding failed for '" + source.name + "'");
    image.sha256 = sha256Hex(image.file);
    return image;
}
//======================================================================================================================
asset::AssetResult<void> captureResources(asset::SceneDocument& doc, const engine::Scene& scene,
                                          std::span<const LabTexture> textures) {
    const auto tables = scene.tables();
    if (!tables.vertices || !tables.indices || scene.skySphere || scene.objects.empty())
        return fail("expected a prepared bare generator scene without sky geometry");
    auto content = std::make_shared<asset::DocContent>();
    std::vector<asset::VertexPNTU> vertices(scene.tableStats().vertexBytes / 48);
    std::vector<uint32_t> indices(scene.tableStats().indexBytes / 4);
    tables.vertices->readback(vertices.data(), vertices.size() * sizeof(vertices[0]));
    tables.indices->readback(indices.data(), indices.size() * sizeof(indices[0]));
    std::vector<engine::MeshRow> meshes(tables.meshCount);
    tables.meshes->readback(meshes.data(), meshes.size() * sizeof(meshes[0]));
    for (const auto& mesh : meshes) {
        asset::GeoData geometry;
        geometry.vertices.assign(vertices.begin() + mesh.firstVertex,
                                 vertices.begin() + mesh.firstVertex + mesh.vertexCount);
        for (uint32_t i = 0; i < mesh.indexCount; ++i)
            geometry.indices.push_back(indices[mesh.firstIndex + i] - mesh.firstVertex);
        content->geometries.push_back(std::move(geometry));
    }
    std::vector<std::optional<engine::MaterialId>> materials(tables.materialCount);
    for (const auto& object : scene.objects) {
        if (object.material.slot >= materials.size())
            return fail("material slot lies outside the prepared table");
        materials[object.material.slot] = object.material;
    }
    std::vector<bool> seenTextures(textures.size(), false);
    const auto imageIndex = [&](std::optional<engine::TextureId> id,
                                bool srgb) -> asset::AssetResult<int> {
        if (!id)
            return -1;
        if (id->slot >= textures.size() || !scene.tryTexture(*id))
            return fail("material texture slot has no captured pixels");
        const auto& input = textures[id->slot];
        const auto* texture = scene.tryTexture(*id);
        const auto format = srgb ? rojoRHI::Format::RGBA8Unorm_sRGB : rojoRHI::Format::RGBA8Unorm;
        if (input.srgb != srgb || texture->format() != format || texture->width() != input.width ||
            texture->height() != input.height ||
            texture->mipLevels() !=
                (input.mipmapped
                     ? static_cast<uint32_t>(std::bit_width(std::max(input.width, input.height)))
                     : 1u))
            return fail("texture '" + input.name + "' format, dimensions or mip count differs");
        seenTextures[id->slot] = true;
        return static_cast<int>(id->slot);
    };
    for (size_t i = 0; i < materials.size(); ++i) {
        if (!materials[i])
            return fail("material slot " + std::to_string(i) + " has no generator object");
        const auto& material = scene.material(*materials[i]);
        const glm::mat4 identity(1.0f);
        if (std::memcmp(&material.uvTransform, &identity, sizeof(glm::mat4)) != 0)
            return fail("material UV transform is not representable");
        asset::DocMaterial output{.name = "Material " + std::to_string(i)};
        auto& values = output.values;
        values.baseColorFactor = material.albedo;
        values.roughness = material.roughness;
        values.metallic = material.metallic;
        values.occlusionStrength = material.occlusionStrength;
        values.emissiveFactor = material.emissive;
        values.alphaMode = material.alphaMode == engine::AlphaMode::Mask
                               ? asset::GltfAlphaMode::Mask
                               : asset::GltfAlphaMode::Opaque;
        values.alphaCutoff = material.alphaCutoff;
        values.doubleSided = material.doubleSided;
        const std::array bindings{
            std::tuple{material.diffuse, true, &values.baseColorImage},
            std::tuple{material.normalMap, false, &values.normalImage},
            std::tuple{material.metallicRoughness, false, &values.metallicRoughnessImage},
            std::tuple{material.occlusion, false, &values.occlusionImage},
            std::tuple{material.emissiveMap, true, &values.emissiveImage}};
        for (const auto& [id, srgb, destination] : bindings) {
            auto index = imageIndex(id, srgb);
            if (!index)
                return std::unexpected(index.error());
            *destination = *index;
        }
        std::optional<float> strength;
        for (const auto& object : scene.objects) {
            if (object.material != *materials[i])
                continue;
            if (strength && std::bit_cast<uint32_t>(*strength) !=
                                std::bit_cast<uint32_t>(object.emissiveStrength))
                return fail("shared material has divergent emissive strengths");
            strength = object.emissiveStrength;
        }
        output.emissiveStrength = *strength;
        doc.materials.push_back(std::move(output));
    }
    if (std::ranges::find(seenTextures, false) != seenTextures.end())
        return fail("captured texture has no material reference");
    for (const auto& texture : textures) {
        auto image = captureImage(texture);
        if (!image)
            return std::unexpected(image.error());
        content->images.push_back(std::move(*image));
    }
    doc.content = content;
    content->geometrySha256 = sha256Hex(asset::sceneDocumentGeometry(doc));
    return {};
}
//======================================================================================================================
asset::AssetResult<void> captureTracks(asset::SceneDocument& doc, const engine::Scene& scene,
                                       std::span<const uint32_t> nodes) {
    for (const auto& track : scene.animation.tracks) {
        if (track.objectIndex >= nodes.size() || nodes[track.objectIndex] == UINT32_MAX ||
            track.keys.empty() || track.loopDuration != 0.0)
            return fail("rigid track cannot be represented on the captured object");
        asset::DocAnimation animation{.name = scene.objects[track.objectIndex].name,
                                      .sampleRate = asset::kAnimationBakeRate,
                                      .keyCount = static_cast<uint32_t>(track.keys.size())};
        for (const auto path : {asset::DocChannelPath::Translation, asset::DocChannelPath::Rotation,
                                asset::DocChannelPath::Scale}) {
            asset::DocChannel channel{
                .node = nodes[track.objectIndex], .path = path, .step = track.step};
            for (size_t i = 0; i < track.keys.size(); ++i) {
                const auto& key = track.keys[i];
                if (key.time != double(i) / asset::kAnimationBakeRate)
                    return fail("rigid key time differs from the document 60 Hz grid");
                channel.values.push_back(path == asset::DocChannelPath::Translation
                                             ? glm::vec4(key.translation, 0.0f)
                                         : path == asset::DocChannelPath::Scale
                                             ? glm::vec4(key.scale, 0.0f)
                                             : glm::vec4(key.rotation.x, key.rotation.y,
                                                         key.rotation.z, key.rotation.w));
            }
            animation.channels.push_back(std::move(channel));
        }
        doc.animations.push_back(std::move(animation));
    }
    for (const auto& track : scene.animation.emissiveTracks) {
        if (track.objectIndex >= nodes.size() || nodes[track.objectIndex] == UINT32_MAX ||
            track.keys.empty())
            return fail("emissive track has no captured object");
        const auto& node = doc.nodes[nodes[track.objectIndex]];
        asset::DocAnimation animation{
            .name = scene.objects[track.objectIndex].name + " emission",
            .sampleRate = asset::kAnimationBakeRate,
            .keyCount =
                static_cast<uint32_t>(scene.animation.duration * asset::kAnimationBakeRate) + 1};
        asset::DocChannel channel{.path = asset::DocChannelPath::EmissiveStrength,
                                  .material = doc.meshes[*node.mesh].material,
                                  .step = true};
        for (uint32_t k = 0; k < animation.keyCount; ++k)
            channel.values.push_back(
                {asset::sampleEmissiveTrack(track, double(k) / animation.sampleRate), 0.0f, 0.0f,
                 0.0f});
        animation.channels.push_back(std::move(channel));
        doc.animations.push_back(std::move(animation));
    }
    return {};
}
} // namespace

//======================================================================================================================
asset::AssetResult<asset::SceneDocument> captureLabDocument(const asset::SceneDocument& current,
                                                            const engine::Scene& generated,
                                                            std::span<const LabTexture> textures,
                                                            std::span<const uint32_t> skipObjects) {
    std::optional<uint32_t> generator;
    for (uint32_t n = 0; n < current.nodes.size(); ++n) {
        if (!current.nodes[n].generator)
            continue;
        if (generator)
            return fail("expected exactly one generator node");
        generator = n;
    }
    if (!generator)
        return fail("document has no generator node");
    const auto& source = current.nodes[*generator];
    const auto& name = source.generator->name;
    if (name != "material-lab" && name != "temporal-lab" && name != "light-lab")
        return fail("unsupported generator '" + name + "'");
    if (!current.meshes.empty() || current.content || !current.materials.empty() ||
        !source.children.empty() || source.translation != glm::vec3(0) ||
        source.rotation != glm::quat(1, 0, 0, 0) || source.scale != glm::vec3(1) ||
        std::ranges::find(current.rootNodes, *generator) == current.rootNodes.end())
        return fail("generator must be an identity root without existing saved content");
    std::vector<bool> skip(generated.objects.size(), false);
    for (uint32_t index : skipObjects) {
        if (index >= skip.size() || skip[index])
            return fail("invalid or repeated skipped object index");
        skip[index] = true;
    }
    asset::SceneDocument doc = current;
    doc.schemaVersion = asset::kSceneDocumentSchema;
    doc.nodes.clear();
    doc.rootNodes.clear();
    auto captured = captureResources(doc, generated, textures);
    if (!captured)
        return std::unexpected(captured.error());
    doc.bounds = std::pair{generated.authoredBounds.minimum, generated.authoredBounds.maximum};
    std::vector<uint32_t> remap(current.nodes.size(), UINT32_MAX);
    std::vector<uint32_t> objectNodes(generated.objects.size(), UINT32_MAX);
    std::vector<uint32_t> replacement;
    for (uint32_t n = 0; n < current.nodes.size(); ++n) {
        if (n == *generator) {
            for (uint32_t i = 0; i < generated.objects.size(); ++i) {
                if (skip[i])
                    continue;
                const auto& object = generated.objects[i];
                const auto rotation = asset::exactRotationForEulerDegrees(object.eulerDegrees);
                if (!rotation)
                    return fail(std::format("object {} '{}' eulerDegrees [{:.9g}, {:.9g}, {:.9g}] "
                                            "bits [{:08x}, {:08x}, {:08x}] has no exact quaternion",
                                            i, object.name, object.eulerDegrees.x,
                                            object.eulerDegrees.y, object.eulerDegrees.z,
                                            std::bit_cast<uint32_t>(object.eulerDegrees.x),
                                            std::bit_cast<uint32_t>(object.eulerDegrees.y),
                                            std::bit_cast<uint32_t>(object.eulerDegrees.z)));
                const uint32_t node = static_cast<uint32_t>(doc.nodes.size());
                objectNodes[i] = node;
                replacement.push_back(node);
                const uint32_t mesh = static_cast<uint32_t>(doc.meshes.size());
                doc.meshes.push_back({object.name, object.mesh.slot, object.material.slot});
                doc.nodes.push_back({.name = object.name,
                                     .translation = object.position,
                                     .rotation = *rotation,
                                     .scale = object.scale,
                                     .mesh = mesh,
                                     .motion = object.motionClass == engine::MotionClass::Invalid
                                                   ? asset::DocMotion::Invalid
                                                   : asset::DocMotion::Rigid,
                                     .enabled = object.enabled && source.enabled});
            }
            if (name != "light-lab")
                continue;
        }
        remap[n] = static_cast<uint32_t>(doc.nodes.size());
        doc.nodes.push_back(current.nodes[n]);
    }
    for (auto& node : doc.nodes)
        for (auto& child : node.children)
            child = remap[child];
    for (uint32_t root : current.rootNodes) {
        if (root == *generator)
            doc.rootNodes.insert(doc.rootNodes.end(), replacement.begin(), replacement.end());
        if (remap[root] != UINT32_MAX)
            doc.rootNodes.push_back(remap[root]);
    }
    doc.camera = remap[current.camera];
    for (auto& animation : doc.animations)
        for (auto& channel : animation.channels)
            if (channel.path != asset::DocChannelPath::EmissiveStrength)
                channel.node = remap[channel.node];
    asset::DocNode lights{.name = "Lights"};
    for (uint32_t n = 0; n < doc.nodes.size(); ++n)
        if (doc.nodes[n].role)
            lights.children.push_back(n);
    if (!lights.children.empty()) {
        std::erase_if(doc.rootNodes, [&](uint32_t n) {
            return std::ranges::find(lights.children, n) != lights.children.end();
        });
        doc.rootNodes.push_back(static_cast<uint32_t>(doc.nodes.size()));
        doc.nodes.push_back(std::move(lights));
    }
    if (auto tracks = captureTracks(doc, generated, objectNodes); !tracks)
        return std::unexpected(tracks.error());
    if (auto valid = asset::validateSceneDocumentModel(doc); !valid)
        return std::unexpected(valid.error());
    return doc;
}
} // namespace lmx::scenes
