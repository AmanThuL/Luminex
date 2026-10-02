//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentContentWrite.cpp
/// @brief Serializes immutable document geometry, material factors and hashed PNG references.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocumentWriteInternal.h"

#include "Core/IO/JsonWriter.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/DocumentUri.h"
#include "Engine/Asset/Image/PngImage.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace lmx::asset {
namespace {

//======================================================================================================================
template <typename T>
void field(JsonWriter& w, std::string_view key, const T& value) {
    w.key(key);
    if constexpr (std::is_same_v<T, bool>)
        w.boolean(value);
    else if constexpr (std::is_integral_v<T>)
        w.integer(value);
    else if constexpr (std::is_floating_point_v<T>)
        w.number(value);
    else
        w.string(value);
}

//======================================================================================================================
template <typename T>
void vector(JsonWriter& w, std::string_view key, const T& value, size_t count) {
    w.key(key);
    w.beginArray(true);
    for (size_t i = 0; i < count; ++i)
        w.number(value[i]);
    w.endArray();
}

//======================================================================================================================
void texture(JsonWriter& w, std::string_view key, int index, std::optional<float> strength = {}) {
    if (index < 0)
        return;
    w.key(key);
    w.beginObject();
    field(w, "index", index);
    if (strength)
        field(w, "strength", *strength);
    w.endObject();
}

//======================================================================================================================
void material(JsonWriter& w, const DocMaterial& material) {
    const auto& v = material.values;
    w.beginObject();
    field(w, "name", material.name);
    w.key("pbrMetallicRoughness");
    w.beginObject();
    vector(w, "baseColorFactor", v.baseColorFactor, 4);
    field(w, "metallicFactor", v.metallic);
    field(w, "roughnessFactor", v.roughness);
    texture(w, "baseColorTexture", v.baseColorImage);
    texture(w, "metallicRoughnessTexture", v.metallicRoughnessImage);
    w.endObject();
    texture(w, "normalTexture", v.normalImage);
    texture(w, "occlusionTexture", v.occlusionImage, v.occlusionStrength);
    texture(w, "emissiveTexture", v.emissiveImage);
    vector(w, "emissiveFactor", v.emissiveFactor, 3);
    field(w, "alphaMode", v.alphaMode == GltfAlphaMode::Mask ? "MASK" : "OPAQUE");
    field(w, "alphaCutoff", v.alphaCutoff);
    field(w, "doubleSided", v.doubleSided);
    w.key("extensions");
    w.beginObject();
    w.key("KHR_materials_emissive_strength");
    w.beginObject();
    field(w, "emissiveStrength", material.emissiveStrength);
    w.endObject();
    w.endObject();
    w.endObject();
}

//======================================================================================================================
AssetResult<void> invalid(std::string path, std::string_view reason) {
    return std::unexpected(AssetError{AssetErrorCode::Malformed,
                                      "JSON pointer '" + path + "': " + std::string(reason)});
}

} // namespace

//======================================================================================================================
std::vector<std::byte> sceneDocumentGeometry(const SceneDocument& doc) {
    std::vector<std::byte> bytes;
    if (!doc.content)
        return bytes;
    static_assert(std::endian::native == std::endian::little);
    for (const auto& geometry : doc.content->geometries) {
        const auto vertices = std::as_bytes(std::span(geometry.vertices));
        const auto indices = std::as_bytes(std::span(geometry.indices));
        bytes.insert(bytes.end(), vertices.begin(), vertices.end());
        bytes.insert(bytes.end(), indices.begin(), indices.end());
    }
    return bytes;
}

//======================================================================================================================
std::string detail::geometryUri(std::string_view bufferUri) {
    return std::filesystem::path(bufferUri).stem().string() + ".geometry.bin";
}

//======================================================================================================================
std::string detail::imageUri(std::string_view bufferUri, const DocImage& image) {
    return std::filesystem::path(bufferUri).stem().string() + ".textures/" + image.name + ".png";
}

//======================================================================================================================
AssetResult<void> detail::validateImageName(std::string_view name, size_t index,
                                            std::span<const DocImage> previous) {
    const auto path = "/images/" + std::to_string(index) + "/name";
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string_view::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return invalid(path, "expected a safe PNG filename stem");
    const auto fold = [](std::string_view text) {
        std::string result(text);
        for (char& c : result)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return result;
    };
    const auto folded = fold(name);
    for (const auto& image : previous)
        if (fold(image.name) == folded)
            return invalid(path, "duplicate PNG filename");
    return {};
}

