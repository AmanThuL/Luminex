#include "Engine/GltfLoader.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace lmx::engine {

namespace {

//======================================================================================================================
AssetError makeError(std::string_view path, std::string_view reason,
                     AssetErrorCode code = AssetErrorCode::Malformed) {
    return AssetError{code, std::string(path) + ": " + std::string(reason)};
}

//======================================================================================================================
const char* cgltfResultMessage(cgltf_result result) {
    switch (result) {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_invalid_options:
        return "invalid cgltf_options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "I/O error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "legacy (pre-2.0) glTF is not supported";
    default:
        return "unknown cgltf_result";
    }
}

//======================================================================================================================
AssetErrorCode cgltfErrorCode(cgltf_result result) {
    switch (result) {
    case cgltf_result_file_not_found:
        return AssetErrorCode::NotFound;
    case cgltf_result_io_error:
    case cgltf_result_out_of_memory:
        return AssetErrorCode::Io;
    case cgltf_result_legacy_gltf:
    case cgltf_result_unknown_format:
        return AssetErrorCode::Unsupported;
    case cgltf_result_success:
    case cgltf_result_data_too_short:
    case cgltf_result_invalid_json:
    case cgltf_result_invalid_gltf:
    case cgltf_result_invalid_options:
    default:
        return AssetErrorCode::Malformed;
    }
}

//======================================================================================================================
// cgltf loads buffers but not images. Support buffer views and relative file URIs; reject data
// URIs explicitly because the asset pipeline does not consume them.
AssetResult<GltfImage> decodeImage(const cgltf_image& image, const std::filesystem::path& baseDir,
                                   std::string_view gltfPath) {
    std::vector<unsigned char> encoded;
    if (image.buffer_view != nullptr) {
        const uint8_t* data = cgltf_buffer_view_data(image.buffer_view);
        if (data == nullptr) {
            return std::unexpected(makeError(gltfPath, "image buffer_view has no data"));
        }
        encoded.assign(data, data + image.buffer_view->size);
    } else if (image.uri != nullptr) {
        std::string uri(image.uri);
        const cgltf_size decodedLen = cgltf_decode_uri(uri.data());
        uri.resize(decodedLen);
        if (uri.starts_with("data:")) {
            return std::unexpected(
                makeError(gltfPath,
                          "data-URI images are not supported (use buffer_view or an "
                          "external file)",
                          AssetErrorCode::Unsupported));
        }
        const std::filesystem::path imagePath = baseDir / uri;
        std::ifstream file(imagePath, std::ios::binary);
        if (!file) {
            return std::unexpected(makeError(
                gltfPath, "image file not found: " + imagePath.string(), AssetErrorCode::NotFound));
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size <= 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max()) {
            return std::unexpected(makeError(
                gltfPath, "image file size is invalid: " + imagePath.string(), AssetErrorCode::Io));
        }
        encoded.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(encoded.data()), size);
        if (!file) {
            return std::unexpected(makeError(
                gltfPath, "failed to read image: " + imagePath.string(), AssetErrorCode::Io));
        }
    } else {
        return std::unexpected(makeError(gltfPath, "image has neither buffer_view nor uri"));
    }

    if (encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(makeError(gltfPath, "encoded image size is invalid"));
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                                  &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return std::unexpected(makeError(
            gltfPath, std::string("stb_image failed to decode image: ") + stbi_failure_reason()));
    }
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / (static_cast<size_t>(height) * 4)) {
        stbi_image_free(pixels);
        return std::unexpected(makeError(gltfPath, "decoded image dimensions are invalid"));
    }

    GltfImage out;
    out.width = static_cast<uint32_t>(width);
    out.height = static_cast<uint32_t>(height);
    out.rgba8.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    std::memcpy(out.rgba8.data(), pixels, out.rgba8.size());
    stbi_image_free(pixels);
    return out;
}

