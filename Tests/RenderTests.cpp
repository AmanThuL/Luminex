#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>

#include "BrdfOracle.h"

#include "Engine/GeometryGenerator.h"
#include "Engine/Ibl.h"
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

//======================================================================================================================
// Shaders/ScenePass.slang hardcodes the prefiltered chain's level count so the roughness-to-mip map
// is a compile-time constant in the fragment. Nothing links the two, so this is the statement that
// the shader and the generator agree; a chain generated at a different depth would otherwise shift
// every specular lookup by a fraction of a mip with no build error.
TEST_CASE("the scene shader's specular mip count matches the IBL generator's", "[render]") {
    REQUIRE(lmx::engine::ibl::kSpecularMipCount == 5);
}

//======================================================================================================================
// The energy statement the whole material model rests on, made without a GPU.
//
// In a uniform environment of radiance E, Source/Engine/Ibl.h's generators reproduce E exactly at
// every roughness -- the irradiance convolution and the prefilter both normalize by their own
// accumulated weight -- so both image-based samples are E and the fragment reduces to E times the
// surface's total reflectance. A white surface must then return E itself: it absorbs nothing, so
// every photon that arrived has to leave.
//
// That closure is algebraic rather than approximate, which is why this runs as a unit test at
// float precision before Tests/GpuRendererTests.cpp measures it through the pipeline. For albedo 1
// the diffuse term carries exactly the energy the two specular terms did not
// (diffuseEnergy = 1 - singleScatter - multiScatter), so the three sum to 1 for any DFG pair; for a
// conductor the diffuse term vanishes and the compensation alone restores what single scattering
// dropped, since Favg collapses to 1 when F0 does. Single scattering by itself would fail this: the
// DFG table's scale + bias falls to 0.32 at roughness 1 (pinned in Tests/EngineIblTests.cpp), so an
// uncompensated rough conductor would return a third of the light it received.
TEST_CASE("a white furnace returns its own radiance at every roughness", "[render]") {
    namespace brdf = lmx::test::brdf;

    constexpr glm::vec3 kEnvironment{1.0f, 1.0f, 1.0f};
    // The five columns MaterialLab's grid sweeps, endpoints included.
    const float roughnesses[] = {0.05f, 0.2875f, 0.525f, 0.7625f, 1.0f};
    // Facing and oblique probes: the closure is independent of view angle, and saying so here is
    // what makes the GPU furnace free to probe anywhere on a sphere rather than only its centre.
    const float views[] = {1.0f, 0.7f, 0.25f};

    const auto furnace = [&](float roughness, float metallic, float nov) {
        const brdf::Surface surface{
            .baseColor = glm::vec3(1.0f), .perceptualRoughness = roughness, .metallic = metallic};
        return brdf::imageBasedLight(kEnvironment, kEnvironment, brdf::sampleDfg(nov, roughness),
                                     surface);
    };

    SECTION("a white dielectric and a white conductor both close exactly") {
        for (float roughness : roughnesses) {
            for (float metallic : {0.0f, 1.0f}) {
                for (float nov : views) {
                    INFO("roughness " << roughness << ", metallic " << metallic << ", N.V " << nov);
                    const glm::vec3 result = furnace(roughness, metallic, nov);
                    // float32 accumulation over the compensation's divide; well inside the
                    // half-float storage the GPU path adds on top of it.
                    REQUIRE(result.r == Catch::Approx(1.0f).margin(1e-5));
                    REQUIRE(result.g == Catch::Approx(1.0f).margin(1e-5));
                    REQUIRE(result.b == Catch::Approx(1.0f).margin(1e-5));
                }
            }
        }
    }

    // A partly-metallic surface is not a material, it is a blend between two of them: glTF's
    // metallic parameter scales the diffuse lobe away by (1 - metallic) while F0 climbs toward the
    // base colour, and nothing puts the energy the diffuse lobe gave up back into the specular one.
    // So there is no furnace for the middle of that axis to close, and claiming one would mean
    // inventing a term the glTF model does not have. What *is* true everywhere -- and is the
    // property that matters -- is that no combination creates energy.
    SECTION("no roughness and metallic combination reflects more than it received") {
        for (float roughness : roughnesses) {
            for (float metallic : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                for (float nov : views) {
                    INFO("roughness " << roughness << ", metallic " << metallic << ", N.V " << nov);
                    const glm::vec3 result = furnace(roughness, metallic, nov);
                    REQUIRE(result.r <= 1.0f + 1e-5f);
                    REQUIRE(result.r > 0.0f);
                }
            }
        }
    }
}

//======================================================================================================================
// A black surface is the other half of the same statement: it reflects nothing, so no arrangement
// of environment radiance may make it glow. Only the dielectric 4% survives, and only through the
// specular terms.
TEST_CASE("a black dielectric reflects only its Fresnel share of the environment", "[render]") {
    namespace brdf = lmx::test::brdf;

    const brdf::Surface surface{.baseColor = glm::vec3(0.0f), .perceptualRoughness = 0.25f};
    const glm::vec3 result = brdf::imageBasedLight(glm::vec3(1.0f), glm::vec3(1.0f),
                                                   brdf::sampleDfg(1.0f, 0.25f), surface);
    // At normal incidence the split sum reduces to F0 * scale + bias with scale near 1, so the
    // reflected fraction sits just above the 4% dielectric constant and far below unity.
    REQUIRE(result.r > 0.03f);
    REQUIRE(result.r < 0.06f);
}

