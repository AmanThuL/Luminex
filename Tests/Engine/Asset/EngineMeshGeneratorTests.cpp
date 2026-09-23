#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include "Support/EngineTestSupport.h"

#include "Engine/Asset/Texture/Ibl.h"
#include "Engine/Geometry/Mesh.h"

#include <cmath>

using lmx::test::near3;

namespace {
constexpr float kEps = 1e-5f;
} // namespace

//======================================================================================================================
TEST_CASE("cube mesh has 24 vertices, 36 CCW indices, unit bounds", "[render]") {
    const lmx::engine::MeshData cube = lmx::engine::makeCube();
    REQUIRE(cube.vertices.size() == 24);
    REQUIRE(cube.indices.size() == 36);
    glm::vec3 lo{1e9f}, hi{-1e9f};
    for (const lmx::engine::Vertex& v : cube.vertices) {
        lo = glm::min(lo, {v.px, v.py, v.pz});
        hi = glm::max(hi, {v.px, v.py, v.pz});
    }
    REQUIRE(near3(lo, {-0.5f, -0.5f, -0.5f}, kEps));
    REQUIRE(near3(hi, {0.5f, 0.5f, 0.5f}, kEps));
    // Every triangle's geometric normal must agree with its vertices' stored normal --
    // this pins both winding (CCW from outside) and per-face normals in one property.
    for (size_t i = 0; i < cube.indices.size(); i += 3) {
        const lmx::engine::Vertex& a = cube.vertices[cube.indices[i]];
        const lmx::engine::Vertex& b = cube.vertices[cube.indices[i + 1]];
        const lmx::engine::Vertex& c = cube.vertices[cube.indices[i + 2]];
        const glm::vec3 geometric =
            glm::normalize(glm::cross(glm::vec3{b.px - a.px, b.py - a.py, b.pz - a.pz},
                                      glm::vec3{c.px - a.px, c.py - a.py, c.pz - a.pz}));
        REQUIRE(glm::dot(geometric, {a.nx, a.ny, a.nz}) > 0.99f);
    }
    // The PNTU tail. These two procedural meshes carry no texture and never get a normal map --
    // the scene shader's normal-map branch is off for them (MaterialRow normal-map flag clear) --
    // so the tangent is a fixed placeholder rather than a per-face frame, and the uv is 0.
    // Pinned so that a future generator change has to say so out loud.
    for (const lmx::engine::Vertex& v : cube.vertices) {
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}, kEps));
        REQUIRE(v.tw == 1.0f);
        REQUIRE(v.u == 0.0f);
        REQUIRE(v.v == 0.0f);
    }
}

//======================================================================================================================
TEST_CASE("plane mesh spans its half extent with +Y normals", "[render]") {
    const lmx::engine::MeshData plane = lmx::engine::makePlane(5.0f);
    REQUIRE(plane.vertices.size() == 4);
    REQUIRE(plane.indices.size() == 6);
    for (const lmx::engine::Vertex& v : plane.vertices) {
        REQUIRE(v.py == 0.0f);
        REQUIRE(near3({v.nx, v.ny, v.nz}, {0.0f, 1.0f, 0.0f}, kEps));
        REQUIRE(std::abs(v.px) == Catch::Approx(5.0f));
        REQUIRE(std::abs(v.pz) == Catch::Approx(5.0f));
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}, kEps));
        REQUIRE(v.tw == 1.0f);
        REQUIRE(v.u == 0.0f);
        REQUIRE(v.v == 0.0f);
    }
}

//======================================================================================================================
// Shaders/Passes/Scene/ScenePass.slang hardcodes the prefiltered chain's level count so the
// roughness-to-mip map is a compile-time constant in the fragment. Nothing links the two, so this
// is the statement that the shader and the generator agree; a chain generated at a different depth
// would otherwise shift every specular lookup by a fraction of a mip with no build error.
TEST_CASE("the scene shader's specular mip count matches the IBL generator's", "[render]") {
    REQUIRE(lmx::asset::ibl::kSpecularMipCount == 5);
}
