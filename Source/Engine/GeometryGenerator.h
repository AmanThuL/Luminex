//----------------------------------------------------------------------------------------------------------------------
/// @file GeometryGenerator.h
/// @brief Declares procedural geometry data and generation helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <vector>

namespace lmx::engine {

/// Packed position, normal, tangent, and UV vertex shared with renderer mesh data.
struct VertexPNTU {
    float px, py, pz;     ///< Object-space position.
    float nx, ny, nz;     ///< Object-space unit normal.
    float tx, ty, tz, tw; ///< xyz tangent, w handedness (+1/-1)
    float u, v;           ///< Primary texture coordinates.
};
static_assert(sizeof(VertexPNTU) == 48, "vertex stride must match the shader's packed layout");

/// CPU geometry payload containing indexed packed vertices.
struct GeoData {
    std::vector<VertexPNTU> vertices; ///< Packed vertex stream.
    std::vector<uint32_t> indices;    ///< Triangle-list indices.
};

/// Generates a centered XZ grid with `m` by `n` vertices.
GeoData makeGrid(float width, float depth, uint32_t m, uint32_t n);
/// Generates a Y-axis cylinder or frustum with the requested tessellation.
GeoData makeCylinder(float bottomR, float topR, float height, uint32_t slices, uint32_t stacks);
/// Generates an indexed sphere suitable for scene or sky geometry.
GeoData makeSphere(float radius, uint32_t slices, uint32_t stacks);

} // namespace lmx::engine
