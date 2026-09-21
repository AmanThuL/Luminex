//----------------------------------------------------------------------------------------------------------------------
/// @file Ibl.cpp
/// @brief Implements deterministic CPU image-based-lighting generation.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Texture/Ibl.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Sampling.h"
#include "Core/Math/Sequence.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace lmx::asset::ibl {

namespace {

//======================================================================================================================
// Height-correlated Smith visibility (Heitz 2014): G / (4 N.L N.V), with alpha the squared
// perceptual roughness. The same visibility the direct-lighting BRDF uses, so the DFG table and
// the analytic lights agree on how much energy a rough surface loses.
float smithVisibility(float nov, float nol, float alpha) {
    const float alpha2 = alpha * alpha;
    const float view = nol * std::sqrt(nov * nov * (1.0f - alpha2) + alpha2);
    const float light = nov * std::sqrt(nol * nol * (1.0f - alpha2) + alpha2);
    return 0.5f / (view + light);
}

struct CubeLocation {
    uint32_t face;
    float u;
    float v;
};

//======================================================================================================================
CubeLocation projectCube(const glm::vec3& direction) {
    const glm::vec3 magnitude = glm::abs(direction);
    uint32_t face = 0;
    float s = 0.0f, t = 0.0f, major = 0.0f;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        major = magnitude.x;
        face = direction.x > 0.0f ? 0u : 1u;
        s = direction.x > 0.0f ? -direction.z : direction.z;
        t = -direction.y;
    } else if (magnitude.y >= magnitude.z) {
        major = magnitude.y;
        face = direction.y > 0.0f ? 2u : 3u;
        s = direction.x;
        t = direction.y > 0.0f ? direction.z : -direction.z;
    } else {
        major = magnitude.z;
        face = direction.z > 0.0f ? 4u : 5u;
        s = direction.z > 0.0f ? direction.x : -direction.x;
        t = -direction.y;
    }
    LMX_ASSERT(major > 0.0f, "projectCube: the sample direction is degenerate");

    return {face, 0.5f * (s / major + 1.0f), 0.5f * (t / major + 1.0f)};
}

//======================================================================================================================
glm::vec3 cubeTap(const CpuCubemap& map, uint32_t face, int32_t x, int32_t y) {
    const auto size = static_cast<int32_t>(map.faceSize);
    if (x < 0 || x >= size || y < 0 || y >= size) {
        const float inverse = 1.0f / static_cast<float>(map.faceSize);
        const CubeLocation neighbor = projectCube(
            cubeFaceDirection(face, 2.0f * (static_cast<float>(x) + 0.5f) * inverse - 1.0f,
                              2.0f * (static_cast<float>(y) + 0.5f) * inverse - 1.0f));
        face = neighbor.face;
        x = std::clamp(static_cast<int32_t>(neighbor.u * size), 0, size - 1);
        y = std::clamp(static_cast<int32_t>(neighbor.v * size), 0, size - 1);
    }
    return glm::vec3(map.faces[face][size_t{static_cast<uint32_t>(y)} * map.faceSize +
                                     static_cast<uint32_t>(x)]);
}

//======================================================================================================================
// Sample radiance continuously even at mirror roughness, where importance sampling cannot hide
// nearest-neighbor blocks. Difference-form interpolation preserves constant radiance exactly.
glm::vec3 sampleCubeLinear(const CpuCubemap& map, const glm::vec3& direction) {
    const CubeLocation location = projectCube(direction);
    const float x = location.u * static_cast<float>(map.faceSize) - 0.5f;
    const float y = location.v * static_cast<float>(map.faceSize) - 0.5f;
    const auto x0 = static_cast<int32_t>(std::floor(x));
    const auto y0 = static_cast<int32_t>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const glm::vec3 a = cubeTap(map, location.face, x0, y0);
    const glm::vec3 b = cubeTap(map, location.face, x0 + 1, y0);
    const glm::vec3 c = cubeTap(map, location.face, x0, y0 + 1);
    const glm::vec3 d = cubeTap(map, location.face, x0 + 1, y0 + 1);
    const glm::vec3 upper = a + fx * (b - a);
    const glm::vec3 lower = c + fx * (d - c);
    return upper + fy * (lower - upper);
}

