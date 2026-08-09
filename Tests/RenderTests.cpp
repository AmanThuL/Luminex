#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>

#include "Engine/GeometryGenerator.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <algorithm>
#include <cmath>

using namespace lmx::render;

namespace {
constexpr float kEps = 1e-5f;

//======================================================================================================================
bool near3(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEps));
}

//======================================================================================================================
glm::vec3 riggedLightDir() {
    return glm::normalize(glm::vec3{0.577f, -0.577f, 0.577f});
}

// The clip-space extremes of a bounding sphere's surface, sampled rather than re-derived. The fit
// under test *is* the sphere's silhouette, so the extremes are what pin it -- and sampling keeps
// the assertions independent of how the implementation picked its light-space basis, which no
// caller can see.
struct ClipExtent {
    float maxAbsX = 0.0f, maxAbsY = 0.0f;
    float minZ = 1e9f, maxZ = -1e9f;
};

//======================================================================================================================
ClipExtent sphereClipExtent(const glm::mat4& viewProj, const glm::vec4& sphere) {
    constexpr int kRings = 32;
    constexpr int kSegments = 64;
    ClipExtent extent;
    for (int i = 0; i <= kRings; ++i) {
        const float theta = glm::pi<float>() * static_cast<float>(i) / kRings;
        for (int j = 0; j < kSegments; ++j) {
            const float phi = glm::two_pi<float>() * static_cast<float>(j) / kSegments;
            const glm::vec3 point =
                glm::vec3(sphere) + sphere.w * glm::vec3{std::sin(theta) * std::cos(phi),
                                                         std::cos(theta),
                                                         std::sin(theta) * std::sin(phi)};
            const glm::vec4 clip = viewProj * glm::vec4(point, 1.0f);
            extent.maxAbsX = std::max(extent.maxAbsX, std::abs(clip.x));
            extent.maxAbsY = std::max(extent.maxAbsY, std::abs(clip.y));
            extent.minZ = std::min(extent.minZ, clip.z);
            extent.maxZ = std::max(extent.maxZ, clip.z);
        }
    }
    return extent;
}
} // namespace

//======================================================================================================================
TEST_CASE("default camera looks down -Z", "[render]") {
    Camera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    REQUIRE(near3(camera.forward(), {0.0f, 0.0f, -1.0f}));
    REQUIRE(near3(camera.right(), {1.0f, 0.0f, 0.0f}));
}

//======================================================================================================================
TEST_CASE("view matrix moves the world opposite the camera", "[render]") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    // A point 1 unit in front of the camera lands 1 unit down the view -Z axis.
    const glm::vec4 p = camera.viewMatrix() * glm::vec4(0.0f, 0.0f, 4.0f, 1.0f);
    REQUIRE(near3(glm::vec3(p), {0.0f, 0.0f, -1.0f}));
}

//======================================================================================================================
TEST_CASE("projection maps near to 1 and the horizon to 0 -- reversed infinite far", "[render]") {
    Camera camera;
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
    Camera camera;
    camera.look(0.0f, 10.0f); // way past +90°
    // The clamp constant mirrors Camera::look's ±(π/2 − 0.01) contract.
    REQUIRE(camera.pitch < glm::half_pi<float>());
    camera.look(0.0f, -20.0f);
    REQUIRE(camera.pitch > -glm::half_pi<float>());
}

//======================================================================================================================
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

