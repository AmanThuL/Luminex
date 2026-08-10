//----------------------------------------------------------------------------------------------------------------------
/// @file GeometryGenerator.cpp
/// @brief Implements deterministic procedural geometry generation.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryGenerator.h"

#include "Core/Assert.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace lmx::engine {

namespace {

//======================================================================================================================
// Appends a cap with cap-specific normals, UVs, and outward-facing winding.
void addCylinderCap(GeoData& mesh, float radius, float height, uint32_t slices, bool topCap) {
    const auto baseIndex = static_cast<uint32_t>(mesh.vertices.size());
    const float y = topCap ? 0.5f * height : -0.5f * height;
    const float ny = topCap ? 1.0f : -1.0f;
    const float dTheta = 2.0f * glm::pi<float>() / static_cast<float>(slices);

    // Cap UVs increase toward +Z while the normal flips. With B = w * cross(N,T) and T = +X,
    // the top requires w=-1 and the bottom w=+1.
    const float tw = topCap ? -1.0f : 1.0f;

    // Duplicate the ring (rather than reuse the body's) because the cap's normal and UVs differ.
    for (uint32_t i = 0; i <= slices; ++i) {
        const float x = radius * std::cos(i * dTheta);
        const float z = radius * std::sin(i * dTheta);
        // Scale cap UVs by height to match the cylinder body's density.
        const float u = x / height + 0.5f;
        const float v = z / height + 0.5f;
        mesh.vertices.push_back({x, y, z, 0.0f, ny, 0.0f, 1.0f, 0.0f, 0.0f, tw, u, v});
    }
    mesh.vertices.push_back({0.0f, y, 0.0f, 0.0f, ny, 0.0f, 1.0f, 0.0f, 0.0f, tw, 0.5f, 0.5f});
    const auto centerIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);

    for (uint32_t i = 0; i < slices; ++i) {
        if (topCap) {
            mesh.indices.insert(mesh.indices.end(),
                                {centerIndex, baseIndex + i + 1, baseIndex + i});
        } else {
            mesh.indices.insert(mesh.indices.end(),
                                {centerIndex, baseIndex + i, baseIndex + i + 1});
        }
    }
}

} // namespace

//======================================================================================================================
GeoData makeGrid(float width, float depth, uint32_t m, uint32_t n) {
    LMX_ASSERT(m >= 2 && n >= 2, "makeGrid: need at least a 2x2 vertex grid");
    GeoData mesh;
    mesh.vertices.resize(static_cast<size_t>(m) * n);

    const float halfWidth = 0.5f * width;
    const float halfDepth = 0.5f * depth;
    const float dx = width / static_cast<float>(n - 1);
    const float dz = depth / static_cast<float>(m - 1);
    const float du = 1.0f / static_cast<float>(n - 1);
    const float dv = 1.0f / static_cast<float>(m - 1);

    for (uint32_t i = 0; i < m; ++i) {
        const float z = halfDepth - static_cast<float>(i) * dz;
        for (uint32_t j = 0; j < n; ++j) {
            const float x = -halfWidth + static_cast<float>(j) * dx;
            mesh.vertices[i * n + j] = {x,
                                        0.0f,
                                        z,
                                        0.0f,
                                        1.0f,
                                        0.0f,
                                        1.0f,
                                        0.0f,
                                        0.0f,
                                        1.0f,
                                        static_cast<float>(j) * du,
                                        static_cast<float>(i) * dv};
        }
    }

    const uint32_t faceCount = (m - 1) * (n - 1);
    mesh.indices.reserve(static_cast<size_t>(faceCount) * 6);
    for (uint32_t i = 0; i < m - 1; ++i) {
        for (uint32_t j = 0; j < n - 1; ++j) {
            mesh.indices.insert(mesh.indices.end(),
                                {i * n + j, i * n + j + 1, (i + 1) * n + j, (i + 1) * n + j,
                                 i * n + j + 1, (i + 1) * n + j + 1});
        }
    }
    return mesh;
}

