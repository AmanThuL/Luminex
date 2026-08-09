#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Engine/GeometryGenerator.h"
#include "EngineTestSupport.h"

using namespace lmx::engine;
using lmx::test::near3;

namespace {

//======================================================================================================================
// dot(geometric normal, stored normal) > 0 for every triangle pins both the winding (CCW as
// seen from outside) and per-vertex normals in one property -- same check RenderTests.cpp uses
// for Mesh::makeCube.
void requireConsistentWinding(const GeoData& mesh) {
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const VertexPNTU& a = mesh.vertices[mesh.indices[i]];
        const VertexPNTU& b = mesh.vertices[mesh.indices[i + 1]];
        const VertexPNTU& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 geometric = glm::cross(glm::vec3{b.px - a.px, b.py - a.py, b.pz - a.pz},
                                               glm::vec3{c.px - a.px, c.py - a.py, c.pz - a.pz});
        if (glm::length(geometric) < 1e-8f) {
            continue; // degenerate sliver (can happen adjacent to a pole); nothing to check
        }
        const glm::vec3 storedNormal =
            glm::normalize(glm::vec3{a.nx, a.ny, a.nz} + glm::vec3{b.nx, b.ny, b.nz} +
                           glm::vec3{c.nx, c.ny, c.nz});
        REQUIRE(glm::dot(glm::normalize(geometric), storedNormal) > 0.0f);
    }
}

} // namespace

//======================================================================================================================
TEST_CASE("grid(20,30,60,40) has the expected vertex/triangle counts", "[engine]") {
    const GeoData grid = makeGrid(20.0f, 30.0f, 60, 40);
    REQUIRE(grid.vertices.size() == 60 * 40);
    REQUIRE(grid.indices.size() == 59 * 39 * 2 * 3);
}

//======================================================================================================================
TEST_CASE("grid vertices carry +Y normals, +X tangents, and lie on the XZ plane", "[engine]") {
    const GeoData grid = makeGrid(20.0f, 30.0f, 60, 40);
    for (const VertexPNTU& v : grid.vertices) {
        REQUIRE(v.py == 0.0f);
        REQUIRE(near3({v.nx, v.ny, v.nz}, {0.0f, 1.0f, 0.0f}));
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}));
        REQUIRE(v.tw == 1.0f);
    }
}

//======================================================================================================================
TEST_CASE("grid spans its half extents", "[engine]") {
    const GeoData grid = makeGrid(20.0f, 30.0f, 60, 40);
    glm::vec3 lo{1e9f}, hi{-1e9f};
    for (const VertexPNTU& v : grid.vertices) {
        lo = glm::min(lo, {v.px, v.py, v.pz});
        hi = glm::max(hi, {v.px, v.py, v.pz});
    }
    REQUIRE(lo.x == Catch::Approx(-10.0f));
    REQUIRE(hi.x == Catch::Approx(10.0f));
    REQUIRE(lo.z == Catch::Approx(-15.0f));
    REQUIRE(hi.z == Catch::Approx(15.0f));
}

//======================================================================================================================
TEST_CASE("grid triangles wind CCW as seen from outside (+Y)", "[engine]") {
    requireConsistentWinding(makeGrid(20.0f, 30.0f, 60, 40));
}

//======================================================================================================================
TEST_CASE("cylinder(0.5,0.3,3.0,20,20) has the expected vertex and index counts", "[engine]") {
    const GeoData cyl = makeCylinder(0.5f, 0.3f, 3.0f, 20, 20);
    // Body: (stacks+1) rings * (slices+1) verts/ring = 21*21 = 441. Each cap adds slices+1 ring
    // verts + 1 center = 22. Body indices: stacks*slices*6 = 2400; each cap adds slices*3 = 60.
    REQUIRE(cyl.vertices.size() == 441 + 22 + 22);
    REQUIRE(cyl.indices.size() == 2400 + 60 + 60);
}

//======================================================================================================================
TEST_CASE("cylinder triangles wind CCW as seen from outside", "[engine]") {
    requireConsistentWinding(makeCylinder(0.5f, 0.3f, 3.0f, 20, 20));
}

//======================================================================================================================
TEST_CASE("sphere's first triangle winds CCW as seen from outside", "[engine]") {
    const GeoData sphere = makeSphere(2.0f, 20, 20);
    const VertexPNTU& v0 = sphere.vertices[sphere.indices[0]];
    const VertexPNTU& v1 = sphere.vertices[sphere.indices[1]];
    const VertexPNTU& v2 = sphere.vertices[sphere.indices[2]];
    const glm::vec3 p0{v0.px, v0.py, v0.pz};
    const glm::vec3 p1{v1.px, v1.py, v1.pz};
    const glm::vec3 p2{v2.px, v2.py, v2.pz};
    const glm::vec3 geometric = glm::cross(p1 - p0, p2 - p0);
    const glm::vec3 centroid = (p0 + p1 + p2) / 3.0f; // sphere is centered on the origin
    REQUIRE(glm::dot(geometric, centroid) > 0.0f);
}