//======================================================================================================================
TEST_CASE("fitShadowOrtho places the light 2r back along its own direction", "[render]") {
    const glm::vec4 sphere{0.0f, 0.0f, 0.0f, 42.43f};
    const ShadowMatrices matrices = fitShadowOrtho(sphere, riggedLightDir());

    // An orthographic eye has no projected point of its own, but it does have a clip-space image,
    // and that image is what pins where the fit put it. With the eye 2r out and the frustum
    // running near = r to far = 3r, the reversed ortho maps z_view linearly with slope
    // 1/(far - near) = 1/(2r) and offset far/(far - near) = 1.5 -- so the near plane lands on 1,
    // the far plane on 0, and the eye's own plane (z_view = 0) one half-depth past the near plane
    // at 1.5. The sphere's centre (z_view = -2r) still lands on exactly 0.5: reversing the
    // convention swaps the ends and leaves the midpoint where it was.
    const glm::vec4 eye = matrices.viewProj * glm::vec4(-2.0f * sphere.w * riggedLightDir(), 1.0f);
    REQUIRE(eye.w == Catch::Approx(1.0f)); // orthographic: no perspective divide anywhere
    REQUIRE(eye.x == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(eye.y == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(eye.z == Catch::Approx(1.5f).margin(1e-4));

    // And the light looks *at* the centre: it lands dead on the view axis, halfway through depth.
    const glm::vec4 center = matrices.viewProj * glm::vec4(glm::vec3(sphere), 1.0f);
    REQUIRE(center.x == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(center.y == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(center.z == Catch::Approx(0.5f).margin(1e-4));
}

//======================================================================================================================
TEST_CASE("fitShadowOrtho fits the bounding sphere exactly, off-origin too", "[render]") {
    // Off-origin on every axis: a fit that quietly assumed a centred sphere passes at the origin
    // and fails here, which is the only reason this case does not reuse the sphere above.
    const glm::vec4 sphere{3.0f, -1.0f, 2.0f, 7.0f};
    const ShadowMatrices matrices = fitShadowOrtho(sphere, riggedLightDir());
    const ClipExtent extent = sphereClipExtent(matrices.viewProj, sphere);

    // Exactly 1, not "at most 1": the ortho extents are ±r in light space, so the silhouette
    // touches all four sides. A loose fit wastes shadow-map texels; a tight-but-clipping one
    // loses geometry. Approx bounds it from both directions in one assertion.
    REQUIRE(extent.maxAbsX == Catch::Approx(1.0f).margin(1e-3));
    REQUIRE(extent.maxAbsY == Catch::Approx(1.0f).margin(1e-3));
    // Metal depth range, [0,1] rather than GL's [-1,1] -- the same convention Camera uses and the
    // range a D32Float shadow map stores, which is why CalcShadowFactor needs no z remap.
    REQUIRE(extent.minZ == Catch::Approx(0.0f).margin(1e-3));
    REQUIRE(extent.maxZ == Catch::Approx(1.0f).margin(1e-3));

    // Which end of that range is which is the whole reversed-Z convention, and the extents above
    // cannot see it -- they are the same two numbers either way round. Named points can: the
    // surface point nearest the light sits one radius in front of the fitted near plane and must
    // read 1, the one behind it must read 0. A fit that forgot to reverse passes every assertion
    // above and fails both of these.
    const glm::vec3 nearestToLight = glm::vec3(sphere) - sphere.w * riggedLightDir();
    const glm::vec3 farthestFromLight = glm::vec3(sphere) + sphere.w * riggedLightDir();
    REQUIRE((matrices.viewProj * glm::vec4(nearestToLight, 1.0f)).z ==
            Catch::Approx(1.0f).margin(1e-3));
    REQUIRE((matrices.viewProj * glm::vec4(farthestFromLight, 1.0f)).z ==
            Catch::Approx(0.0f).margin(1e-3));
}

//======================================================================================================================
TEST_CASE("fitShadowOrtho survives a light pointing straight down", "[render]") {
    // A direction parallel to world up makes lookAt's cross product degenerate into a zero-length
    // axis. The fit has to pick another up rather than emit NaNs -- a NaN matrix blanks the whole
    // shadow map.
    const glm::vec4 sphere{0.0f, 5.0f, 0.0f, 10.0f};
    const ShadowMatrices matrices = fitShadowOrtho(sphere, {0.0f, -1.0f, 0.0f});
    const ClipExtent extent = sphereClipExtent(matrices.viewProj, sphere);
    REQUIRE(extent.maxAbsX == Catch::Approx(1.0f).margin(1e-3));
    REQUIRE(extent.maxAbsY == Catch::Approx(1.0f).margin(1e-3));
    REQUIRE(extent.minZ == Catch::Approx(0.0f).margin(1e-3));
    REQUIRE(extent.maxZ == Catch::Approx(1.0f).margin(1e-3));
}

//======================================================================================================================
TEST_CASE("fitShadowOrtho bakes the NDC-to-texcoord map into shadowTransform", "[render]") {
    const glm::vec4 sphere{0.0f, 0.0f, 0.0f, 10.0f};
    const ShadowMatrices matrices =
        fitShadowOrtho(sphere, glm::normalize(glm::vec3{0.0f, -1.0f, 0.2f}));

    // Shadow texcoords map x [-1,1] -> u [0,1] and y [-1,1] -> v [1,0],
    // because texture v runs down; z untouched, because Metal clip depth is already the [0,1] a
    // D32Float map stores. Checked on interior points as well as the origin so a wrong scale and
    // a wrong offset cannot cancel each other out.
    for (const glm::vec3& point :
         {glm::vec3{0.0f}, glm::vec3{4.0f, -3.0f, 6.0f}, glm::vec3{-7.0f, 2.0f, -1.0f}}) {
        const glm::vec4 clip = matrices.viewProj * glm::vec4(point, 1.0f);
        const glm::vec4 shadow = matrices.shadowTransform * glm::vec4(point, 1.0f);
        REQUIRE(shadow.x == Catch::Approx(0.5f * clip.x + 0.5f).margin(1e-5));
        REQUIRE(shadow.y == Catch::Approx(-0.5f * clip.y + 0.5f).margin(1e-5));
        REQUIRE(shadow.z == Catch::Approx(clip.z).margin(1e-5));
        REQUIRE(shadow.w == Catch::Approx(clip.w).margin(1e-5));
    }

    // The corner the convention is named for, stated as a fact about texels rather than as
    // algebra: clip-space left/top is the shadow map's (0,0) texel.
    const glm::vec4 leftTop = glm::inverse(matrices.viewProj) * glm::vec4(-1.0f, 1.0f, 0.5f, 1.0f);
    const glm::vec4 uv = matrices.shadowTransform * leftTop;
    REQUIRE(uv.x == Catch::Approx(0.0f).margin(1e-5));
    REQUIRE(uv.y == Catch::Approx(0.0f).margin(1e-5));
}

//======================================================================================================================
TEST_CASE("fromGeo copies every Engine vertex field verbatim", "[render]") {
    // Distinct values in every one of the twelve floats: a swapped or dropped field shows up as a
    // specific number in the wrong place rather than as a plausible-looking mesh.
    lmx::engine::GeoData geo;
    geo.vertices = {
        {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, -1.0f, 10.0f, 11.0f},
        {-1.5f, -2.5f, -3.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.75f},
    };
    geo.indices = {0, 1, 0};

    const MeshData data = fromGeo(geo);
    REQUIRE(data.indices == geo.indices);
    REQUIRE(data.vertices.size() == geo.vertices.size());
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        const Vertex& out = data.vertices[i];
        const lmx::engine::VertexPNTU& in = geo.vertices[i];
        REQUIRE(near3({out.px, out.py, out.pz}, {in.px, in.py, in.pz}));
        REQUIRE(near3({out.nx, out.ny, out.nz}, {in.nx, in.ny, in.nz}));
        REQUIRE(near3({out.tx, out.ty, out.tz}, {in.tx, in.ty, in.tz}));
        REQUIRE(out.tw == in.tw);
        REQUIRE(out.u == in.u);
        REQUIRE(out.v == in.v);
    }
}

//======================================================================================================================
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
    // The PNTU tail. These two procedural meshes carry no texture and never get a normal map --
    // the scene shader's normal-map branch is off for them (ObjectUniforms flags bit0 clear) --
    // so the tangent is a fixed placeholder rather than a per-face frame, and the uv is 0.
    // Pinned so that a future generator change has to say so out loud.
    for (const Vertex& v : cube.vertices) {
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}));
        REQUIRE(v.tw == 1.0f);
        REQUIRE(v.u == 0.0f);
        REQUIRE(v.v == 0.0f);
    }
}

//======================================================================================================================
TEST_CASE("plane mesh spans its half extent with +Y normals", "[render]") {
    const MeshData plane = makePlane(5.0f);
    REQUIRE(plane.vertices.size() == 4);
    REQUIRE(plane.indices.size() == 6);
    for (const Vertex& v : plane.vertices) {
        REQUIRE(v.py == 0.0f);
        REQUIRE(near3({v.nx, v.ny, v.nz}, {0.0f, 1.0f, 0.0f}));
        REQUIRE(std::abs(v.px) == Catch::Approx(5.0f));
        REQUIRE(std::abs(v.pz) == Catch::Approx(5.0f));
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}));
        REQUIRE(v.tw == 1.0f);
        REQUIRE(v.u == 0.0f);
        REQUIRE(v.v == 0.0f);
    }
}
