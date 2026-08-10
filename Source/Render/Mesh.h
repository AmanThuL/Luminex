//----------------------------------------------------------------------------------------------------------------------
/// @file Mesh.h
/// @brief Declares renderer vertex, mesh-data, and GPU mesh helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lmx::render {

/// Packed position, normal, tangent, and UV layout consumed by scene shaders.
struct Vertex {
    float px, py, pz;     ///< Object-space position.
    float nx, ny, nz;     ///< Object-space unit normal.
    float tx, ty, tz, tw; ///< xyz tangent, w handedness (+1/-1)
    float u, v;           ///< Primary texture coordinates.
};
static_assert(sizeof(Vertex) == 48, "vertex stride must match the shader's packed layout");

/// CPU-side indexed geometry ready for GPU upload.
struct MeshData {
    std::vector<Vertex> vertices;  ///< Packed vertex stream.
    std::vector<uint32_t> indices; ///< Triangle-list indices.
};

/// Converts compatible engine geometry into the renderer's packed mesh representation.
template <typename Geo>
MeshData fromGeo(const Geo& geo) {
    MeshData data;
    data.vertices.reserve(geo.vertices.size());
    for (const auto& v : geo.vertices) {
        data.vertices.push_back(
            {v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.tx, v.ty, v.tz, v.tw, v.u, v.v});
    }
    data.indices = geo.indices;
    return data;
}

/// Builds a unit cube at the origin with CCW indices and per-face normals.
MeshData makeCube();
/// Builds an XZ plane at y=0 with the requested half extent and a +Y normal.
MeshData makePlane(float halfExtent);

/// Owns the GPU buffers and draw count for one indexed mesh.
struct Mesh {
    std::unique_ptr<rhi::Buffer> vertexBuffer; ///< Owned GPU vertex storage.
    std::unique_ptr<rhi::Buffer> indexBuffer;  ///< Owned GPU index storage.
    uint32_t indexCount = 0;                   ///< Number of indices submitted per draw.
};

/// Uploads CPU mesh data into labelled vertex and index buffers.
rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label);

} // namespace lmx::render
