#include "Engine/Ibl.h"
#include "Engine/Scene.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace lmx::engine;
using namespace lmx::engine::ibl;

namespace {

// The environment radiance every closure below is stated against. Deliberately not grey and
// deliberately above 1.0 in one channel: a half-precision round trip that clamped to unorm range
// would show up here.
constexpr glm::vec3 kTestRadiance{0.35f, 0.62f, 1.40f};

//======================================================================================================================
bool nearVec3(const glm::vec3& a, const glm::vec3& b, float tolerance) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
           std::abs(a.z - b.z) <= tolerance;
}

//======================================================================================================================
// The LUT texel index for the texel-centered convention Ibl.h documents.
glm::vec2 dfgTexel(const std::vector<glm::vec2>& lut, uint32_t size, uint32_t x, uint32_t y) {
    REQUIRE(lut.size() == size_t{size} * size);
    return lut[size_t{y} * size + x];
}

//======================================================================================================================
// Computed once: the production LUT costs kDfgLutSize^2 * kDfgSampleCount importance samples, and
// several cases below read the same table.
const std::vector<glm::vec2>& productionDfgLut() {
    static const std::vector<glm::vec2> lut = computeDfgLut(kDfgLutSize);
    return lut;
}

//======================================================================================================================
// GGX normal distribution, height-correlated Smith visibility (V = G / (4 NoL NoV)) -- the same
// microfacet model computeDfgLut integrates, evaluated directly so the oracle below shares no code
// with it.
float ggxDistribution(float noh, float alpha) {
    const float a2 = alpha * alpha;
    const float d = (noh * a2 - noh) * noh + 1.0f;
    return a2 / (glm::pi<float>() * d * d);
}

//======================================================================================================================
float smithVisibility(float nov, float nol, float alpha) {
    const float a2 = alpha * alpha;
    const float v = nol * std::sqrt(nov * nov * (1.0f - a2) + a2);
    const float l = nov * std::sqrt(nol * nol * (1.0f - a2) + a2);
    return 0.5f / (v + l);
}

//======================================================================================================================
// Independent oracle for the split-sum DFG table: the directional albedo of single-scattering GGX
// with Fresnel fixed at 1, integrated by uniform hemisphere quadrature rather than by importance
// sampling. With F = 1 the split-sum reconstruction F0 * scale + bias collapses to scale + bias, so
// this value and the LUT texel's channel sum must agree.
double ggxDirectionalAlbedo(double nov, double alpha, int cosineSteps, int azimuthSteps) {
    const double vx = std::sqrt(1.0 - nov * nov);
    const double vz = nov;
    double total = 0.0;
    for (int i = 0; i < cosineSteps; ++i) {
        const double nol = (i + 0.5) / cosineSteps;
        const double sinTheta = std::sqrt(1.0 - nol * nol);
        for (int j = 0; j < azimuthSteps; ++j) {
            const double phi = 2.0 * glm::pi<double>() * (j + 0.5) / azimuthSteps;
            const double lx = sinTheta * std::cos(phi);
            const double ly = sinTheta * std::sin(phi);
            double hx = lx + vx, hy = ly, hz = nol + vz;
            const double length = std::sqrt(hx * hx + hy * hy + hz * hz);
            if (length < 1e-9) {
                continue;
            }
            hx /= length;
            hy /= length;
            hz /= length;
            const double noh = hz > 0.0 ? hz : 0.0;
            total +=
                static_cast<double>(
                    ggxDistribution(static_cast<float>(noh), static_cast<float>(alpha))) *
                static_cast<double>(smithVisibility(
                    static_cast<float>(nov), static_cast<float>(nol), static_cast<float>(alpha))) *
                nol;
        }
    }
    // dOmega = d(cos theta) * d(phi) over the upper hemisphere.
    return total * (1.0 / cosineSteps) * (2.0 * glm::pi<double>() / azimuthSteps);
}

//======================================================================================================================
// Byte view of a cube's payload, for the memcmp determinism cases.
std::vector<unsigned char> cubeBytes(const CpuCubemap& cube) {
    std::vector<unsigned char> bytes;
    for (const std::vector<glm::vec4>& face : cube.faces) {
        const auto* first = reinterpret_cast<const unsigned char*>(face.data());
        bytes.insert(bytes.end(), first, first + face.size() * sizeof(glm::vec4));
    }
    return bytes;
}

} // namespace

