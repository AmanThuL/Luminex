#pragma once
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lmx::render {

// Mirrors the packed MSL layout Slang emits for Shaders/Mesh.slang (measured in Task 8):
// three packed_float3 fields, stride 36.
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
};
static_assert(sizeof(Vertex) == 36, "vertex stride must match the shader's packed layout");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

MeshData makeCube(); // unit cube at origin, 24 verts / 36 indices, CCW, per-face normals
MeshData makePlane(float halfExtent); // XZ plane at y=0, 4 verts / 6 indices, +Y normal

struct Mesh {
    std::unique_ptr<rhi::Buffer> vertexBuffer;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    uint32_t indexCount = 0;
};

rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label);

} // namespace lmx::render