//======================================================================================================================
AssetResult<void> detail::validateDocumentContent(const SceneDocument& doc) {
    if (doc.schemaVersion != 1 && doc.schemaVersion != kSceneDocumentSchema)
        return invalid("/extensions/LMX_scene/schemaVersion", "unsupported schema version");
    if (doc.bounds) {
        if (doc.schemaVersion < 2)
            return invalid("/extensions/LMX_scene/bounds", "requires schema 2");
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(doc.bounds->first[k]) || !std::isfinite(doc.bounds->second[k]) ||
                doc.bounds->first[k] > doc.bounds->second[k])
                return invalid("/extensions/LMX_scene/bounds", "invalid finite min/max bounds");
    }
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
        const auto& node = doc.nodes[i];
        const auto path = "/nodes/" + std::to_string(i);
        if (node.mesh && (doc.schemaVersion < 2 || *node.mesh >= doc.meshes.size()))
            return invalid(path + "/mesh", "invalid mesh reference for this schema");
        if ((node.motion != DocMotion::Rigid && node.motion != DocMotion::Invalid) ||
            (doc.schemaVersion < 2 && node.motion != DocMotion::Rigid))
            return invalid(path + "/extensions/LMX_scene/motion", "invalid motion for this schema");
    }
    if (doc.meshes.empty()) {
        if (doc.content || !doc.materials.empty())
            return invalid("/meshes", "content requires meshes");
        return {};
    }
    if (doc.schemaVersion < 2 || !doc.content || doc.content->geometries.empty())
        return invalid("/meshes", "schema 2 meshes require geometry content");
    for (const auto& mesh : doc.meshes)
        if (mesh.geometry >= doc.content->geometries.size() ||
            mesh.material >= doc.materials.size())
            return invalid("/meshes", "invalid geometry or material index");
    std::vector<bool> referenced(doc.content->geometries.size());
    for (const auto& mesh : doc.meshes)
        referenced[mesh.geometry] = true;
    if (std::ranges::find(referenced, false) != referenced.end())
        return invalid("/meshes", "every geometry must be referenced by a mesh");
    for (const auto& geometry : doc.content->geometries) {
        if (geometry.vertices.empty() || geometry.indices.empty() || geometry.indices.size() % 3)
            return invalid("/meshes", "geometry requires vertices and triangle indices");
        for (const auto& vertex : geometry.vertices)
            for (float value : std::bit_cast<std::array<float, 12>>(vertex))
                if (!std::isfinite(value))
                    return invalid("/meshes", "non-finite vertex");
        for (uint32_t index : geometry.indices)
            if (index >= geometry.vertices.size())
                return invalid("/meshes", "vertex index out of range");
    }
    if (sha256Hex(sceneDocumentGeometry(doc)) != doc.content->geometrySha256)
        return invalid("/extensions/LMX_scene/contentHashes",
                       "geometry bytes do not match the model hash");
    for (size_t i = 0; i < doc.content->images.size(); ++i) {
        const auto& image = doc.content->images[i];
        const auto path = "/images/" + std::to_string(i);
        if (auto name = validateImageName(image.name, i, std::span(doc.content->images).first(i));
            !name)
            return name;
        if (sha256Hex(image.file) != image.sha256)
            return invalid(path + "/uri", "PNG bytes do not match the model hash");
        const auto decoded = readPng(std::span<const std::byte>(image.file));
        if (!decoded || decoded->width != image.width || decoded->height != image.height ||
            !std::ranges::equal(std::as_bytes(std::span(decoded->rgba)), image.rgba8))
            return invalid(path + "/uri", "PNG bytes do not match the model pixels");
    }
    for (size_t i = 0; i < doc.materials.size(); ++i) {
        const auto& material = doc.materials[i];
        const auto& v = material.values;
        const auto path = "/materials/" + std::to_string(i);
        const std::array<float, 10> factors{
            v.baseColorFactor.x, v.baseColorFactor.y, v.baseColorFactor.z, v.baseColorFactor.w,
            v.emissiveFactor.x,  v.emissiveFactor.y,  v.emissiveFactor.z,  v.metallic,
            v.roughness,         v.occlusionStrength};
        for (float value : factors)
            if (!std::isfinite(value) || value < 0 || value > 1)
                return invalid(path, "factor outside [0,1]");
        if (!std::isfinite(material.emissiveStrength) || material.emissiveStrength < 0 ||
            !std::isfinite(v.alphaCutoff) || v.alphaCutoff < 0)
            return invalid(path, "invalid emissive strength or alpha cutoff");
        if (v.alphaMode != GltfAlphaMode::Opaque && v.alphaMode != GltfAlphaMode::Mask)
            return invalid(path + "/alphaMode", "invalid enum");
        for (int index : {v.baseColorImage, v.normalImage, v.metallicRoughnessImage,
                          v.occlusionImage, v.emissiveImage})
            if (index < -1 || (index >= 0 && size_t(index) >= doc.content->images.size()))
                return invalid(path, "image index out of range");
    }
    return {};
}

