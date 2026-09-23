//----------------------------------------------------------------------------------------------------------------------
/// @file RenderLocalLightTests.cpp
/// @brief Tests the CPU point/spot light model, its GPU row and its shading-function mirror.
//----------------------------------------------------------------------------------------------------------------------

#include "Support/BrdfOracle.h"

#include "Engine/Lights/LocalLight.h"
#include "Engine/Lights/LocalLightMath.h"
#include "Engine/Scene/SceneTables.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using Catch::Approx;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

//======================================================================================================================
float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

} // namespace

//======================================================================================================================
// This proves computePunctualLight's *multiplier* collapses to the directional mirror's implicit
// attenuation=1/cone=1 at 1 m and normal incidence -- i.e. that the point-light-specific plumbing
// (row decode, distance/cone terms) is transparent at unit distance. Both sides call the same
// D_GGX/V_SmithHeightCorrelated/F_Schlick formulas (LocalLightMath.cpp's private core here,
// Tests/BrdfOracle.h's copy there), so this is not an independent check of BRDF correctness itself
// -- that is Tests/BrdfOracle.h's and the shader oracles' job.
TEST_CASE("a point light of intensity 2 at 1 m and normal incidence matches the directional mirror",
          "[render][light]") {
    const lmx::test::brdf::Surface surface{glm::vec3(0.5f, 0.6f, 0.7f), 0.5f, 0.2f, 1.0f,
                                           glm::vec3(0.0f)};
    const float alpha = lmx::test::brdf::alphaOf(surface.perceptualRoughness);
    const glm::vec3 f0 = lmx::test::brdf::f0Of(surface);

    const glm::vec3 position(0.0f);
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec3 toEye(0.0f, 0.0f, 1.0f);

    // A light directly above the surface at 1 m, with a range large enough that the windowed term
    // rounds to exactly 1.0f in float32, so the point light's attenuation is exactly the
    // directional mirror's implicit 1.0.
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Point;
    light.position = glm::vec3(0.0f, 0.0f, 1.0f);
    light.colour = glm::vec3(1.0f);
    light.intensity = 2.0f;
    light.range = 1000.0f;

    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());
    CHECK(row->spotScale == 0.0f);
    CHECK(row->spotOffset == 1.0f);

    const glm::vec3 actual = lmx::engine::computePunctualLight(
        *row, position, normal, toEye, surface.baseColor, f0, surface.metallic, alpha);
    const glm::vec3 expected = lmx::test::brdf::directionalLight(
        glm::vec3(2.0f), glm::vec3(0.0f, 0.0f, -1.0f), normal, toEye, surface);

    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.z == expected.z);
}

//======================================================================================================================
TEST_CASE("attenuation reaches exactly zero at range and beyond, and is positive just inside it",
          "[render][light]") {
    constexpr float range = 10.0f;
    CHECK(lmx::engine::punctualAttenuation(range, range) == 0.0f);
    CHECK(lmx::engine::punctualAttenuation(
              std::nextafter(range, std::numeric_limits<float>::infinity()), range) == 0.0f);
    CHECK(lmx::engine::punctualAttenuation(std::nextafter(range, 0.0f), range) > 0.0f);
}

//======================================================================================================================
// Pins the floor at exactly 0.01 m, not merely "some floor at or above 0.01 m": every hand-computed
// expectation below uses a literal 0.01 m denominator, and 0.02 m -- outside the floor -- is shown
// to use its own true distance instead, which a floor larger than 0.01 m would falsely also clamp.
TEST_CASE("the distance floor clamps attenuation to exactly 0.01 m, not any larger floor",
          "[render][light]") {
    // A range far larger than every sampled distance makes the windowed falloff term round to
    // exactly 1.0f, isolating the floor's effect on the inverse-square denominator.
    constexpr float range = 1000.0f;
    const auto windowAt = [](float distance) {
        const float ratio = distance / range;
        const float ratio4 = ratio * ratio * ratio * ratio;
        const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
        return window * window;
    };

    const float floored = windowAt(0.01f) / (0.01f * 0.01f);
    CHECK(lmx::engine::punctualAttenuation(0.001f, range) == Approx(floored));
    CHECK(lmx::engine::punctualAttenuation(0.005f, range) == Approx(floored));
    CHECK(lmx::engine::punctualAttenuation(0.01f, range) == Approx(floored));

    // 0.02 m is above the floor, so it is not clamped to the same value: this is what discriminates
    // a 0.01 m floor from a larger one that would also catch 0.02 m.
    const float unfloored = windowAt(0.02f) / (0.02f * 0.02f);
    CHECK(lmx::engine::punctualAttenuation(0.02f, range) == Approx(unfloored));
    CHECK(lmx::engine::punctualAttenuation(0.02f, range) <
          lmx::engine::punctualAttenuation(0.01f, range));
}

