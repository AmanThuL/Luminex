#pragma once
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lmx::render {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw; // xyz tangent, w handedness (+1/-1)
    float u, v;
};
static_assert(sizeof(Vertex) == 48, "vertex stride must match the shader's packed layout");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

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

MeshData makeCube(); // unit cube at origin, 24 verts / 36 indices, CCW, per-face normals
MeshData makePlane(float halfExtent); // XZ plane at y=0, 4 verts / 6 indices, +Y normal

struct Mesh {
    std::unique_ptr<rhi::Buffer> vertexBuffer;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    uint32_t indexCount = 0;
};

rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label);

} // namespace lmx::render