//======================================================================================================================
// Reduce radiance before taking wide GGX samples. Solid-angle weights preserve the source's
// spherical energy; exact overlap rectangles include the last row/column of odd extents.
// Accumulating differences from one texel preserves constant environments exactly.
std::vector<CpuCubemap> makeRadianceMips(const CpuCubemap& env) {
    std::vector<CpuCubemap> mips{env};
    while (mips.back().faceSize > 1) {
        const CpuCubemap& source = mips.back();
        CpuCubemap level;
        level.faceSize = std::max(1u, source.faceSize / 2);
        const float scale = static_cast<float>(source.faceSize) / level.faceSize;
        for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
            level.faces[face].resize(size_t{level.faceSize} * level.faceSize);
            for (uint32_t y = 0; y < level.faceSize; ++y) {
                for (uint32_t x = 0; x < level.faceSize; ++x) {
                    const float left = static_cast<float>(x) * scale;
                    const float right = static_cast<float>(x + 1) * scale;
                    const float top = static_cast<float>(y) * scale;
                    const float bottom = static_cast<float>(y + 1) * scale;
                    const auto x0 = static_cast<uint32_t>(left);
                    const auto y0 = static_cast<uint32_t>(top);
                    const auto x1 =
                        std::min(source.faceSize, static_cast<uint32_t>(std::ceil(right)));
                    const auto y1 =
                        std::min(source.faceSize, static_cast<uint32_t>(std::ceil(bottom)));
                    const glm::vec3 reference(
                        source.faces[face][size_t{y0} * source.faceSize + x0]);
                    glm::vec3 difference(0.0f);
                    float weightSum = 0.0f;
                    for (uint32_t sy = y0; sy < y1; ++sy) {
                        for (uint32_t sx = x0; sx < x1; ++sx) {
                            const float inverse = 2.0f / static_cast<float>(source.faceSize);
                            const float u0 =
                                std::max(left, static_cast<float>(sx)) * inverse - 1.0f;
                            const float u1 =
                                std::min(right, static_cast<float>(sx + 1)) * inverse - 1.0f;
                            const float v0 = std::max(top, static_cast<float>(sy)) * inverse - 1.0f;
                            const float v1 =
                                std::min(bottom, static_cast<float>(sy + 1)) * inverse - 1.0f;
                            const float weight = cubeAreaElement(u1, v1) - cubeAreaElement(u0, v1) -
                                                 cubeAreaElement(u1, v0) + cubeAreaElement(u0, v0);
                            const glm::vec3 radiance(
                                source.faces[face][size_t{sy} * source.faceSize + sx]);
                            difference += (radiance - reference) * weight;
                            weightSum += weight;
                        }
                    }
                    level.faces[face][size_t{y} * level.faceSize + x] =
                        glm::vec4(reference + difference / weightSum, 1.0f);
                }
            }
        }
        mips.push_back(std::move(level));
    }
    return mips;
}

//======================================================================================================================
glm::vec3 sampleRadianceMips(const std::vector<CpuCubemap>& mips, const glm::vec3& direction,
                             float lod) {
    const float clamped = std::clamp(lod, 0.0f, static_cast<float>(mips.size() - 1));
    const auto lower = static_cast<size_t>(clamped);
    const size_t upper = std::min(lower + 1, mips.size() - 1);
    const glm::vec3 a = sampleCubeLinear(mips[lower], direction);
    const glm::vec3 b = sampleCubeLinear(mips[upper], direction);
    return a + (clamped - static_cast<float>(lower)) * (b - a);
}

//======================================================================================================================
// One texel of the split-sum DFG table: the (scale, bias) that reconstruct a specular response as
// F0 * scale + bias. Importance-sampling the GGX NDF cancels D and the BRDF's 1 / (4 N.L N.V)
// against the sample pdf D * N.H / (4 V.H), which is why each sample contributes only
// 4 * visibility * N.L * V.H / N.H (Karis, "Real Shading in Unreal Engine 4", with the
// height-correlated Smith visibility this project shades with substituted for Unreal's k = a/2
// Smith-Schlick approximation).
glm::vec2 integrateDfg(float nov, float alpha) {
    const glm::vec3 view(std::sqrt(std::max(0.0f, 1.0f - nov * nov)), 0.0f, nov);
    float scale = 0.0f;
    float bias = 0.0f;
    for (uint32_t sample = 0; sample < kDfgSampleCount; ++sample) {
        const glm::vec3 half = sampleGgxHalfVector(hammersley(sample, kDfgSampleCount), alpha);
        const glm::vec3 light = 2.0f * glm::dot(view, half) * half - view;
        const float nol = light.z;
        const float noh = half.z;
        const float voh = glm::dot(view, half);
        if (nol <= 0.0f || noh <= 0.0f || voh <= 0.0f) {
            continue;
        }
        const float weight = 4.0f * smithVisibility(nov, nol, alpha) * nol * voh / noh;
        const float schlick = 1.0f - voh;
        const float fresnel = schlick * schlick * schlick * schlick * schlick;
        scale += weight * (1.0f - fresnel);
        bias += weight * fresnel;
    }
    return glm::vec2(scale, bias) / static_cast<float>(kDfgSampleCount);
}

} // namespace