//======================================================================================================================
TEST_CASE("the cone term is exactly zero at outerCone and reaches one at innerCone",
          "[render][light]") {
    const float innerCone = radians(10.0f);
    const float outerCone = radians(30.0f);

    lmx::engine::LocalLight spot{};
    spot.type = lmx::engine::LocalLightType::Spot;
    spot.colour = glm::vec3(1.0f);
    spot.intensity = 1.0f;
    spot.range = 10.0f;
    spot.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    spot.innerCone = innerCone;
    spot.outerCone = outerCone;
    const auto row = lmx::engine::makeLightRow(spot);
    REQUIRE(row.has_value());

    // cosOuter * spotScale + spotOffset is the same multiply makeLightRow used to build spotOffset,
    // negated and re-added, so it is exact zero regardless of the division's rounding.
    const float cosInner = std::cos(innerCone);
    const float cosOuter = std::cos(outerCone);
    CHECK(lmx::engine::spotTerm(cosOuter, row->spotScale, row->spotOffset) == 0.0f);
    CHECK(lmx::engine::spotTerm(cosInner, row->spotScale, row->spotOffset) == Approx(1.0f));
}

//======================================================================================================================
TEST_CASE(
    "a spot at 30 degrees uses the tight cone sphere and at 60 degrees falls back to the range "
    "sphere",
    "[render][light]") {
    constexpr float kInflation = 1.0f + 1.0f / 1024.0f;

    lmx::engine::LocalLight tight{};
    tight.type = lmx::engine::LocalLightType::Spot;
    tight.position = glm::vec3(1.0f, 2.0f, 3.0f);
    tight.colour = glm::vec3(1.0f);
    tight.intensity = 1.0f;
    tight.range = 20.0f;
    tight.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    tight.innerCone = radians(10.0f);
    tight.outerCone = radians(30.0f);

    const auto tightRow = lmx::engine::makeLightRow(tight);
    REQUIRE(tightRow.has_value());
    const float cosOuterTight = std::cos(tight.outerCone);
    const float expectedRadius = tight.range / (2.0f * cosOuterTight);
    const glm::vec3 expectedCentre = tight.position + tight.direction * expectedRadius;
    CHECK(tightRow->boundCentre.x == Approx(expectedCentre.x));
    CHECK(tightRow->boundCentre.y == Approx(expectedCentre.y));
    CHECK(tightRow->boundCentre.z == Approx(expectedCentre.z));
    CHECK(tightRow->boundRadius == Approx(expectedRadius * kInflation));

    lmx::engine::LocalLight wide = tight;
    wide.innerCone = radians(40.0f);
    wide.outerCone = radians(60.0f);

    const auto wideRow = lmx::engine::makeLightRow(wide);
    REQUIRE(wideRow.has_value());
    CHECK(wideRow->boundCentre.x == Approx(wide.position.x));
    CHECK(wideRow->boundCentre.y == Approx(wide.position.y));
    CHECK(wideRow->boundCentre.z == Approx(wide.position.z));
    CHECK(wideRow->boundRadius == Approx(wide.range * kInflation));
}