//======================================================================================================================
TEST_CASE("sphere triangles wind CCW as seen from outside", "[engine]") {
    requireConsistentWinding(makeSphere(2.0f, 20, 20));
}

//======================================================================================================================
TEST_CASE("sphere vertices lie on the sphere of the given radius", "[engine]") {
    const GeoData sphere = makeSphere(2.0f, 12, 8);
    for (const VertexPNTU& v : sphere.vertices) {
        REQUIRE(glm::length(glm::vec3{v.px, v.py, v.pz}) == Catch::Approx(2.0f).margin(1e-4));
    }
}

//======================================================================================================================
TEST_CASE("cylinder cap ring tangents have handedness matching each cap's bitangent direction",
          "[engine]") {
    // Both caps share T=(1,0,0) and dP/dv along +Z (v = z/height + 0.5 for both rings), while N
    // flips between caps -- so B = w*cross(N,T) forces w = -1 on the top cap (N=(0,1,0)) and
    // w = +1 on the bottom cap (N=(0,-1,0)); see the derivation comment in
    // GeometryGenerator.cpp's addCylinderCap.
    constexpr uint32_t slices = 20, stacks = 20;
    const GeoData cyl = makeCylinder(0.5f, 0.3f, 3.0f, slices, stacks);

    const uint32_t ringVertexCount = slices + 1;
    const uint32_t bodyVertexCount = (stacks + 1) * ringVertexCount;
    const uint32_t topCapRingStart = bodyVertexCount;
    const uint32_t topCapCenter = topCapRingStart + ringVertexCount;
    const uint32_t bottomCapRingStart = topCapCenter + 1;
    const uint32_t bottomCapCenter = bottomCapRingStart + ringVertexCount;

    for (uint32_t i = 0; i < ringVertexCount; ++i) {
        REQUIRE(cyl.vertices[topCapRingStart + i].tw == -1.0f);
        REQUIRE(cyl.vertices[bottomCapRingStart + i].tw == 1.0f);
    }
    REQUIRE(cyl.vertices[topCapCenter].tw == -1.0f);
    REQUIRE(cyl.vertices[bottomCapCenter].tw == 1.0f);
}

//======================================================================================================================
TEST_CASE("grid corner UVs run 0..1 with (0,0) at -X-Z and (1,1) at +X+Z", "[engine]") {
    constexpr uint32_t m = 60, n = 40; // same grid as the other grid tests
    const GeoData grid = makeGrid(20.0f, 30.0f, m, n);
    const VertexPNTU& topLeft = grid.vertices[0];                       // i=0, j=0
    const VertexPNTU& topRight = grid.vertices[n - 1];                  // i=0, j=n-1
    const VertexPNTU& bottomLeft = grid.vertices[(m - 1) * n];          // i=m-1, j=0
    const VertexPNTU& bottomRight = grid.vertices[(m - 1) * n + n - 1]; // i=m-1, j=n-1

    REQUIRE(topLeft.u == 0.0f);
    REQUIRE(topLeft.v == 0.0f);
    REQUIRE(topRight.u == Catch::Approx(1.0f));
    REQUIRE(topRight.v == 0.0f);
    REQUIRE(bottomLeft.u == 0.0f);
    REQUIRE(bottomLeft.v == Catch::Approx(1.0f));
    REQUIRE(bottomRight.u == Catch::Approx(1.0f));
    REQUIRE(bottomRight.v == Catch::Approx(1.0f));
}

//======================================================================================================================
TEST_CASE("cylinder ring v-coordinate follows 1 - i/stacks", "[engine]") {
    constexpr uint32_t slices = 20, stacks = 20;
    const GeoData cyl = makeCylinder(0.5f, 0.3f, 3.0f, slices, stacks);
    const uint32_t ringVertexCount = slices + 1;

    auto ringV = [&](uint32_t ring) { return cyl.vertices[ring * ringVertexCount].v; };
    REQUIRE(ringV(0) == Catch::Approx(1.0f));      // bottom ring (i=0): v = 1 - 0/stacks = 1
    REQUIRE(ringV(stacks) == Catch::Approx(0.0f)); // top ring (i=stacks): v = 1 - 1 = 0

    constexpr uint32_t midRing = 5;
    const float expectedV = 1.0f - static_cast<float>(midRing) / static_cast<float>(stacks);
    for (uint32_t j = 0; j < ringVertexCount; ++j) {
        REQUIRE(cyl.vertices[midRing * ringVertexCount + j].v == Catch::Approx(expectedV));
    }
}

//======================================================================================================================
TEST_CASE("sphere(2,20,20) has the expected vertex and index counts", "[engine]") {
    const GeoData sphere = makeSphere(2.0f, 20, 20);
    // Poles: 2 verts. Rings: (stacks-1)=19 rings * (slices+1)=21 verts/ring.
    REQUIRE(sphere.vertices.size() == 1 + 19 * 21 + 1);
    // Top/bottom stacks: slices=20 triangles each. Inner stacks: (stacks-2)=18 bands * slices*2
    // triangles/band. 3 indices per triangle.
    REQUIRE(sphere.indices.size() == (20 + 18 * 20 * 2 + 20) * 3);
}