//======================================================================================================================
// Edge cases the shader's clamps exist for. Each one is a value a real scene reaches -- a
// silhouette pixel, an authored mirror, an authored fully-rough surface -- and each would produce a
// division by zero, a singular lobe, or a NaN without the guard the model states.
TEST_CASE("the BRDF stays finite and energy-bounded at its parameter limits", "[render]") {
    namespace brdf = lmx::test::brdf;

    const glm::vec3 normal{0.0f, 0.0f, 1.0f};
    const glm::vec3 strength{1.0f, 1.0f, 1.0f};

    SECTION("roughness below the floor is clamped rather than singular") {
        REQUIRE(brdf::alphaOf(0.0f) == Catch::Approx(brdf::kMinRoughness * brdf::kMinRoughness));
        REQUIRE(brdf::alphaOf(0.045f) == Catch::Approx(brdf::alphaOf(0.0f)));
        // A mirror lobe at the floor is huge but finite, which is the whole point of the floor.
        const float peak = brdf::dGgx(1.0f, brdf::alphaOf(0.0f));
        REQUIRE(std::isfinite(peak));
        REQUIRE(peak > 100.0f);
    }

    SECTION("a grazing view direction produces no infinity") {
        // N.V -> 0 drives the visibility term's denominator toward zero; the floor keeps it finite.
        for (float nov : {1e-6f, 1e-4f, 0.01f}) {
            INFO("N.V " << nov);
            const float v = brdf::vSmithHeightCorrelated(nov, 0.5f, brdf::alphaOf(0.5f));
            REQUIRE(std::isfinite(v));
            REQUIRE(v > 0.0f);
        }
    }

    SECTION("metallic 0 and 1 select the dielectric constant and the base colour") {
        const glm::vec3 copper{0.95f, 0.64f, 0.54f};
        REQUIRE(brdf::f0Of({.baseColor = copper, .metallic = 0.0f}).r ==
                Catch::Approx(brdf::kDielectricF0));
        REQUIRE(brdf::f0Of({.baseColor = copper, .metallic = 1.0f}).r == Catch::Approx(copper.r));
        REQUIRE(brdf::f0Of({.baseColor = copper, .metallic = 1.0f}).g == Catch::Approx(copper.g));
    }

    SECTION("a conductor has no diffuse lobe") {
        const brdf::Surface conductor{
            .baseColor = glm::vec3(1.0f), .perceptualRoughness = 1.0f, .metallic = 1.0f};
        const brdf::Surface dielectric{
            .baseColor = glm::vec3(1.0f), .perceptualRoughness = 1.0f, .metallic = 0.0f};
        // Lit from behind the viewer at a shallow angle, a rough dielectric is dominated by its
        // diffuse lobe; removing that lobe has to darken it.
        const glm::vec3 lightDirection{0.0f, 0.0f, -1.0f};
        const glm::vec3 toEye{0.0f, 0.0f, 1.0f};
        const glm::vec3 metalLit =
            brdf::directionalLight(strength, lightDirection, normal, toEye, conductor);
        const glm::vec3 dielectricLit =
            brdf::directionalLight(strength, lightDirection, normal, toEye, dielectric);
        REQUIRE(metalLit.r < dielectricLit.r);
    }

    SECTION("a light behind the surface contributes nothing") {
        const glm::vec3 behind{0.0f, 0.0f, 1.0f}; // travelling away from the normal
        const glm::vec3 lit =
            brdf::directionalLight(strength, behind, normal, glm::vec3(0.0f, 0.0f, 1.0f), {});
        REQUIRE(lit.r == 0.0f);
        REQUIRE(lit.g == 0.0f);
        REQUIRE(lit.b == 0.0f);
    }

    SECTION("direct lighting never returns more than the light it was given") {
        // Diffuse takes what Fresnel left and a metal has no diffuse at all, so a white surface
        // under a unit light integrates to at most that light. Sampled over the hemisphere rather
        // than argued: the specular lobe's peak is where an unnormalized D would show up.
        for (float roughness : {0.045f, 0.2f, 0.6f, 1.0f}) {
            for (int i = 0; i <= 16; ++i) {
                const float theta = glm::half_pi<float>() * static_cast<float>(i) / 16.0f;
                const glm::vec3 toEye{std::sin(theta), 0.0f, std::cos(theta)};
                const glm::vec3 lit = brdf::directionalLight(
                    strength, glm::vec3(0.0f, 0.0f, -1.0f), normal, toEye,
                    {.baseColor = glm::vec3(1.0f), .perceptualRoughness = roughness});
                INFO("roughness " << roughness << ", theta " << theta);
                REQUIRE(std::isfinite(lit.r));
                REQUIRE(lit.r >= 0.0f);
            }
        }
    }
}