//======================================================================================================================
TEST_CASE("the spot bound sphere uses the tight formula at exactly 45 degrees and the range sphere "
          "just above it",
          "[render][light]") {
    constexpr float kInflation = 1.0f + 1.0f / 1024.0f;

    lmx::engine::LocalLight boundary{};
    boundary.type = lmx::engine::LocalLightType::Spot;
    boundary.position = glm::vec3(1.0f, -2.0f, 3.0f);
    boundary.colour = glm::vec3(1.0f);
    boundary.intensity = 1.0f;
    boundary.range = 12.0f;
    boundary.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    boundary.innerCone = radians(20.0f);
    boundary.outerCone = radians(45.0f);

    const auto atBoundary = lmx::engine::makeLightRow(boundary);
    REQUIRE(atBoundary.has_value());
    const float cosOuterAt = std::cos(boundary.outerCone);
    const float expectedRadiusAt = boundary.range / (2.0f * cosOuterAt);
    const glm::vec3 expectedCentreAt = boundary.position + boundary.direction * expectedRadiusAt;
    CHECK(atBoundary->boundCentre.x == Approx(expectedCentreAt.x));
    CHECK(atBoundary->boundCentre.y == Approx(expectedCentreAt.y));
    CHECK(atBoundary->boundCentre.z == Approx(expectedCentreAt.z));
    CHECK(atBoundary->boundRadius == Approx(expectedRadiusAt * kInflation));

    lmx::engine::LocalLight justAbove = boundary;
    justAbove.outerCone = radians(45.1f);
    const auto aboveBoundary = lmx::engine::makeLightRow(justAbove);
    REQUIRE(aboveBoundary.has_value());
    CHECK(aboveBoundary->boundCentre.x == Approx(justAbove.position.x));
    CHECK(aboveBoundary->boundCentre.y == Approx(justAbove.position.y));
    CHECK(aboveBoundary->boundCentre.z == Approx(justAbove.position.z));
    CHECK(aboveBoundary->boundRadius == Approx(justAbove.range * kInflation));
}

//======================================================================================================================
TEST_CASE("the bound radius is inflated by exactly 1 + 2^-10 over the raw range sphere",
          "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Point;
    light.position = glm::vec3(0.0f);
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;
    light.range = 5.0f;

    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());
    CHECK(row->boundRadius == Approx(light.range * (1.0f + 1.0f / 1024.0f)));
}

//======================================================================================================================
TEST_CASE("makeLightRow rejects an invalid range", "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Point;
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;

    light.range = 0.0f;
    CHECK_FALSE(lmx::engine::makeLightRow(light).has_value());

    light.range = -1.0f;
    CHECK_FALSE(lmx::engine::makeLightRow(light).has_value());

    light.range = std::numeric_limits<float>::infinity();
    CHECK_FALSE(lmx::engine::makeLightRow(light).has_value());

    light.range = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(lmx::engine::makeLightRow(light).has_value());
}

//======================================================================================================================
TEST_CASE("makeLightRow rejects an invalid spot cone or direction", "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Spot;
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;
    light.range = 10.0f;
    light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    light.innerCone = radians(10.0f);
    light.outerCone = radians(30.0f);

    // innerCone >= outerCone is rejected.
    lmx::engine::LocalLight innerEqualsOuter = light;
    innerEqualsOuter.innerCone = innerEqualsOuter.outerCone;
    CHECK_FALSE(lmx::engine::makeLightRow(innerEqualsOuter).has_value());

    lmx::engine::LocalLight innerGreaterThanOuter = light;
    innerGreaterThanOuter.innerCone = radians(40.0f);
    CHECK_FALSE(lmx::engine::makeLightRow(innerGreaterThanOuter).has_value());

    // outerCone > 89 degrees is rejected.
    lmx::engine::LocalLight outerTooWide = light;
    outerTooWide.outerCone = radians(89.5f);
    CHECK_FALSE(lmx::engine::makeLightRow(outerTooWide).has_value());

    // A zero direction is rejected.
    lmx::engine::LocalLight zeroDirection = light;
    zeroDirection.direction = glm::vec3(0.0f);
    CHECK_FALSE(lmx::engine::makeLightRow(zeroDirection).has_value());

    // The unmodified light is valid, confirming the fixture itself is not what rejected the cases
    // above.
    CHECK(lmx::engine::makeLightRow(light).has_value());
}