//======================================================================================================================
GeoData makeCylinder(float bottomR, float topR, float height, uint32_t slices, uint32_t stacks) {
    LMX_ASSERT(slices >= 3 && stacks >= 1, "makeCylinder: need >=3 slices and >=1 stack");
    GeoData mesh;

    const float stackHeight = height / static_cast<float>(stacks);
    const float radiusStep = (topR - bottomR) / static_cast<float>(stacks);
    const uint32_t ringCount = stacks + 1;
    const float dTheta = 2.0f * glm::pi<float>() / static_cast<float>(slices);

    for (uint32_t i = 0; i < ringCount; ++i) {
        const float y = -0.5f * height + static_cast<float>(i) * stackHeight;
        const float r = bottomR + static_cast<float>(i) * radiusStep;
        for (uint32_t j = 0; j <= slices; ++j) {
            const float c = std::cos(j * dTheta);
            const float s = std::sin(j * dTheta);

            // T follows +theta. The height/radius derivative supplies B; cross(T,B) points out.
            const glm::vec3 tangent{-s, 0.0f, c};
            const float dr = bottomR - topR;
            const glm::vec3 bitangent{dr * c, -height, dr * s};
            const glm::vec3 normal = glm::normalize(glm::cross(tangent, bitangent));

            mesh.vertices.push_back({r * c, y, r * s, normal.x, normal.y, normal.z, tangent.x,
                                     tangent.y, tangent.z, 1.0f,
                                     static_cast<float>(j) / static_cast<float>(slices),
                                     1.0f - static_cast<float>(i) / static_cast<float>(stacks)});
        }
    }

    const uint32_t ringVertexCount = slices + 1;
    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            mesh.indices.insert(mesh.indices.end(),
                                {i * ringVertexCount + j, (i + 1) * ringVertexCount + j,
                                 (i + 1) * ringVertexCount + j + 1, i * ringVertexCount + j,
                                 (i + 1) * ringVertexCount + j + 1, i * ringVertexCount + j + 1});
        }
    }

    addCylinderCap(mesh, topR, height, slices, /*topCap=*/true);
    addCylinderCap(mesh, bottomR, height, slices, /*topCap=*/false);
    return mesh;
}

//======================================================================================================================
GeoData makeSphere(float radius, uint32_t slices, uint32_t stacks) {
    LMX_ASSERT(slices >= 3 && stacks >= 2, "makeSphere: need >=3 slices and >=2 stacks");
    GeoData mesh;

    // A rectangular texture necessarily collapses to one UV at each pole.
    mesh.vertices.push_back(
        {0.0f, radius, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});

    const float phiStep = glm::pi<float>() / static_cast<float>(stacks);
    const float thetaStep = 2.0f * glm::pi<float>() / static_cast<float>(slices);

    for (uint32_t i = 1; i <= stacks - 1; ++i) {
        const float phi = static_cast<float>(i) * phiStep;
        for (uint32_t j = 0; j <= slices; ++j) {
            const float theta = static_cast<float>(j) * thetaStep;

            const glm::vec3 pos{radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi),
                                radius * std::sin(phi) * std::sin(theta)};
            // Partial derivative of position with respect to theta.
            const glm::vec3 tangent =
                glm::normalize(glm::vec3{-radius * std::sin(phi) * std::sin(theta), 0.0f,
                                         radius * std::sin(phi) * std::cos(theta)});
            const glm::vec3 normal = glm::normalize(pos);

            mesh.vertices.push_back({pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, tangent.x,
                                     tangent.y, tangent.z, 1.0f, theta / glm::two_pi<float>(),
                                     phi / glm::pi<float>()});
        }
    }

    mesh.vertices.push_back(
        {0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f});

    for (uint32_t i = 1; i <= slices; ++i) {
        mesh.indices.insert(mesh.indices.end(), {0u, i + 1, i});
    }

    const uint32_t baseIndex = 1;
    const uint32_t ringVertexCount = slices + 1;
    for (uint32_t i = 0; i + 2 <= stacks - 1; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            mesh.indices.insert(
                mesh.indices.end(),
                {baseIndex + i * ringVertexCount + j, baseIndex + i * ringVertexCount + j + 1,
                 baseIndex + (i + 1) * ringVertexCount + j,
                 baseIndex + (i + 1) * ringVertexCount + j, baseIndex + i * ringVertexCount + j + 1,
                 baseIndex + (i + 1) * ringVertexCount + j + 1});
        }
    }

    const auto southPoleIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);
    const uint32_t bottomRingBase = southPoleIndex - ringVertexCount;
    for (uint32_t i = 0; i < slices; ++i) {
        mesh.indices.insert(mesh.indices.end(),
                            {southPoleIndex, bottomRingBase + i, bottomRingBase + i + 1});
    }

    return mesh;
}

} // namespace lmx::engine
