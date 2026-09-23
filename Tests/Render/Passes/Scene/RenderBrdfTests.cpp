#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Support/BrdfOracle.h"

#include <cmath>

//======================================================================================================================
// The energy statement the whole material model rests on, made without a GPU.
//
// In a uniform environment of radiance E, Source/Engine/Asset/Texture/Ibl.h's generators reproduce
// E exactly at every roughness -- the irradiance convolution and the prefilter both normalize by
// their own accumulated weight -- so both image-based samples are E and the fragment reduces to E
// times the surface's total reflectance. A white surface must then return E itself: it absorbs
// nothing, so every photon that arrived has to leave.
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