//======================================================================================================================
glm::vec3 faceDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t faceSize) {
    LMX_ASSERT(face < kCubeFaceCount, "faceDirection: face index is out of range");
    LMX_ASSERT(faceSize > 0 && x < faceSize && y < faceSize,
               "faceDirection: texel coordinates are out of range");
    const float inverse = 1.0f / static_cast<float>(faceSize);
    const float u = 2.0f * (static_cast<float>(x) + 0.5f) * inverse - 1.0f;
    const float v = 2.0f * (static_cast<float>(y) + 0.5f) * inverse - 1.0f;
    return glm::normalize(cubeFaceDirection(face, u, v));
}

//======================================================================================================================
CpuCubemap makeConstantCubemap(const glm::vec3& radiance, uint32_t faceSize) {
    LMX_ASSERT(faceSize > 0, "makeConstantCubemap: faceSize must be at least 1");
    CpuCubemap cube;
    cube.faceSize = faceSize;
    for (std::vector<glm::vec4>& face : cube.faces) {
        face.assign(size_t{faceSize} * faceSize, glm::vec4(radiance, 1.0f));
    }
    return cube;
}

//======================================================================================================================
CpuCubemap computeIrradiance(const CpuCubemap& env, uint32_t outFaceSize) {
    LMX_ASSERT(env.faceSize > 0, "computeIrradiance: the environment cube has no texels");
    LMX_ASSERT(outFaceSize > 0, "computeIrradiance: outFaceSize must be at least 1");

    // Flattened once in face-major scanline order so the inner loop's traversal -- and therefore
    // the order the float32 sums accumulate in -- is fixed.
    struct Texel {
        glm::vec3 direction;
        glm::vec3 radiance;
        float solidAngle;
    };
    std::vector<Texel> source;
    source.reserve(size_t{kCubeFaceCount} * env.faceSize * env.faceSize);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        LMX_ASSERT(env.faces[face].size() == size_t{env.faceSize} * env.faceSize,
                   "computeIrradiance: a face does not hold faceSize * faceSize texels");
        for (uint32_t y = 0; y < env.faceSize; ++y) {
            for (uint32_t x = 0; x < env.faceSize; ++x) {
                source.push_back({faceDirection(face, x, y, env.faceSize),
                                  glm::vec3(env.faces[face][size_t{y} * env.faceSize + x]),
                                  cubeTexelSolidAngle(x, y, env.faceSize)});
            }
        }
    }

    CpuCubemap out;
    out.faceSize = outFaceSize;
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        out.faces[face].resize(size_t{outFaceSize} * outFaceSize);
        for (uint32_t y = 0; y < outFaceSize; ++y) {
            for (uint32_t x = 0; x < outFaceSize; ++x) {
                const glm::vec3 normal = faceDirection(face, x, y, outFaceSize);
                glm::vec3 sum(0.0f);
                float weightSum = 0.0f;
                for (const Texel& texel : source) {
                    const float cosine = glm::dot(normal, texel.direction);
                    if (cosine <= 0.0f) {
                        continue;
                    }
                    const float weight = cosine * texel.solidAngle;
                    sum += texel.radiance * weight;
                    weightSum += weight;
                }
                // Dividing by the accumulated weight rather than by pi is what makes this exact
                // for a constant environment: the discretization error cancels between the two
                // sums instead of surviving as a scale factor.
                const glm::vec3 irradiance = weightSum > 0.0f ? sum / weightSum : glm::vec3(0.0f);
                out.faces[face][size_t{y} * outFaceSize + x] = glm::vec4(irradiance, 1.0f);
            }
        }
    }
    return out;
}