//======================================================================================================================
// Accumulate UV derivatives per vertex, Gram-Schmidt orthogonalize against the normal, and derive
// handedness from the accumulated bitangent.
void generateTangents(GeoData& mesh) {
    const size_t vertexCount = mesh.vertices.size();
    std::vector<glm::vec3> tangentAccum(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> bitangentAccum(vertexCount, glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
        const VertexPNTU& v0 = mesh.vertices[i0];
        const VertexPNTU& v1 = mesh.vertices[i1];
        const VertexPNTU& v2 = mesh.vertices[i2];
        const glm::vec3 p0{v0.px, v0.py, v0.pz};
        const glm::vec3 p1{v1.px, v1.py, v1.pz};
        const glm::vec3 p2{v2.px, v2.py, v2.pz};
        const glm::vec3 e1 = p1 - p0;
        const glm::vec3 e2 = p2 - p0;
        const float du1 = v1.u - v0.u, dv1 = v1.v - v0.v;
        const float du2 = v2.u - v0.u, dv2 = v2.v - v0.v;
        const float denom = du1 * dv2 - du2 * dv1;
        if (std::abs(denom) < 1e-8f) {
            continue; // degenerate UV mapping on this triangle; contributes nothing
        }
        const float r = 1.0f / denom;
        const glm::vec3 tangent = (e1 * dv2 - e2 * dv1) * r;
        const glm::vec3 bitangent = (e2 * du1 - e1 * du2) * r;
        tangentAccum[i0] += tangent;
        tangentAccum[i1] += tangent;
        tangentAccum[i2] += tangent;
        bitangentAccum[i0] += bitangent;
        bitangentAccum[i1] += bitangent;
        bitangentAccum[i2] += bitangent;
    }

    for (size_t i = 0; i < vertexCount; ++i) {
        VertexPNTU& v = mesh.vertices[i];
        glm::vec3 n{v.nx, v.ny, v.nz};
        n = glm::length(n) < 1e-8f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::normalize(n);
        glm::vec3 t = tangentAccum[i];
        float w = 1.0f;
        if (glm::length(t) >= 1e-8f) {
            t = t - n * glm::dot(n, t); // Gram-Schmidt orthogonalize against the normal
        }
        if (glm::length(t) < 1e-8f) {
            // Degenerate UVs fall back to an arbitrary tangent orthogonal to the normal.
            glm::vec3 up{0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, n)) > 0.999f) {
                up = glm::vec3{1.0f, 0.0f, 0.0f};
            }
            t = glm::cross(up, n);
        } else {
            const glm::vec3 b = bitangentAccum[i];
            w = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
        }
        t = glm::normalize(t);
        v.tx = t.x;
        v.ty = t.y;
        v.tz = t.z;
        v.tw = w;
    }
}

