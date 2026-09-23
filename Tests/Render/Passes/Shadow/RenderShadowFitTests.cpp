#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Render/Renderer/Renderer.h"

#include <algorithm>
#include <cmath>

using namespace lmx::render;

namespace {
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