//======================================================================================================================
std::vector<CpuCubemap> prefilterSpecular(const CpuCubemap& env, uint32_t baseFaceSize,
                                          uint32_t mipCount) {
    LMX_ASSERT(env.faceSize > 0, "prefilterSpecular: the environment cube has no texels");
    LMX_ASSERT(baseFaceSize > 0, "prefilterSpecular: baseFaceSize must be at least 1");
    LMX_ASSERT(mipCount > 0, "prefilterSpecular: a chain needs at least its base level");

    for (const auto& face : env.faces) {
        LMX_ASSERT(face.size() == size_t{env.faceSize} * env.faceSize,
                   "prefilterSpecular: a face does not hold faceSize * faceSize texels");
    }
    // Authored neutral environments have one identical texel on every face. Keep their hot path
    // as cheap as a direct read while preserving the same weighted accumulation as other inputs.
    const glm::vec3 constantRadiance(env.faces[0][0]);
    const bool constantEnvironment =
        env.faceSize == 1 && std::all_of(env.faces.begin(), env.faces.end(), [&](const auto& face) {
            return glm::vec3(face[0]) == constantRadiance;
        });
    const std::vector<CpuCubemap> sourceMips =
        constantEnvironment ? std::vector<CpuCubemap>{} : makeRadianceMips(env);
    const auto sampleEnvironment = [&](const glm::vec3& direction, float lod) {
        return constantEnvironment ? constantRadiance
                                   : sampleRadianceMips(sourceMips, direction, lod);
    };
    std::vector<CpuCubemap> chain;
    chain.reserve(mipCount);
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        const uint32_t faceSize = std::max(1u, baseFaceSize >> mip);
        const float roughness =
            mipCount > 1 ? static_cast<float>(mip) / static_cast<float>(mipCount - 1) : 0.0f;
        const float alpha = roughness * roughness;
        std::array<float, kSpecularSampleCount> sourceLods{};
        if (mip > 0 && !constantEnvironment) {
            const float alpha2 = alpha * alpha;
            const float sourceTexelAngle =
                4.0f * glm::pi<float>() /
                (6.0f * static_cast<float>(env.faceSize) * static_cast<float>(env.faceSize));
            for (uint32_t sample = 0; sample < kSpecularSampleCount; ++sample) {
                // V=N makes the reflected-direction PDF D*N.H/(4*V.H) collapse to D/4.
                // The half-vector's local Z is independent of the output texel's normal, so
                // every texel at this roughness shares these sample footprints.
                const float noh =
                    sampleGgxHalfVector(hammersley(sample, kSpecularSampleCount), alpha).z;
                const float denominator = noh * noh * (alpha2 - 1.0f) + 1.0f;
                const float pdf = alpha2 / (4.0f * glm::pi<float>() * denominator * denominator);
                const float sampleSolidAngle = 1.0f / (kSpecularSampleCount * pdf);
                sourceLods[sample] =
                    std::max(0.0f, 0.5f * std::log2(sampleSolidAngle / sourceTexelAngle));
            }
        }

        CpuCubemap level;
        level.faceSize = faceSize;
        for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
            level.faces[face].resize(size_t{faceSize} * faceSize);
            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const glm::vec3 normal = faceDirection(face, x, y, faceSize);
                    glm::vec3 filtered = sampleEnvironment(normal, 0.0f);
                    if (mip > 0) {
                        // Normal, view and reflection all collapse onto the texel direction --
                        // the assumption that lets this be a preprocess at all, at the cost of
                        // the stretched highlight a grazing view would show.
                        glm::vec3 sum(0.0f);
                        float weightSum = 0.0f;
                        for (uint32_t s = 0; s < kSpecularSampleCount; ++s) {
                            const glm::vec3 half = sampleGgxHalfVector(
                                hammersley(s, kSpecularSampleCount), alpha, normal);
                            const glm::vec3 light = 2.0f * glm::dot(normal, half) * half - normal;
                            const float nol = glm::dot(normal, light);
                            if (nol <= 0.0f) {
                                continue;
                            }
                            sum += sampleEnvironment(light, sourceLods[s]) * nol;
                            weightSum += nol;
                        }
                        if (weightSum > 0.0f) {
                            filtered = sum / weightSum;
                        }
                    }
                    level.faces[face][size_t{y} * faceSize + x] = glm::vec4(filtered, 1.0f);
                }
            }
        }
        chain.push_back(std::move(level));
    }
    return chain;
}

//======================================================================================================================
std::vector<glm::vec2> computeDfgLut(uint32_t size) {
    LMX_ASSERT(size > 0, "computeDfgLut: size must be at least 1");
    std::vector<glm::vec2> lut(size_t{size} * size);
    for (uint32_t y = 0; y < size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        const float alpha = roughness * roughness;
        for (uint32_t x = 0; x < size; ++x) {
            const float nov = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            lut[size_t{y} * size + x] = integrateDfg(nov, alpha);
        }
    }
    return lut;
}

} // namespace lmx::asset::ibl
