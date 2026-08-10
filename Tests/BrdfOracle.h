#pragma once

// CPU mirror of the shading model Shaders/Lighting.slang evaluates: GGX specular with
// height-correlated Smith visibility and Schlick Fresnel for analytic lights, and the split-sum
// image-based reconstruction with multiple-scattering compensation. Rendering tests state their
// expectation as a surface plus a lighting configuration and push it through here, so the number in
// the test is derived from the model rather than measured off an image.
//
// Kept beside the tests rather than in Source/, on the same terms as DisplayTransformOracle.h:
// nothing that ships needs a CPU copy of the BRDF, and a mirror the shader is checked against has
// to be written independently of it.
//
// The one thing this file does *not* re-derive is the split-sum DFG table: it reads the very table
// Source/Engine/Ibl.h generates and the renderer uploads, because the point of a probe comparison
// is to check the shader's use of that data, not to re-implement the integrator. The integrator has
// its own independent oracle in Tests/EngineIblTests.cpp.

#include "Engine/Ibl.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace lmx::test::brdf {

// Mirrors Shaders/Lighting.slang's constants of the same names.
inline constexpr float kDielectricF0 = 0.04f;
inline constexpr float kMinRoughness = 0.045f;
inline constexpr float kPi = 3.141592653589793f;

// What a DrawItem's Material means to the shader, after its factors and its textures have been
// combined. Tests that use the renderer's white fallbacks set these from the factors alone.
struct Surface {
    glm::vec3 baseColor{1.0f};
    float perceptualRoughness = 0.5f;
    float metallic = 0.0f;
    float occlusion = 1.0f;
    glm::vec3 emissive{0.0f};
};

//======================================================================================================================
// The shader clamps once, before squaring, and every consumer downstream reads the clamped value.
inline float clampedRoughness(float perceptualRoughness) {
    return std::clamp(perceptualRoughness, kMinRoughness, 1.0f);
}

//======================================================================================================================
inline float alphaOf(float perceptualRoughness) {
    const float r = clampedRoughness(perceptualRoughness);
    return r * r;
}

//======================================================================================================================
inline float dGgx(float noh, float alpha) {
    const float a2 = alpha * alpha;
    const float d = noh * noh * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(kPi * d * d, 1e-9f);
}

//======================================================================================================================
inline float vSmithHeightCorrelated(float nov, float nol, float alpha) {
    const float a2 = alpha * alpha;
    const float view = nov * std::sqrt(nol * nol * (1.0f - a2) + a2);
    const float light = nol * std::sqrt(nov * nov * (1.0f - a2) + a2);
    return 0.5f / std::max(view + light, 1e-5f);
}

//======================================================================================================================
inline glm::vec3 fSchlick(const glm::vec3& f0, float voh) {
    const float f = 1.0f - voh;
    const float f5 = f * f * f * f * f;
    return f0 + (glm::vec3(1.0f) - f0) * f5;
}

//======================================================================================================================
inline glm::vec3 f0Of(const Surface& surface) {
    return glm::mix(glm::vec3(kDielectricF0), surface.baseColor, surface.metallic);
}

