#pragma once

#include <cstdint>
#include <vector>

namespace lmx::engine {

struct VertexPNTU {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw; // xyz tangent, w handedness (+1/-1)
    float u, v;
};
static_assert(sizeof(VertexPNTU) == 48, "vertex stride must match the shader's packed layout");

struct GeoData {
    std::vector<VertexPNTU> vertices;
    std::vector<uint32_t> indices;
};

GeoData makeGrid(float width, float depth, uint32_t m, uint32_t n); // lumine CreateGrid port
GeoData makeCylinder(float bottomR, float topR, float height, uint32_t slices, uint32_t stacks);
GeoData makeSphere(float radius, uint32_t slices, uint32_t stacks); // sky sphere

} // namespace lmx::engine
