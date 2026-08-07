#include "Render/Mesh.h"

#include "Core/Assert.h"

#include <glm/glm.hpp>

namespace lmx::render {

namespace {

// Appends one face's 4 vertices and 6 indices (two CCW triangles) to a mesh under
// construction. u and v must be chosen so cross(u, v) == normal -- that is what pins the
// winding to CCW-viewed-from-outside for every face without special-casing any of them.
void addFace(MeshData& mesh, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v,
             const glm::vec3& normal, float halfExtent, const glm::vec3& color) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    const glm::vec3 corners[4] = {
        center - u * halfExtent - v * halfExtent,
        center + u * halfExtent - v * halfExtent,
        center + u * halfExtent + v * halfExtent,
        center - u * halfExtent + v * halfExtent,
    };
    for (const glm::vec3& p : corners) {
        mesh.vertices.push_back(
            {p.x, p.y, p.z, normal.x, normal.y, normal.z, color.r, color.g, color.b});
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh.indices.push_back(base + i);
    }
}

} // namespace

MeshData makeCube() {
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    constexpr float kHalf = 0.5f;
    const glm::vec3 white{1.0f, 1.0f, 1.0f};
    // One entry per face: {normal, u, v} with cross(u, v) == normal (see addFace). The face
    // center falls out as normal * kHalf, so it isn't listed separately.
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
        addFace(mesh, face.normal * kHalf, face.u, face.v, face.normal, kHalf, white);
    }
    return mesh;
}

MeshData makePlane(float halfExtent) {
    MeshData mesh;
    mesh.vertices.reserve(4);
    mesh.indices.reserve(6);

    const glm::vec3 lightGray{0.8f, 0.8f, 0.8f};
    // Same u/v-cross-product convention as makeCube's +Y face: u=+Z, v=+X gives
    // cross(u, v) == +Y, so the quad comes out CCW from above without a special case.
    addFace(mesh, glm::vec3{0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            halfExtent, lightGray);
    return mesh;
}

rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label) {
    LMX_ASSERT(!data.vertices.empty() && !data.indices.empty(),
               "createMesh: mesh data must not be empty");
    Mesh mesh;
    const std::string base(label);
    // BufferDesc.label is a string_view over `base`; safe because createBuffer consumes it
    // synchronously (copies/uses it before returning), so `base` outliving the call is enough.
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