//======================================================================================================================
AssetResult<GeoData> decodePrimitive(const cgltf_primitive& prim, std::string_view gltfPath,
                                     const std::string& label) {
    if (prim.type != cgltf_primitive_type_triangles) {
        return std::unexpected(makeError(gltfPath,
                                         label + ": only triangle primitives are supported",
                                         AssetErrorCode::Unsupported));
    }

    const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
    if (posAcc == nullptr) {
        return std::unexpected(
            makeError(gltfPath, label + ": primitive has no POSITION attribute"));
    }
    const cgltf_accessor* normAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
    if (normAcc == nullptr) {
        return std::unexpected(makeError(gltfPath, label + ": primitive has no NORMAL attribute"));
    }
    if (normAcc->count != posAcc->count) {
        return std::unexpected(
            makeError(gltfPath, label + ": NORMAL count does not match POSITION count"));
    }
    const cgltf_accessor* uvAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);
    if (uvAcc != nullptr && uvAcc->count != posAcc->count) {
        return std::unexpected(
            makeError(gltfPath, label + ": TEXCOORD_0 count does not match POSITION count"));
    }
    const cgltf_accessor* tanAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0);
    if (tanAcc != nullptr && tanAcc->count != posAcc->count) {
        return std::unexpected(
            makeError(gltfPath, label + ": TANGENT count does not match POSITION count"));
    }

    const size_t vertexCount = posAcc->count;

    // cgltf unpack reports the number of elements written. Check it because sparse or missing
    // buffer views can otherwise become zero-filled geometry.
    std::vector<float> positions(vertexCount * 3);
    if (cgltf_accessor_unpack_floats(posAcc, positions.data(), positions.size()) !=
        positions.size()) {
        return std::unexpected(makeError(
            gltfPath,
            label + ": POSITION unpack failed (sparse/compressed/type-mismatched accessor?)"));
    }
    std::vector<float> normals(vertexCount * 3);
    if (cgltf_accessor_unpack_floats(normAcc, normals.data(), normals.size()) != normals.size()) {
        return std::unexpected(makeError(
            gltfPath,
            label + ": NORMAL unpack failed (sparse/compressed/type-mismatched accessor?)"));
    }
    std::vector<float> uvs;
    if (uvAcc != nullptr) {
        uvs.resize(vertexCount * 2);
        if (cgltf_accessor_unpack_floats(uvAcc, uvs.data(), uvs.size()) != uvs.size()) {
            return std::unexpected(makeError(gltfPath, label + ": TEXCOORD_0 unpack failed "
                                                               "(sparse/compressed/type-mismatched "
                                                               "accessor?)"));
        }
    }
    std::vector<float> tangents;
    if (tanAcc != nullptr) {
        tangents.resize(vertexCount * 4);
        if (cgltf_accessor_unpack_floats(tanAcc, tangents.data(), tangents.size()) !=
            tangents.size()) {
            return std::unexpected(makeError(
                gltfPath,
                label + ": TANGENT unpack failed (sparse/compressed/type-mismatched accessor?)"));
        }
    }

    GeoData mesh;
    mesh.vertices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        VertexPNTU& v = mesh.vertices[i];
        v.px = positions[i * 3 + 0];
        v.py = positions[i * 3 + 1];
        v.pz = positions[i * 3 + 2];
        v.nx = normals[i * 3 + 0];
        v.ny = normals[i * 3 + 1];
        v.nz = normals[i * 3 + 2];
        v.u = uvs.empty() ? 0.0f : uvs[i * 2 + 0];
        v.v = uvs.empty() ? 0.0f : uvs[i * 2 + 1];
        if (!tangents.empty()) {
            v.tx = tangents[i * 4 + 0];
            v.ty = tangents[i * 4 + 1];
            v.tz = tangents[i * 4 + 2];
            v.tw = tangents[i * 4 + 3];
        } else {
            v.tx = v.ty = v.tz = v.tw = 0.0f;
        }
    }

    if (prim.indices != nullptr) {
        mesh.indices.resize(prim.indices->count);
        if (cgltf_accessor_unpack_indices(prim.indices, mesh.indices.data(), sizeof(uint32_t),
                                          prim.indices->count) != prim.indices->count) {
            return std::unexpected(makeError(
                gltfPath,
                label + ": indices unpack failed (sparse/compressed/type-mismatched accessor?)"));
        }
    } else {
        if (vertexCount % 3 != 0) {
            return std::unexpected(makeError(
                gltfPath, label + ": non-indexed primitive vertex count is not a multiple of 3"));
        }
        mesh.indices.resize(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            mesh.indices[i] = i;
        }
    }

    if (tanAcc == nullptr) {
        generateTangents(mesh);
    }

    return mesh;
}

//======================================================================================================================
glm::mat4 nodeWorldMatrix(const cgltf_node& node) {
    float m[16];
    cgltf_node_transform_world(&node, m);
    return glm::make_mat4(m);
}

//======================================================================================================================
AssetResult<void> collectActiveNode(const cgltf_data& data, const cgltf_node& node,
                                    std::string_view path, std::vector<bool>& visitedNodes,
                                    std::vector<bool>& usedMeshes,
                                    std::vector<const cgltf_node*>& activeNodes) {
    const cgltf_size nodeIndex = cgltf_node_index(&data, &node);
    if (nodeIndex >= data.nodes_count) {
        return std::unexpected(makeError(path, "active scene references an invalid node"));
    }
    if (visitedNodes[nodeIndex]) {
        return std::unexpected(makeError(path, "active scene contains a repeated or cyclic node"));
    }
    visitedNodes[nodeIndex] = true;
    activeNodes.push_back(&node);

    if (node.mesh != nullptr) {
        const cgltf_size meshIndex = cgltf_mesh_index(&data, node.mesh);
        if (meshIndex >= data.meshes_count) {
            return std::unexpected(makeError(path, "active node references an invalid mesh"));
        }
        usedMeshes[meshIndex] = true;
    }

    for (cgltf_size i = 0; i < node.children_count; ++i) {
        if (node.children[i] == nullptr) {
            return std::unexpected(makeError(path, "active node has a null child"));
        }
        if (auto result = collectActiveNode(data, *node.children[i], path, visitedNodes, usedMeshes,
                                            activeNodes);
            !result) {
            return result;
        }
    }
    return {};
}

} // namespace