//======================================================================================================================
TEST_CASE("makeLightRow rejects a non-finite position, colour, intensity or direction",
          "[render][light]") {
    lmx::engine::LocalLight base{};
    base.type = lmx::engine::LocalLightType::Point;
    base.position = glm::vec3(0.0f);
    base.colour = glm::vec3(1.0f);
    base.intensity = 1.0f;
    base.range = 10.0f;
    REQUIRE(lmx::engine::makeLightRow(base).has_value());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    lmx::engine::LocalLight nanPosition = base;
    nanPosition.position.x = nan;
    CHECK_FALSE(lmx::engine::makeLightRow(nanPosition).has_value());

    lmx::engine::LocalLight infColour = base;
    infColour.colour.y = inf;
    CHECK_FALSE(lmx::engine::makeLightRow(infColour).has_value());

    lmx::engine::LocalLight nanIntensity = base;
    nanIntensity.intensity = nan;
    CHECK_FALSE(lmx::engine::makeLightRow(nanIntensity).has_value());

    lmx::engine::LocalLight infIntensity = base;
    infIntensity.intensity = inf;
    CHECK_FALSE(lmx::engine::makeLightRow(infIntensity).has_value());

    lmx::engine::LocalLight spot = base;
    spot.type = lmx::engine::LocalLightType::Spot;
    spot.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    spot.innerCone = radians(10.0f);
    spot.outerCone = radians(30.0f);
    REQUIRE(lmx::engine::makeLightRow(spot).has_value());

    // An infinite direction passes the existing nonzero-length gate (length is itself infinite)
    // but must still be rejected, or the stored row ends up with a NaN direction (inf / inf).
    lmx::engine::LocalLight infDirection = spot;
    infDirection.direction = glm::vec3(0.0f, 0.0f, inf);
    CHECK_FALSE(lmx::engine::makeLightRow(infDirection).has_value());

    lmx::engine::LocalLight nanDirection = spot;
    nanDirection.direction = glm::vec3(nan, 0.0f, -1.0f);
    CHECK_FALSE(lmx::engine::makeLightRow(nanDirection).has_value());
}

//======================================================================================================================
TEST_CASE("computePunctualLight returns exact zero at or beyond range, outside the cone, and when "
          "the surface faces away",
          "[render][light]") {
    const glm::vec3 baseColour(0.5f, 0.6f, 0.7f);
    const glm::vec3 f0(0.04f);
    constexpr float metallic = 0.0f;
    constexpr float alpha = 0.25f;

    // d >= range: a point light whose range does not reach the surface.
    {
        lmx::engine::LocalLight light{};
        light.type = lmx::engine::LocalLightType::Point;
        light.position = glm::vec3(0.0f, 0.0f, 10.0f);
        light.colour = glm::vec3(1.0f);
        light.intensity = 1.0f;
        light.range = 5.0f;
        const auto row = lmx::engine::makeLightRow(light);
        REQUIRE(row.has_value());

        const glm::vec3 result = lmx::engine::computePunctualLight(
            *row, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f),
            baseColour, f0, metallic, alpha);
        CHECK(result == glm::vec3(0.0f));
    }

    // Outside the cone: the surface sits directly behind a narrow spot, opposite where it aims.
    {
        lmx::engine::LocalLight light{};
        light.type = lmx::engine::LocalLightType::Spot;
        light.position = glm::vec3(0.0f);
        light.colour = glm::vec3(1.0f);
        light.intensity = 1.0f;
        light.range = 10.0f;
        light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
        light.innerCone = radians(10.0f);
        light.outerCone = radians(30.0f);
        const auto row = lmx::engine::makeLightRow(light);
        REQUIRE(row.has_value());

        const glm::vec3 result = lmx::engine::computePunctualLight(
            *row, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f),
            glm::vec3(0.0f, 0.0f, 1.0f), baseColour, f0, metallic, alpha);
        CHECK(result == glm::vec3(0.0f));
    }

    // N.L <= 0: a point light directly above a surface whose normal faces straight down.
    {
        lmx::engine::LocalLight light{};
        light.type = lmx::engine::LocalLightType::Point;
        light.position = glm::vec3(0.0f, 0.0f, 1.0f);
        light.colour = glm::vec3(1.0f);
        light.intensity = 1.0f;
        light.range = 10.0f;
        const auto row = lmx::engine::makeLightRow(light);
        REQUIRE(row.has_value());

        const glm::vec3 result = lmx::engine::computePunctualLight(
            *row, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f),
            baseColour, f0, metallic, alpha);
        CHECK(result == glm::vec3(0.0f));
    }
}

//======================================================================================================================
TEST_CASE("lightReaches admits inside range and cone, and rejects beyond range or outside the cone",
          "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Spot;
    light.position = glm::vec3(0.0f);
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;
    light.range = 10.0f;
    light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    light.innerCone = radians(10.0f);
    light.outerCone = radians(30.0f);
    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());

    // Inside range and cone: 5 m straight ahead of where the spot aims.
    CHECK(lmx::engine::lightReaches(*row, glm::vec3(0.0f, 0.0f, -5.0f)));

    // Beyond range: 15 m straight ahead, past the 10 m range.
    CHECK_FALSE(lmx::engine::lightReaches(*row, glm::vec3(0.0f, 0.0f, -15.0f)));

    // Outside the cone: within range but well off the 30-degree half-angle.
    CHECK_FALSE(lmx::engine::lightReaches(*row, glm::vec3(5.0f, 0.0f, -1.0f)));
}