//======================================================================================================================
// One analytic directional light. `normal` and `toEye` are unit; `lightDirection` is the way the
// rays travel, matching render::DirectionalLight::direction.
inline glm::vec3 directionalLight(const glm::vec3& strength, const glm::vec3& lightDirection,
                                  const glm::vec3& normal, const glm::vec3& toEye,
                                  const Surface& surface) {
    const glm::vec3 lightVec = -glm::normalize(lightDirection);
    const float nol = std::clamp(glm::dot(normal, lightVec), 0.0f, 1.0f);
    if (nol <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float nov = std::max(glm::dot(normal, toEye), 1e-4f);
    const glm::vec3 halfVec = glm::normalize(toEye + lightVec);
    const float noh = std::clamp(glm::dot(normal, halfVec), 0.0f, 1.0f);
    const float voh = std::clamp(glm::dot(toEye, halfVec), 0.0f, 1.0f);

    const float alpha = alphaOf(surface.perceptualRoughness);
    const glm::vec3 fresnel = fSchlick(f0Of(surface), voh);
    const glm::vec3 diffuse =
        (glm::vec3(1.0f) - fresnel) * (1.0f - surface.metallic) * surface.baseColor / kPi;
    const glm::vec3 specular = dGgx(noh, alpha) * vSmithHeightCorrelated(nov, nol, alpha) * fresnel;
    return (diffuse + specular) * nol * strength;
}

//======================================================================================================================
// The split-sum reconstruction with Fdez-Aguera's multiple-scattering compensation. `dfg` is the
// (scale, bias) pair at (N.V, clamped perceptual roughness).
inline glm::vec3 imageBasedLight(const glm::vec3& irradiance, const glm::vec3& prefiltered,
                                 const glm::vec2& dfg, const Surface& surface) {
    const glm::vec3 f0 = f0Of(surface);
    const glm::vec3 singleScatter = f0 * dfg.x + dfg.y;
    const float multiScatterEnergy = 1.0f - (dfg.x + dfg.y);
    const glm::vec3 averageFresnel = f0 + (glm::vec3(1.0f) - f0) / 21.0f;
    const glm::vec3 multiScatter =
        multiScatterEnergy * singleScatter * averageFresnel /
        glm::max(glm::vec3(1.0f) - averageFresnel * multiScatterEnergy, glm::vec3(1e-4f));
    const glm::vec3 diffuseEnergy = glm::vec3(1.0f) - (singleScatter + multiScatter);
    const glm::vec3 diffuse = surface.baseColor * (1.0f - surface.metallic) * diffuseEnergy;
    return singleScatter * prefiltered + (multiScatter + diffuse) * irradiance;
}

//======================================================================================================================
// The production DFG table, quantized to the RG16Float the renderer uploads it as, so a probe
// comparison is not charged for a precision the GPU never had. Computed once -- the table costs
// kDfgLutSize^2 * kDfgSampleCount importance samples.
inline const std::vector<glm::vec2>& productionDfgLut() {
    static const std::vector<glm::vec2> lut = [] {
        std::vector<glm::vec2> table = engine::ibl::computeDfgLut(engine::ibl::kDfgLutSize);
        for (glm::vec2& texel : table) {
            texel.x = glm::unpackHalf1x16(glm::packHalf1x16(texel.x));
            texel.y = glm::unpackHalf1x16(glm::packHalf1x16(texel.y));
        }
        return table;
    }();
    return lut;
}

//======================================================================================================================
// Clamped bilinear lookup, mirroring what the renderer's clamped IBL sampler does to the texel-
// centered table Ibl.h documents: coordinate c addresses texel c * size - 0.5, and both ends clamp
// to the outermost texel centre rather than running past them.
inline glm::vec2 sampleDfg(float nov, float perceptualRoughness) {
    const auto size = static_cast<int>(engine::ibl::kDfgLutSize);
    const std::vector<glm::vec2>& lut = productionDfgLut();

    const auto axis = [](float coordinate) {
        constexpr auto extent = static_cast<float>(engine::ibl::kDfgLutSize);
        return std::clamp(coordinate * extent - 0.5f, 0.0f, extent - 1.0f);
    };
    const float x = axis(std::clamp(nov, 0.0f, 1.0f));
    const float y = axis(clampedRoughness(perceptualRoughness));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, size - 1);
    const int y1 = std::min(y0 + 1, size - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const auto texel = [&](int ix, int iy) {
        return lut[static_cast<size_t>(iy) * static_cast<size_t>(size) + static_cast<size_t>(ix)];
    };
    const glm::vec2 top = glm::mix(texel(x0, y0), texel(x1, y0), fx);
    const glm::vec2 bottom = glm::mix(texel(x0, y1), texel(x1, y1), fx);
    return glm::mix(top, bottom, fy);
}

//======================================================================================================================
// The whole fragment, for a surface in a uniform environment of radiance `environment`. A constant
// environment is what Source/Engine/Ibl.h's generators reproduce exactly at every roughness, so
// both image-based samples are that radiance itself and no cube lookup has to be mirrored here.
//
// `lights` is the same three-light set a SceneView carries, with the shadow factor already folded
// into each strength by the caller.
inline glm::vec3 shadeInUniformEnvironment(const Surface& surface, const glm::vec3& normal,
                                           const glm::vec3& toEye, const glm::vec3& environment,
                                           std::span<const std::pair<glm::vec3, glm::vec3>> lights) {
    glm::vec3 color(0.0f);
    for (const auto& [strength, direction] : lights) {
        color += directionalLight(strength, direction, normal, toEye, surface);
    }
    const float nov = std::clamp(glm::dot(normal, toEye), 1e-4f, 1.0f);
    color += surface.occlusion *
             imageBasedLight(environment, environment,
                             sampleDfg(nov, surface.perceptualRoughness), surface);
    return color + surface.emissive;
}

} // namespace lmx::test::brdf