//======================================================================================================================
AssetResult<GltfScene> loadGltf(std::string_view path) {
    const std::string pathStr(path);

    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    const cgltf_result parseResult = cgltf_parse_file(&options, pathStr.c_str(), &rawData);
    if (parseResult != cgltf_result_success) {
        return std::unexpected(
            makeError(path, cgltfResultMessage(parseResult), cgltfErrorCode(parseResult)));
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);

    const cgltf_result bufResult = cgltf_load_buffers(&options, data.get(), pathStr.c_str());
    if (bufResult != cgltf_result_success) {
        return std::unexpected(
            makeError(path, std::string("failed to load buffers: ") + cgltfResultMessage(bufResult),
                      cgltfErrorCode(bufResult)));
    }

    const cgltf_result validateResult = cgltf_validate(data.get());
    if (validateResult != cgltf_result_success) {
        return std::unexpected(makeError(
            path, std::string("glTF validation failed: ") + cgltfResultMessage(validateResult),
            cgltfErrorCode(validateResult)));
    }

    GltfScene scene;
    const std::filesystem::path baseDir = std::filesystem::path(pathStr).parent_path();

    const cgltf_scene* activeScene = data->scene;
    if (activeScene == nullptr && data->scenes_count > 0) {
        activeScene = &data->scenes[0];
    }
    if (activeScene == nullptr) {
        return std::unexpected(makeError(path, "glTF contains no scene"));
    }

    std::vector<bool> visitedNodes(data->nodes_count, false);
    std::vector<bool> usedMeshes(data->meshes_count, false);
    std::vector<const cgltf_node*> activeNodes;
    for (cgltf_size i = 0; i < activeScene->nodes_count; ++i) {
        if (activeScene->nodes[i] == nullptr) {
            return std::unexpected(makeError(path, "active scene has a null root node"));
        }
        if (auto result = collectActiveNode(*data, *activeScene->nodes[i], path, visitedNodes,
                                            usedMeshes, activeNodes);
            !result) {
            return std::unexpected(result.error());
        }
    }

    std::vector<bool> usedMaterials(data->materials_count, false);
    bool needsDefaultMaterial = false;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        if (!usedMeshes[meshIndex]) {
            continue;
        }
        for (const cgltf_primitive& primitive : std::span(
                 data->meshes[meshIndex].primitives, data->meshes[meshIndex].primitives_count)) {
            if (primitive.material == nullptr) {
                needsDefaultMaterial = true;
                continue;
            }
            const cgltf_size materialIndex = cgltf_material_index(data.get(), primitive.material);
            if (materialIndex >= data->materials_count) {
                return std::unexpected(makeError(path, "primitive references an invalid material"));
            }
            usedMaterials[materialIndex] = true;
        }
    }

    scene.images.resize(data->images_count);
    std::vector<bool> referencedImages(data->images_count, false);
    std::vector<uint32_t> materialFlatIndex(data->materials_count,
                                            std::numeric_limits<uint32_t>::max());
    const auto imageIndex = [&](const cgltf_texture_view& view,
                                std::string_view slot) -> AssetResult<int> {
        if (view.texture == nullptr || view.texture->image == nullptr) {
            return -1;
        }
        const cgltf_size index = cgltf_image_index(data.get(), view.texture->image);
        if (index >= data->images_count) {
            return std::unexpected(
                makeError(path, std::string(slot) + " references an invalid image"));
        }
        return static_cast<int>(index);
    };
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        if (!usedMaterials[i]) {
            continue;
        }
        const cgltf_material& mat = data->materials[i];
        GltfMaterial out;
        if (mat.has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = mat.pbr_metallic_roughness;
            out.baseColorFactor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                            pbr.base_color_factor[2], pbr.base_color_factor[3]);
            out.metallic = pbr.metallic_factor;
            out.roughness = pbr.roughness_factor;
            auto index = imageIndex(pbr.base_color_texture, "base-color texture");
            if (!index) {
                return std::unexpected(index.error());
            }
            out.baseColorImage = *index;
            auto mrIndex = imageIndex(pbr.metallic_roughness_texture, "metallic-roughness texture");
            if (!mrIndex) {
                return std::unexpected(mrIndex.error());
            }
            out.metallicRoughnessImage = *mrIndex;
        }
        auto normalIndex = imageIndex(mat.normal_texture, "normal texture");
        if (!normalIndex) {
            return std::unexpected(normalIndex.error());
        }
        out.normalImage = *normalIndex;
        auto occlusionIndex = imageIndex(mat.occlusion_texture, "occlusion texture");
        if (!occlusionIndex) {
            return std::unexpected(occlusionIndex.error());
        }
        out.occlusionImage = *occlusionIndex;
        auto emissiveIndex = imageIndex(mat.emissive_texture, "emissive texture");
        if (!emissiveIndex) {
            return std::unexpected(emissiveIndex.error());
        }
        out.emissiveImage = *emissiveIndex;
        out.emissiveFactor =
            glm::vec3(mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2]);
        materialFlatIndex[i] = static_cast<uint32_t>(scene.materials.size());
        scene.materials.push_back(out);
        if (out.baseColorImage >= 0) {
            referencedImages[static_cast<size_t>(out.baseColorImage)] = true;
        }
        if (out.normalImage >= 0) {
            referencedImages[static_cast<size_t>(out.normalImage)] = true;
        }
        if (out.metallicRoughnessImage >= 0) {
            referencedImages[static_cast<size_t>(out.metallicRoughnessImage)] = true;
        }
        if (out.occlusionImage >= 0) {
            referencedImages[static_cast<size_t>(out.occlusionImage)] = true;
        }
        if (out.emissiveImage >= 0) {
            referencedImages[static_cast<size_t>(out.emissiveImage)] = true;
        }
    }
    const uint32_t defaultMaterialIndex = static_cast<uint32_t>(scene.materials.size());
    if (needsDefaultMaterial) {
        scene.materials.push_back(GltfMaterial{});
    }

    // Preserve source image indices while decoding only sampled texture slots.
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (!referencedImages[i]) {
            continue;
        }
        auto image = decodeImage(data->images[i], baseDir, path);
        if (!image) {
            return std::unexpected(image.error());
        }
        scene.images[i] = std::move(*image);
    }

    std::vector<std::vector<uint32_t>> primitiveFlatIndex(data->meshes_count);
    std::vector<std::vector<uint32_t>> primitiveMaterialIndex(data->meshes_count);
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        if (!usedMeshes[m]) {
            continue;
        }
        const cgltf_mesh& mesh = data->meshes[m];
        primitiveFlatIndex[m].resize(mesh.primitives_count);
        primitiveMaterialIndex[m].resize(mesh.primitives_count);
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            const std::string label = pathStr + " mesh '" +
                                      (mesh.name != nullptr ? mesh.name : "unnamed") +
                                      "' primitive " + std::to_string(p);
            auto geo = decodePrimitive(prim, path, label);
            if (!geo) {
                return std::unexpected(geo.error());
            }
            primitiveFlatIndex[m][p] = static_cast<uint32_t>(scene.meshes.size());
            scene.meshes.push_back(std::move(*geo));

            uint32_t matIdx = 0;
            if (prim.material != nullptr) {
                const cgltf_size sourceIndex = cgltf_material_index(data.get(), prim.material);
                if (sourceIndex >= materialFlatIndex.size() ||
                    materialFlatIndex[sourceIndex] == std::numeric_limits<uint32_t>::max()) {
                    return std::unexpected(makeError(path, "primitive material was not collected"));
                }
                matIdx = materialFlatIndex[sourceIndex];
            } else {
                matIdx = defaultMaterialIndex;
            }
            primitiveMaterialIndex[m][p] = matIdx;
        }
    }

    for (const cgltf_node* node : activeNodes) {
        if (node->mesh == nullptr) {
            continue;
        }
        const cgltf_size meshIdx = cgltf_mesh_index(data.get(), node->mesh);
        const glm::mat4 world = nodeWorldMatrix(*node);
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            scene.instances.push_back({.meshIndex = primitiveFlatIndex[meshIdx][p],
                                       .materialIndex = primitiveMaterialIndex[meshIdx][p],
                                       .world = world});
        }
    }

    return scene;
}

//======================================================================================================================
glm::vec3 fresnelFromMetallic(const glm::vec4& baseColor, float metallic) {
    return glm::mix(glm::vec3(0.04f), glm::vec3(baseColor), metallic);
}

} // namespace lmx::engine
