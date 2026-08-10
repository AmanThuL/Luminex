//----------------------------------------------------------------------------------------------------------------------
/// @file Mesh.cpp
/// @brief Builds built-in mesh data and uploads renderer meshes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Mesh.h"

#include "Core/Assert.h"

#include <glm/glm.hpp>

namespace lmx::render {

namespace {

constexpr float kPlaceholderTangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};

//======================================================================================================================
// cross(u, v) == normal gives every generated face an outward CCW winding.
void addFace(MeshData& mesh, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v,
             const glm::vec3& normal, float halfExtent) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    const glm::vec3 corners[4] = {
        center - u * halfExtent - v * halfExtent,
        center + u * halfExtent - v * halfExtent,
        center + u * halfExtent + v * halfExtent,
        center - u * halfExtent + v * halfExtent,
    };
    for (const glm::vec3& p : corners) {
        mesh.vertices.push_back({p.x, p.y, p.z, normal.x, normal.y, normal.z,
                                 kPlaceholderTangent[0], kPlaceholderTangent[1],
                                 kPlaceholderTangent[2], kPlaceholderTangent[3], 0.0f, 0.0f});
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh.indices.push_back(base + i);
    }
}

} // namespace

//======================================================================================================================
MeshData makeCube() {
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    constexpr float kHalf = 0.5f;
    // Each basis satisfies cross(u, v) == normal; the center is normal * kHalf.
    struct Face {
        glm::vec3 normal, u, v;
    };
    const Face faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // +X
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}, // -X
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},  // +Y
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // -Y
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // +Z
        {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // -Z
    };
    for (const Face& face : faces) {
        addFace(mesh, face.normal * kHalf, face.u, face.v, face.normal, kHalf);
    }
    return mesh;
}

//======================================================================================================================
MeshData makePlane(float halfExtent) {
    MeshData mesh;
    mesh.vertices.reserve(4);
    mesh.indices.reserve(6);

    // u=+Z and v=+X produce an upward normal and CCW winding from above.
    addFace(mesh, glm::vec3{0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            halfExtent);
    return mesh;
}

//======================================================================================================================
rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label) {
    LMX_ASSERT(!data.vertices.empty() && !data.indices.empty(),
               "createMesh: mesh data must not be empty");
    Mesh mesh;
    const std::string base(label);
    // Temporary label strings outlive each synchronous createBuffer call.
    auto vertices = device.createBuffer(
        {.size = data.vertices.size() * sizeof(Vertex), .label = base + ".vertices"},
        data.vertices.data());
    if (!vertices) {
        return std::unexpected(vertices.error());
    }
    mesh.vertexBuffer = std::move(*vertices);
    auto indices = device.createBuffer(
        {.size = data.indices.size() * sizeof(uint32_t), .label = base + ".indices"},
        data.indices.data());
    if (!indices) {
        return std::unexpected(indices.error());
    }
    mesh.indexBuffer = std::move(*indices);
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    return mesh;
}

} // namespace lmx::render