//======================================================================================================================
// Pins the face-index-to-direction convention Ibl.h documents against RHI.h's cube face order, so a
// generated cube cannot silently end up mirrored or rotated relative to the sky it came from.
TEST_CASE("cube face centers look along the RHI's face axes", "[engine][ibl]") {
    // An odd face size puts a texel exactly at each face center.
    constexpr uint32_t kSize = 5;
    constexpr uint32_t kCenter = 2;
    const glm::vec3 axes[kCubeFaceCount] = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                                            {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        INFO("face " << face);
        REQUIRE(nearVec3(faceDirection(face, kCenter, kCenter, kSize), axes[face], 1e-6f));
    }
    // Every direction is unit length, corners included.
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                REQUIRE(std::abs(glm::length(faceDirection(face, x, y, kSize)) - 1.0f) < 1e-6f);
            }
        }
    }
}

//======================================================================================================================
// The convolution's solid-angle weights are normalized by their own sum, so this closure holds
// because the weights conserve energy rather than because a particular discretization happened to
// integrate to pi.
TEST_CASE("a constant environment convolves to its own radiance", "[engine][ibl]") {
    const CpuCubemap env = makeConstantCubemap(kTestRadiance, 8);
    REQUIRE(env.faceSize == 8);
    REQUIRE(env.faces[0].size() == 64);

    const CpuCubemap irradiance = computeIrradiance(env, kIrradianceFaceSize);
    REQUIRE(irradiance.faceSize == kIrradianceFaceSize);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        REQUIRE(irradiance.faces[face].size() == size_t{kIrradianceFaceSize} * kIrradianceFaceSize);
        for (const glm::vec4& texel : irradiance.faces[face]) {
            INFO("face " << face);
            REQUIRE(nearVec3(glm::vec3(texel), kTestRadiance, 1e-5f));
        }
    }
}

//======================================================================================================================
// Same argument one integral up: the N.L weights of the split-sum prefilter normalize out, so every
// roughness level of a constant environment is that same constant.
TEST_CASE("a constant environment prefilters to a constant chain", "[engine][ibl]") {
    constexpr uint32_t kBase = 8;
    constexpr uint32_t kMips = 4;
    const CpuCubemap env = makeConstantCubemap(kTestRadiance, 4);
    const std::vector<CpuCubemap> chain = prefilterSpecular(env, kBase, kMips);

    REQUIRE(chain.size() == kMips);
    for (uint32_t mip = 0; mip < kMips; ++mip) {
        INFO("mip " << mip);
        REQUIRE(chain[mip].faceSize == std::max(1u, kBase >> mip));
        for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
            REQUIRE(chain[mip].faces[face].size() ==
                    size_t{chain[mip].faceSize} * chain[mip].faceSize);
            for (const glm::vec4& texel : chain[mip].faces[face]) {
                REQUIRE(nearVec3(glm::vec3(texel), kTestRadiance, 1e-5f));
            }
        }
    }
}

//======================================================================================================================
// The mirror level has no convolution to mask a point resample. A linear face ramp must remain
// continuous between source texel centers instead of expanding each source texel into a block.
TEST_CASE("mirror prefilter reconstructs a linear face ramp", "[engine][ibl]") {
    constexpr uint32_t kSourceSize = 8;
    constexpr uint32_t kOutputSize = 64;
    CpuCubemap env = makeConstantCubemap(glm::vec3(0.0f), kSourceSize);
    for (auto& face : env.faces) {
        for (uint32_t y = 0; y < kSourceSize; ++y) {
            for (uint32_t x = 0; x < kSourceSize; ++x) {
                face[size_t{y} * kSourceSize + x] = {(static_cast<float>(x) + 0.5f) / kSourceSize,
                                                     (static_cast<float>(y) + 0.5f) / kSourceSize,
                                                     0.5f, 1.0f};
            }
        }
    }
    const auto chain = prefilterSpecular(env, kOutputSize, 1);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t y = 4; y < kOutputSize - 4; ++y) {
            for (uint32_t x = 4; x < kOutputSize - 4; ++x) {
                const glm::vec3 expected{(static_cast<float>(x) + 0.5f) / kOutputSize,
                                         (static_cast<float>(y) + 0.5f) / kOutputSize, 0.5f};
                REQUIRE(nearVec3(glm::vec3(chain[0].faces[face][size_t{y} * kOutputSize + x]),
                                 expected, 1e-6f));
            }
        }
    }
}

