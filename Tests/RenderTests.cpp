#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>

#include "Render/Camera.h"
#include "Render/Mesh.h"

using namespace lmx::render;

namespace {
constexpr float kEps = 1e-5f;
bool near3(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEps));
}
} // namespace

TEST_CASE("default camera looks down -Z", "[render]") {
    Camera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    REQUIRE(near3(camera.forward(), {0.0f, 0.0f, -1.0f}));
    REQUIRE(near3(camera.right(), {1.0f, 0.0f, 0.0f}));
}

TEST_CASE("view matrix moves the world opposite the camera", "[render]") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    // A point 1 unit in front of the camera lands 1 unit down the view -Z axis.
    const glm::vec4 p = camera.viewMatrix() * glm::vec4(0.0f, 0.0f, 4.0f, 1.0f);
    REQUIRE(near3(glm::vec3(p), {0.0f, 0.0f, -1.0f}));
}

TEST_CASE("projection maps near to 0 and far to 1 -- Metal depth range", "[render]") {
    Camera camera;
    camera.nearZ = 0.1f;
    camera.farZ = 100.0f;
    const glm::mat4 proj = camera.projectionMatrix(16.0f / 9.0f);
    const glm::vec4 nearP = proj * glm::vec4(0.0f, 0.0f, -camera.nearZ, 1.0f);
    const glm::vec4 farP = proj * glm::vec4(0.0f, 0.0f, -camera.farZ, 1.0f);
    REQUIRE(nearP.z / nearP.w == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(farP.z / farP.w == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("look clamps pitch short of the poles", "[render]") {
    Camera camera;
    camera.look(0.0f, 10.0f); // way past +90°
    REQUIRE(camera.pitch < glm::half_pi<float>());
    camera.look(0.0f, -20.0f);
    REQUIRE(camera.pitch > -glm::half_pi<float>());
}

TEST_CASE("move is camera-relative on the horizontal plane", "[render]") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.yaw = glm::half_pi<float>(); // facing +X
    camera.pitch = 0.0f;
    camera.move({0.0f, 0.0f, 2.0f}); // forward
    REQUIRE(near3(camera.position, {2.0f, 0.0f, 0.0f}));
    camera.move({1.0f, 0.0f, 0.0f}); // right of +X-facing = -Z... verify via right()
    REQUIRE(near3(camera.position, glm::vec3{2.0f, 0.0f, 0.0f} + camera.right()));
}

TEST_CASE("cube mesh has 24 vertices, 36 CCW indices, unit bounds", "[render]") {
    const MeshData cube = makeCube();
    REQUIRE(cube.vertices.size() == 24);
    REQUIRE(cube.indices.size() == 36);
    glm::vec3 lo{1e9f}, hi{-1e9f};
    for (const Vertex& v : cube.vertices) {
        lo = glm::min(lo, {v.px, v.py, v.pz});
        hi = glm::max(hi, {v.px, v.py, v.pz});
    }
    REQUIRE(near3(lo, {-0.5f, -0.5f, -0.5f}));
    REQUIRE(near3(hi, {0.5f, 0.5f, 0.5f}));
    // Every triangle's geometric normal must agree with its vertices' stored normal --
    // this pins both winding (CCW from outside) and per-face normals in one property.
    for (size_t i = 0; i < cube.indices.size(); i += 3) {
        const Vertex& a = cube.vertices[cube.indices[i]];
        const Vertex& b = cube.vertices[cube.indices[i + 1]];
        const Vertex& c = cube.vertices[cube.indices[i + 2]];
        const glm::vec3 geometric =
            glm::normalize(glm::cross(glm::vec3{b.px - a.px, b.py - a.py, b.pz - a.pz},
                                      glm::vec3{c.px - a.px, c.py - a.py, c.pz - a.pz}));
        REQUIRE(glm::dot(geometric, {a.nx, a.ny, a.nz}) > 0.99f);
    }
}

TEST_CASE("plane mesh spans its half extent with +Y normals", "[render]") {
    const MeshData plane = makePlane(5.0f);
    REQUIRE(plane.vertices.size() == 4);
    REQUIRE(plane.indices.size() == 6);
    for (const Vertex& v : plane.vertices) {
        REQUIRE(v.py == 0.0f);
        REQUIRE(near3({v.nx, v.ny, v.nz}, {0.0f, 1.0f, 0.0f}));
        REQUIRE(std::abs(v.px) == Catch::Approx(5.0f));
        REQUIRE(std::abs(v.pz) == Catch::Approx(5.0f));
    }
}
