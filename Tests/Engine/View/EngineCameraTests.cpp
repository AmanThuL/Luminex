#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Support/EngineTestSupport.h"

#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Geometry/Mesh.h"
#include "Engine/View/Camera.h"

using lmx::test::near3;

namespace {
constexpr float kEps = 1e-5f;
} // namespace

//======================================================================================================================
TEST_CASE("default camera looks down -Z", "[render]") {
    lmx::engine::Camera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    REQUIRE(near3(camera.forward(), {0.0f, 0.0f, -1.0f}, kEps));
    REQUIRE(near3(camera.right(), {1.0f, 0.0f, 0.0f}, kEps));
}

//======================================================================================================================
TEST_CASE("view matrix moves the world opposite the camera", "[render]") {
    lmx::engine::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    // A point 1 unit in front of the camera lands 1 unit down the view -Z axis.
    const glm::vec4 p = camera.viewMatrix() * glm::vec4(0.0f, 0.0f, 4.0f, 1.0f);
    REQUIRE(near3(glm::vec3(p), {0.0f, 0.0f, -1.0f}, kEps));
}

//======================================================================================================================
TEST_CASE("projection maps near to 1 and the horizon to 0 -- reversed infinite far", "[render]") {
    lmx::engine::Camera camera;
    camera.nearZ = 0.1f;
    camera.farZ = 100.0f;
    const glm::mat4 proj = camera.projectionMatrix(16.0f / 9.0f);

    // The reversed infinite-far projection emits clip.z = nearZ and clip.w = -z_view, so the
    // depth a fragment writes is nearZ / (-z_view): exactly 1 on the near plane, and a reciprocal
    // that only approaches 0 as the eye distance grows without bound. There is no depth a point
    // can reach 0 at, which is the whole point of an infinite far plane -- nothing is ever
    // clipped away for being too distant.
    const glm::vec4 nearP = proj * glm::vec4(0.0f, 0.0f, -camera.nearZ, 1.0f);
    REQUIRE(nearP.z / nearP.w == Catch::Approx(1.0f).margin(1e-6));

    // farZ is data the projection no longer reads. A point at farZ is an ordinary point, at
    // 0.1/100 = 0.001, and one a thousand times farther still sits above zero rather than being
    // clipped -- both of which a finite far plane would have gotten wrong.
    const glm::vec4 farP = proj * glm::vec4(0.0f, 0.0f, -camera.farZ, 1.0f);
    REQUIRE(farP.z / farP.w == Catch::Approx(0.001f).margin(1e-6));
    const glm::vec4 beyondP = proj * glm::vec4(0.0f, 0.0f, -100000.0f, 1.0f);
    REQUIRE(beyondP.w > 0.0f);
    REQUIRE(beyondP.z / beyondP.w == Catch::Approx(1e-6f).margin(1e-9));

    // Depth decreases monotonically with distance, which is what makes Greater the nearer-wins
    // comparison the pipelines are built with.
    REQUIRE(nearP.z / nearP.w > farP.z / farP.w);
    REQUIRE(farP.z / farP.w > beyondP.z / beyondP.w);
}

//======================================================================================================================
TEST_CASE("look clamps pitch short of the poles", "[render]") {
    lmx::engine::Camera camera;
    camera.look(0.0f, 10.0f); // way past +90°
    // The clamp constant mirrors Camera::look's ±(π/2 − 0.01) contract.
    REQUIRE(camera.pitch < glm::half_pi<float>());
    camera.look(0.0f, -20.0f);
    REQUIRE(camera.pitch > -glm::half_pi<float>());
}

//======================================================================================================================
TEST_CASE("move is camera-relative on the horizontal plane", "[render]") {
    lmx::engine::Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.yaw = glm::half_pi<float>(); // facing +X
    camera.pitch = 0.0f;
    camera.move({0.0f, 0.0f, 2.0f}); // forward
    REQUIRE(near3(camera.position, {2.0f, 0.0f, 0.0f}, kEps));
    camera.move({1.0f, 0.0f, 0.0f}); // right of +X-facing = -Z... verify via right()
    REQUIRE(near3(camera.position, glm::vec3{2.0f, 0.0f, 0.0f} + camera.right(), kEps));
}

//======================================================================================================================
TEST_CASE("fromGeo copies every Engine vertex field verbatim", "[render]") {
    // Distinct values in every one of the twelve floats: a swapped or dropped field shows up as a
    // specific number in the wrong place rather than as a plausible-looking mesh.
    lmx::asset::GeoData geo;
    geo.vertices = {
        {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, -1.0f, 10.0f, 11.0f},
        {-1.5f, -2.5f, -3.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.75f},
    };
    geo.indices = {0, 1, 0};

    const lmx::engine::MeshData data = lmx::engine::fromGeo(geo);
    REQUIRE(data.indices == geo.indices);
    REQUIRE(data.vertices.size() == geo.vertices.size());
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        const lmx::engine::Vertex& out = data.vertices[i];
        const lmx::asset::VertexPNTU& in = geo.vertices[i];
        REQUIRE(near3({out.px, out.py, out.pz}, {in.px, in.py, in.pz}, kEps));
        REQUIRE(near3({out.nx, out.ny, out.nz}, {in.nx, in.ny, in.nz}, kEps));
        REQUIRE(near3({out.tx, out.ty, out.tz}, {in.tx, in.ty, in.tz}, kEps));
        REQUIRE(out.tw == in.tw);
        REQUIRE(out.u == in.u);
        REQUIRE(out.v == in.v);
    }
}