//======================================================================================================================
// A direction-valued environment is continuous across all twelve edges and eight corners. Its
// analytic value checks face orientation and neighboring-face reconstruction independently of
// the resampler; unlike a constant furnace it exposes clamped or wrongly mapped seam taps.
TEST_CASE("mirror prefilter reconstructs continuous radiance across cube edges and corners",
          "[engine][ibl]") {
    constexpr uint32_t kSourceSize = 16;
    constexpr uint32_t kOutputSize = 128;
    CpuCubemap env = makeConstantCubemap(glm::vec3(0.0f), kSourceSize);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t y = 0; y < kSourceSize; ++y) {
            for (uint32_t x = 0; x < kSourceSize; ++x) {
                env.faces[face][size_t{y} * kSourceSize + x] =
                    glm::vec4(0.5f * (faceDirection(face, x, y, kSourceSize) + 1.0f), 1.0f);
            }
        }
    }
    const auto chain = prefilterSpecular(env, kOutputSize, 1);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t y = 0; y < kOutputSize; ++y) {
            for (uint32_t x = 0; x < kOutputSize; ++x) {
                if (x != 0 && y != 0 && x != kOutputSize - 1 && y != kOutputSize - 1) {
                    continue;
                }
                INFO("face " << face << " x " << x << " y " << y);
                const glm::vec3 expected = 0.5f * (faceDirection(face, x, y, kOutputSize) + 1.0f);
                REQUIRE(nearVec3(glm::vec3(chain[0].faces[face][size_t{y} * kOutputSize + x]),
                                 expected, 0.006f));
            }
        }
    }
    // Filtering a nonconstant source must retain the generator's reproducibility contract.
    const auto repeatA = prefilterSpecular(env, 8, 3);
    const auto repeatB = prefilterSpecular(env, 8, 3);
    for (size_t mip = 0; mip < repeatA.size(); ++mip) {
        REQUIRE(cubeBytes(repeatA[mip]) == cubeBytes(repeatB[mip]));
    }
}

//======================================================================================================================
// The +X face's right edge neighbors -Z. A deliberately discontinuous face color makes the
// contribution from the neighbor measurable, so merely clamping the sample cannot pass.
TEST_CASE("mirror prefilter blends a neighboring cube face at the seam", "[engine][ibl]") {
    CpuCubemap env = makeConstantCubemap(glm::vec3(0.0f), 8);
    env.faces[5].assign(64, glm::vec4(1.0f));
    const auto chain = prefilterSpecular(env, 64, 1);
    const glm::vec3 edge(chain[0].faces[0][size_t{32} * 64 + 63]);
    REQUIRE(nearVec3(edge, glm::vec3(0.4375f), 1e-6f));
}

//======================================================================================================================
// An HDR checker has a constant mean radiance but detail far below a rough reflection's sample
// footprint. Reading source mip zero at every GGX sample creates deterministic mottling. The
// footprint-filtered result must recover the mean without clipping away its above-one energy.
TEST_CASE("rough prefilter integrates fine HDR radiance without sampling mottling",
          "[engine][ibl]") {
    constexpr uint32_t kSize = 128;
    const glm::vec3 meanRadiance{4.0f, 16.0f, 32.0f};
    CpuCubemap env = makeConstantCubemap(glm::vec3(0.0f), kSize);
    for (auto& face : env.faces) {
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                face[size_t{y} * kSize + x] =
                    glm::vec4(((x + y) % 2 == 0) ? 2.0f * meanRadiance : glm::vec3(0.0f), 1.0f);
            }
        }
    }
    const auto chain = prefilterSpecular(env, 16, 3);
    glm::vec3 sum(0.0f);
    for (const auto& face : chain.back().faces) {
        for (const glm::vec4& texel : face) {
            REQUIRE(nearVec3(glm::vec3(texel), meanRadiance, 0.02f));
            sum += glm::vec3(texel);
        }
    }
    const float texelCount =
        static_cast<float>(kCubeFaceCount * chain.back().faceSize * chain.back().faceSize);
    REQUIRE(nearVec3(sum / texelCount, meanRadiance, 0.005f));
}