//======================================================================================================================
// lightReaches takes no normal, so a receiving surface facing away from the light still counts as
// reached -- exactly the position computePunctualLight's own N.L early-out zeroes for shading, to
// show the two are deliberately independent (culling admits the light; shading still decides
// whether it contributes).
TEST_CASE("lightReaches ignores surface orientation", "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Point;
    light.position = glm::vec3(0.0f, 0.0f, 1.0f);
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;
    light.range = 10.0f;
    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());

    const glm::vec3 position(0.0f);
    const glm::vec3 awayFromLight(0.0f, 0.0f, -1.0f);

    CHECK(lmx::engine::lightReaches(*row, position));
    CHECK(lmx::engine::computePunctualLight(*row, position, awayFromLight,
                                            glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.5f),
                                            glm::vec3(0.04f), 0.0f, 0.25f) == glm::vec3(0.0f));
}

//======================================================================================================================
// A non-unit shading normal (e.g. off unnormalized interpolated vertex normals) can dot to more
// than 1 with a unit light vector; Shaders/Common/Lighting.slang's ComputeDirectionalLight
// saturates N.L before using it, and this mirror must match or a GPU/CPU oracle comparison would
// disagree whenever that happens.
TEST_CASE("N.L is saturated to [0, 1] before it scales the result", "[render][light]") {
    lmx::engine::LocalLight light{};
    light.type = lmx::engine::LocalLightType::Point;
    light.position = glm::vec3(0.0f, 0.0f, 5.0f);
    light.colour = glm::vec3(1.0f);
    light.intensity = 1.0f;
    light.range = 1000.0f;
    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());

    const glm::vec3 position(0.0f);
    const glm::vec3 normal(0.0f, 0.0f, 2.0f); // deliberately non-unit; dots to 2.0 with lightVec
    const glm::vec3 toEye(0.0f, 0.0f, 1.0f);
    const glm::vec3 baseColour(0.5f, 0.6f, 0.7f);
    const glm::vec3 f0(0.04f);
    constexpr float alpha = 0.25f;

    const glm::vec3 actual = lmx::engine::computePunctualLight(*row, position, normal, toEye,
                                                               baseColour, f0, 0.0f, alpha);

    // Hand-evaluated with N.L clamped to 1, as the shader does; N.O.V is deliberately left
    // unclamped, since neither the shader nor the mirror clamps it above its floor.
    const float nol = 1.0f;
    const float nov = glm::dot(normal, toEye);
    const glm::vec3 lightVec(0.0f, 0.0f, 1.0f);
    const glm::vec3 halfVec = glm::normalize(toEye + lightVec);
    const float noh = std::clamp(glm::dot(normal, halfVec), 0.0f, 1.0f);
    const float voh = std::clamp(glm::dot(toEye, halfVec), 0.0f, 1.0f);
    const glm::vec3 fresnel = lmx::test::brdf::fSchlick(f0, voh);
    const glm::vec3 diffuse = (glm::vec3(1.0f) - fresnel) * baseColour / kPi;
    const glm::vec3 specular = lmx::test::brdf::dGgx(noh, alpha) *
                               lmx::test::brdf::vSmithHeightCorrelated(nov, nol, alpha) * fresnel;
    const glm::vec3 expected = (diffuse + specular) * nol * row->strength *
                               lmx::engine::punctualAttenuation(5.0f, row->range);

    CHECK(actual.x == Approx(expected.x));
    CHECK(actual.y == Approx(expected.y));
    CHECK(actual.z == Approx(expected.z));
}

//======================================================================================================================
TEST_CASE("Disabled local lights validate authored data and produce an inert GPU row", "[light]") {
    lmx::engine::LocalLight light;
    light.enabled = false;
    light.position = {1.0f, 2.0f, 3.0f};
    light.intensity = 50.0f;
    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row);
    REQUIRE(row->range == 0.0f);
    REQUIRE(row->boundRadius == 0.0f);
    REQUIRE(row->strength == glm::vec3(0.0f));
    REQUIRE_FALSE(lmx::engine::lightReaches(*row, glm::vec3(0.0f)));
    light.range = -1.0f;
    REQUIRE_FALSE(lmx::engine::makeLightRow(light));
}