//======================================================================================================================
void detail::geometryViews(JsonWriter& w, const SceneDocument& doc) {
    if (!doc.content)
        return;
    uint64_t offset = 0;
    for (const auto& geometry : doc.content->geometries) {
        for (bool vertices : {true, false}) {
            const auto length = vertices ? geometry.vertices.size() * sizeof(VertexPNTU)
                                         : geometry.indices.size() * sizeof(uint32_t);
            w.beginObject();
            field(w, "buffer", doc.animations.empty() ? 0 : 1);
            field(w, "byteOffset", offset);
            field(w, "byteLength", length);
            if (vertices)
                field(w, "byteStride", sizeof(VertexPNTU));
            field(w, "target", vertices ? 34962 : 34963);
            w.endObject();
            offset += length;
        }
    }
}

//======================================================================================================================
void detail::geometryAccessors(JsonWriter& w, const SceneDocument& doc, size_t animationViews) {
    if (!doc.content)
        return;
    constexpr std::array offsets{0, 12, 24, 40, 0};
    constexpr std::array types{"VEC3", "VEC3", "VEC4", "VEC2", "SCALAR"};
    for (size_t g = 0; g < doc.content->geometries.size(); ++g) {
        const auto& geometry = doc.content->geometries[g];
        for (size_t a = 0; a < 5; ++a) {
            w.beginObject();
            field(w, "bufferView", animationViews + g * 2 + (a == 4 ? 1 : 0));
            field(w, "byteOffset", offsets[a]);
            field(w, "componentType", a == 4 ? 5125 : 5126);
            field(w, "count", a == 4 ? geometry.indices.size() : geometry.vertices.size());
            field(w, "type", types[a]);
            if (a == 0) {
                glm::vec3 lo(std::numeric_limits<float>::max()),
                    hi(std::numeric_limits<float>::lowest());
                for (const auto& vertex : geometry.vertices) {
                    const auto values = std::bit_cast<std::array<float, 12>>(vertex);
                    for (int k = 0; k < 3; ++k) {
                        lo[k] = std::min(lo[k], values[k]);
                        hi[k] = std::max(hi[k], values[k]);
                    }
                }
                vector(w, "min", lo, 3);
                vector(w, "max", hi, 3);
            }
            w.endObject();
        }
    }
}

//======================================================================================================================
void detail::contentJson(JsonWriter& w, const SceneDocument& doc, std::string_view bufferUri,
                         size_t animationAccessors) {
    if (!doc.content)
        return;
    w.key("meshes");
    w.beginArray();
    for (const auto& mesh : doc.meshes) {
        w.beginObject();
        field(w, "name", mesh.name);
        w.key("primitives");
        w.beginArray();
        w.beginObject();
        w.key("attributes");
        w.beginObject();
        const auto first = animationAccessors + mesh.geometry * 5;
        field(w, "POSITION", first);
        field(w, "NORMAL", first + 1);
        field(w, "TANGENT", first + 2);
        field(w, "TEXCOORD_0", first + 3);
        w.endObject();
        field(w, "indices", first + 4);
        field(w, "material", mesh.material);
        w.endObject();
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.key("materials");
    w.beginArray();
    for (const auto& value : doc.materials)
        material(w, value);
    w.endArray();
    if (doc.content->images.empty())
        return;
    w.key("textures");
    w.beginArray();
    for (size_t i = 0; i < doc.content->images.size(); ++i) {
        w.beginObject();
        field(w, "source", i);
        field(w, "sampler", i);
        w.endObject();
    }
    w.endArray();
    w.key("images");
    w.beginArray();
    for (const auto& image : doc.content->images) {
        w.beginObject();
        field(w, "name", image.name);
        field(w, "uri", encodeDocumentUri(imageUri(bufferUri, image)));
        w.endObject();
    }
    w.endArray();
    w.key("samplers");
    w.beginArray();
    for (const auto& image : doc.content->images) {
        w.beginObject();
        field(w, "minFilter", image.mipmapped ? 9987 : 9729);
        w.endObject();
    }
    w.endArray();
}

//======================================================================================================================
void detail::contentExtensionJson(JsonWriter& w, const SceneDocument& doc,
                                  std::string_view bufferUri) {
    if (doc.bounds) {
        w.key("bounds");
        w.beginObject();
        vector(w, "min", doc.bounds->first, 3);
        vector(w, "max", doc.bounds->second, 3);
        w.endObject();
    }
    if (!doc.content)
        return;
    w.key("contentHashes");
    w.beginObject();
    field(w, encodeDocumentUri(geometryUri(bufferUri)), doc.content->geometrySha256);
    for (const auto& image : doc.content->images)
        field(w, encodeDocumentUri(imageUri(bufferUri, image)), image.sha256);
    w.endObject();
}

} // namespace lmx::asset