//======================================================================================================================
// Odd extents split source texels between mip cells. Constants must retain their radiance, and
// an environment lit only along its last row/column must retain that energy instead of losing
// the unmatched texels. At roughness one, V=N makes the specular integral cosine weighted, so
// the independent diffuse quadrature provides a bounded check of its mean (not bitwise parity).
TEST_CASE("rough prefilter retains odd-sized source radiance including border texels",
          "[engine][ibl]") {
    constexpr uint32_t kSize = 63;
    const auto constant = prefilterSpecular(makeConstantCubemap(kTestRadiance, kSize), 8, 3);
    for (const auto& level : constant) {
        for (const auto& face : level.faces) {
            for (const auto& texel : face) {
                REQUIRE(nearVec3(glm::vec3(texel), kTestRadiance, 1e-5f));
            }
        }
    }
    CpuCubemap border = makeConstantCubemap(glm::vec3(0.0f), kSize);
    for (auto& face : border.faces) {
        for (uint32_t i = 0; i < kSize; ++i) {
            face[size_t{kSize - 1} * kSize + i] = glm::vec4(16.0f, 16.0f, 16.0f, 1.0f);
            face[size_t{i} * kSize + kSize - 1] = glm::vec4(16.0f, 16.0f, 16.0f, 1.0f);
        }
    }
    const auto filtered = prefilterSpecular(border, 8, 2);
    const CpuCubemap reference = computeIrradiance(border, filtered.back().faceSize);
    float actualMean = 0.0f;
    float referenceMean = 0.0f;
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (size_t texel = 0; texel < reference.faces[face].size(); ++texel) {
            actualMean += filtered.back().faces[face][texel].x;
            referenceMean += reference.faces[face][texel].x;
        }
    }
    const float count =
        static_cast<float>(kCubeFaceCount * reference.faceSize * reference.faceSize);
    actualMean /= count;
    referenceMean /= count;
    INFO("rough mean " << actualMean << ", independent diffuse mean " << referenceMean);
    REQUIRE(referenceMean > 0.1f);
    REQUIRE(std::abs(actualMean - referenceMean) < 0.03f);
}

//======================================================================================================================
// At normal incidence and mirror roughness the whole integral collapses: the half vector is the
// normal, Schlick's (1 - V.H)^5 vanishes, and the split sum degenerates to F0 * 1 + 0.
TEST_CASE("the DFG LUT reduces to F0 at normal incidence and mirror roughness", "[engine][ibl]") {
    const std::vector<glm::vec2>& lut = productionDfgLut();
    const glm::vec2 corner = dfgTexel(lut, kDfgLutSize, kDfgLutSize - 1, 0);
    REQUIRE(std::abs(corner.x - 1.0f) < 0.02f);
    REQUIRE(std::abs(corner.y - 0.0f) < 0.02f);
}

//======================================================================================================================
// Correctness of the integration itself, against a quadrature that shares no code with it.
//
// The two named texels and their reference directional albedos, computed by uniform hemisphere
// quadrature of D * V * N.L at 1024 x 2048 steps (converged to 1e-6):
//
//   texel (47, 15): N.V = 0.7421875, perceptual roughness = 0.2421875 -> 0.994510
//   texel (31, 47): N.V = 0.4921875, perceptual roughness = 0.7421875 -> 0.671113
//
// Karis's published EnvBRDFApprox fit ("Physically Based Shading on Mobile", 2014) is deliberately
// *not* the reference here: it fits Unreal's split-sum table, whose Smith G uses k = alpha / 2,
// and it disagrees with the height-correlated Smith visibility this project shades with by up to
// 0.18 in the scale channel at mid roughness -- far outside any tolerance worth asserting. The
// quadrature above agrees with computeDfgLut to better than 0.001 instead.
TEST_CASE("the DFG LUT matches an independent quadrature of the GGX directional albedo",
          "[engine][ibl]") {
    const std::vector<glm::vec2>& lut = productionDfgLut();

    struct Probe {
        uint32_t x, y;
        double referenceAlbedo;
        float expectedScale;
    };
    // expectedScale pins this integrator's own scale channel; the albedo above validates it.
    const Probe probes[] = {{47, 15, 0.994510, 0.9938f}, {31, 47, 0.671113, 0.6623f}};

    for (const Probe& probe : probes) {
        INFO("texel (" << probe.x << ", " << probe.y << ")");
        const double nov = (probe.x + 0.5) / kDfgLutSize;
        const double roughness = (probe.y + 0.5) / kDfgLutSize;
        const double alpha = roughness * roughness;

        const double oracle = ggxDirectionalAlbedo(nov, alpha, 256, 512);
        REQUIRE(std::abs(oracle - probe.referenceAlbedo) < 1e-4);

        const glm::vec2 texel = dfgTexel(lut, kDfgLutSize, probe.x, probe.y);
        REQUIRE(std::abs(static_cast<double>(texel.x + texel.y) - oracle) < 0.005);
        REQUIRE(std::abs(texel.x - probe.expectedScale) < 0.03f);
    }
}

//======================================================================================================================
// A single-scattering microfacet BRDF can only lose energy, never create it, and it loses more of
// it the rougher the surface gets. Monotonicity is asserted at a facing view direction and across
// well-separated roughness rows: near grazing incidence a rough surface reflects *more* than a
// smooth one, and adjacent rows at alpha near zero differ by less than the Monte Carlo noise floor.
TEST_CASE("the DFG LUT is energy-bounded and loses energy with roughness", "[engine][ibl]") {
    const std::vector<glm::vec2>& lut = productionDfgLut();

    for (const glm::vec2& texel : lut) {
        REQUIRE(texel.x >= 0.0f);
        REQUIRE(texel.y >= 0.0f);
        // Reflectance for F0 = 1 is scale + bias; 1.001 is the estimator's own noise, measured at
        // a maximum of 1.00005 over the table.
        REQUIRE(texel.x + texel.y <= 1.001f);
    }

    constexpr uint32_t kFacingColumn = 47; // N.V = 0.7421875
    const uint32_t rows[] = {0, 16, 32, 48, 63};
    float previous = 2.0f;
    for (uint32_t row : rows) {
        INFO("row " << row);
        const float scale = dfgTexel(lut, kDfgLutSize, kFacingColumn, row).x;
        REQUIRE(scale < previous);
        previous = scale;
    }
}

//======================================================================================================================
// Hammersley points and a fixed traversal order, not an RNG and not a parallel reduction: repeating
// a generation reproduces it bit for bit, which is the property that lets a generated IBL set be
// treated as a build artifact.
TEST_CASE("the generators are byte-identical across runs", "[engine][ibl]") {
    const CpuCubemap env = makeConstantCubemap(glm::vec3(0.21f, 0.44f, 0.87f), 4);

    const std::vector<unsigned char> irradianceA = cubeBytes(computeIrradiance(env, 8));
    const std::vector<unsigned char> irradianceB = cubeBytes(computeIrradiance(env, 8));
    REQUIRE(irradianceA.size() > 0);
    REQUIRE(irradianceA.size() == irradianceB.size());
    REQUIRE(std::memcmp(irradianceA.data(), irradianceB.data(), irradianceA.size()) == 0);

    const std::vector<CpuCubemap> chainA = prefilterSpecular(env, 8, 3);
    const std::vector<CpuCubemap> chainB = prefilterSpecular(env, 8, 3);
    REQUIRE(chainA.size() == chainB.size());
    REQUIRE(chainA.size() == 3);
    for (size_t mip = 0; mip < chainA.size(); ++mip) {
        INFO("mip " << mip);
        const std::vector<unsigned char> a = cubeBytes(chainA[mip]);
        const std::vector<unsigned char> b = cubeBytes(chainB[mip]);
        REQUIRE(a.size() == b.size());
        REQUIRE(std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    const std::vector<glm::vec2> lutA = computeDfgLut(16);
    const std::vector<glm::vec2> lutB = computeDfgLut(16);
    REQUIRE(lutA.size() == 256);
    REQUIRE(lutB.size() == lutA.size());
    REQUIRE(std::memcmp(lutA.data(), lutB.data(), lutA.size() * sizeof(glm::vec2)) == 0);
}

//======================================================================================================================
// The uploaded set every catalog scene carries. MaterialLab has an asset-free fallback, so this
// runs wherever a Metal 4 device exists.
TEST_CASE("a built scene carries its uploaded IBL textures", "[gpu]") {
    auto device = lmx::rhi::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadMaterialLabScene(**device);
    REQUIRE(scene.has_value());

    REQUIRE((*scene)->irradianceMap != nullptr);
    REQUIRE((*scene)->irradianceMap->width() == kIrradianceFaceSize);
    REQUIRE((*scene)->irradianceMap->height() == kIrradianceFaceSize);

    REQUIRE((*scene)->prefilteredEnvMap != nullptr);
    REQUIRE((*scene)->skyCubemap != nullptr);
    const uint32_t reflectionSize =
        (*scene)->skyCubemap->width() == 1 ? kSpecularBaseFaceSize : 128;
    REQUIRE((*scene)->prefilteredEnvMap->width() == reflectionSize);
    REQUIRE((*scene)->prefilteredEnvMap->height() == reflectionSize);

    REQUIRE((*scene)->dfgLut != nullptr);
    REQUIRE((*scene)->dfgLut->width() == kDfgLutSize);
    REQUIRE((*scene)->dfgLut->height() == kDfgLutSize);
    // rhi::Texture exposes no mip count, so the prefiltered chain's kSpecularMipCount levels stay
    // pinned CPU-side by the prefilterSpecular case above rather than read back off the device.

    // The sky the set was generated from is still uploaded alongside it, unchanged.
    REQUIRE((*scene)->skyCubemap != nullptr);
}
