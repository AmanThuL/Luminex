//----------------------------------------------------------------------------------------------------------------------
/// @file Ibl.cpp
/// @brief Implements deterministic CPU image-based-lighting generation.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Ibl.h"

#include "Core/Assert.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace lmx::engine::ibl {

namespace {

// One RGBA16Float texel; the cube upload below writes rows of these.
constexpr uint64_t kHalf4Stride = 8;
constexpr uint64_t kHalf2Stride = 4;

//======================================================================================================================
// Van der Corput radical inverse in base 2: the low-discrepancy second coordinate of the
// Hammersley sequence, produced by reversing the bits of `index`. A sequence rather than an RNG is
// what makes every generator here reproducible without carrying a seed.
float radicalInverse(uint32_t index) {
    index = (index << 16u) | (index >> 16u);
    index = ((index & 0x55555555u) << 1u) | ((index & 0xAAAAAAAAu) >> 1u);
    index = ((index & 0x33333333u) << 2u) | ((index & 0xCCCCCCCCu) >> 2u);
    index = ((index & 0x0F0F0F0Fu) << 4u) | ((index & 0xF0F0F0F0u) >> 4u);
    index = ((index & 0x00FF00FFu) << 8u) | ((index & 0xFF00FF00u) >> 8u);
    return static_cast<float>(index) * 2.3283064365386963e-10f; // 1 / 2^32
}

//======================================================================================================================
glm::vec2 hammersley(uint32_t index, uint32_t count) {
    return {static_cast<float>(index) / static_cast<float>(count), radicalInverse(index)};
}

//======================================================================================================================
// A GGX half vector drawn from the NDF, in a tangent frame whose normal is +Z.
glm::vec3 importanceSampleGgxLocal(const glm::vec2& xi, float alpha) {
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * glm::pi<float>() * xi.x;
    const float cosTheta =
        std::sqrt(std::max(0.0f, (1.0f - xi.y) / (1.0f + (alpha2 - 1.0f) * xi.y)));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

//======================================================================================================================
// The same half vector rotated into the frame of `normal`. Which orthonormal basis the frame uses
// only rotates the sample pattern about the normal, so the arbitrary `up` choice below is free --
// it just has to stay away from being parallel to the normal.
glm::vec3 importanceSampleGgx(const glm::vec2& xi, float alpha, const glm::vec3& normal) {
    const glm::vec3 local = importanceSampleGgxLocal(xi, alpha);
    const glm::vec3 up =
        std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangentX = glm::normalize(glm::cross(up, normal));
    const glm::vec3 tangentY = glm::cross(normal, tangentX);
    return glm::normalize(tangentX * local.x + tangentY * local.y + normal * local.z);
}

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

//======================================================================================================================
// Signed solid angle of the [-1, x] x [-1, y] corner region of a cube face, in the face's own
// [-1, 1] parameterization. Differencing four of these gives one texel's solid angle.
float areaElement(float x, float y) {
    return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
}

//======================================================================================================================
float texelSolidAngle(uint32_t x, uint32_t y, uint32_t faceSize) {
    const float inverse = 1.0f / static_cast<float>(faceSize);
    const float u = 2.0f * (static_cast<float>(x) + 0.5f) * inverse - 1.0f;
    const float v = 2.0f * (static_cast<float>(y) + 0.5f) * inverse - 1.0f;
    return areaElement(u - inverse, v - inverse) - areaElement(u - inverse, v + inverse) -
           areaElement(u + inverse, v - inverse) + areaElement(u + inverse, v + inverse);
}

//======================================================================================================================
// The exact inverse of faceDirection: picks the face `direction` points at by its major axis and
// returns that face's nearest texel. Nearest rather than bilinear because the prefilter's own
// sample count, not the reconstruction filter, is what carries its accuracy here.
glm::vec3 sampleCubeNearest(const CpuCubemap& map, const glm::vec3& direction) {
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
    LMX_ASSERT(major > 0.0f, "sampleCubeNearest: the sample direction is degenerate");

    const float size = static_cast<float>(map.faceSize);
    const float u = 0.5f * (s / major + 1.0f) * size;
    const float v = 0.5f * (t / major + 1.0f) * size;
    const auto clampTexel = [&](float coordinate) {
        return static_cast<uint32_t>(std::clamp(static_cast<int32_t>(std::floor(coordinate)), 0,
                                                static_cast<int32_t>(map.faceSize) - 1));
    };
    return glm::vec3(map.faces[face][size_t{clampTexel(v)} * map.faceSize + clampTexel(u)]);
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
        const glm::vec3 half = importanceSampleGgxLocal(hammersley(sample, kDfgSampleCount), alpha);
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

//======================================================================================================================
// Uploads a cube chain as RGBA16Float. `chain` is mip-major (chain[0] is the base level); the
// staging buffers stay alive across createTexture because TextureMip only borrows their storage.
rhi::Result<std::unique_ptr<rhi::Texture>>
uploadCube(rhi::Device& device, std::span<const CpuCubemap> chain, const std::string& label) {
    LMX_ASSERT(!chain.empty(), "uploadCube: a cube needs at least its base level");
    const uint32_t mipCount = static_cast<uint32_t>(chain.size());

    // Face-major, mip-major within a face -- the order createTexture documents for a cube.
    std::vector<std::vector<uint16_t>> staging;
    staging.reserve(size_t{kCubeFaceCount} * mipCount);
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            const std::vector<glm::vec4>& texels = chain[mip].faces[face];
            LMX_ASSERT(texels.size() == size_t{chain[mip].faceSize} * chain[mip].faceSize,
                       "uploadCube: a face does not hold faceSize * faceSize texels");
            std::vector<uint16_t> halves(texels.size() * 4);
            for (size_t texel = 0; texel < texels.size(); ++texel) {
                for (int channel = 0; channel < 4; ++channel) {
                    halves[texel * 4 + static_cast<size_t>(channel)] =
                        glm::packHalf1x16(texels[texel][channel]);
                }
            }
            staging.push_back(std::move(halves));
        }
    }

    std::vector<rhi::TextureMip> mips;
    mips.reserve(staging.size());
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            mips.push_back({.data = staging[size_t{face} * mipCount + mip].data(),
                            .bytesPerRow = uint64_t{chain[mip].faceSize} * kHalf4Stride});
        }
    }
    return device.createTexture({.width = chain[0].faceSize,
                                 .height = chain[0].faceSize,
                                 .format = rhi::Format::RGBA16Float,
                                 .kind = rhi::TextureKind::Cube,
                                 .mipLevels = mipCount,
                                 .sampled = true,
                                 .label = label},
                                mips);
}

//======================================================================================================================
rhi::Result<std::unique_ptr<rhi::Texture>> uploadDfgLut(rhi::Device& device,
                                                        const std::vector<glm::vec2>& lut,
                                                        uint32_t size, const std::string& label) {
    std::vector<uint16_t> halves(lut.size() * 2);
    for (size_t texel = 0; texel < lut.size(); ++texel) {
        halves[texel * 2] = glm::packHalf1x16(lut[texel].x);
        halves[texel * 2 + 1] = glm::packHalf1x16(lut[texel].y);
    }
    const rhi::TextureMip mip{.data = halves.data(), .bytesPerRow = uint64_t{size} * kHalf2Stride};
    const std::array<rhi::TextureMip, 1> mips = {mip};
    return device.createTexture({.width = size,
                                 .height = size,
                                 .format = rhi::Format::RG16Float,
                                 .mipLevels = 1,
                                 .sampled = true,
                                 .label = label},
                                mips);
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
    glm::vec3 direction(0.0f);
    switch (face) {
    case 0:
        direction = {1.0f, -v, -u};
        break;
    case 1:
        direction = {-1.0f, -v, u};
        break;
    case 2:
        direction = {u, 1.0f, v};
        break;
    case 3:
        direction = {u, -1.0f, -v};
        break;
    case 4:
        direction = {u, -v, 1.0f};
        break;
    default:
        direction = {-u, -v, -1.0f};
        break;
    }
    return glm::normalize(direction);
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
                                  texelSolidAngle(x, y, env.faceSize)});
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

    std::vector<CpuCubemap> chain;
    chain.reserve(mipCount);
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        const uint32_t faceSize = std::max(1u, baseFaceSize >> mip);
        const float roughness =
            mipCount > 1 ? static_cast<float>(mip) / static_cast<float>(mipCount - 1) : 0.0f;
        const float alpha = roughness * roughness;

        CpuCubemap level;
        level.faceSize = faceSize;
        for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
            level.faces[face].resize(size_t{faceSize} * faceSize);
            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const glm::vec3 normal = faceDirection(face, x, y, faceSize);
                    glm::vec3 filtered = sampleCubeNearest(env, normal);
                    if (mip > 0) {
                        // Normal, view and reflection all collapse onto the texel direction --
                        // the assumption that lets this be a preprocess at all, at the cost of
                        // the stretched highlight a grazing view would show.
                        glm::vec3 sum(0.0f);
                        float weightSum = 0.0f;
                        for (uint32_t s = 0; s < kSpecularSampleCount; ++s) {
                            const glm::vec3 half = importanceSampleGgx(
                                hammersley(s, kSpecularSampleCount), alpha, normal);
                            const glm::vec3 light = 2.0f * glm::dot(normal, half) * half - normal;
                            const float nol = glm::dot(normal, light);
                            if (nol <= 0.0f) {
                                continue;
                            }
                            sum += sampleCubeNearest(env, light) * nol;
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

//======================================================================================================================
rhi::Result<IblTextures> generate(rhi::Device& device, const CpuCubemap& env,
                                  std::string_view label) {
    LMX_ASSERT(env.faceSize > 0, "ibl::generate: the environment cube has no texels");
    IblTextures textures;

    const CpuCubemap irradiance = computeIrradiance(env, kIrradianceFaceSize);
    const std::array<CpuCubemap, 1> irradianceChain = {irradiance};
    auto irradianceTexture =
        uploadCube(device, irradianceChain, std::string(label) + ".irradiance");
    if (!irradianceTexture) {
        return std::unexpected(std::move(irradianceTexture.error()));
    }
    textures.irradiance = std::move(*irradianceTexture);

    const std::vector<CpuCubemap> prefiltered =
        prefilterSpecular(env, kSpecularBaseFaceSize, kSpecularMipCount);
    auto prefilteredTexture =
        uploadCube(device, prefiltered, std::string(label) + ".prefilteredEnv");
    if (!prefilteredTexture) {
        return std::unexpected(std::move(prefilteredTexture.error()));
    }
    textures.prefilteredEnv = std::move(*prefilteredTexture);

    // The DFG table depends on nothing but its own size, so every scene in a process shares one
    // integration of it.
    static const std::vector<glm::vec2> lut = computeDfgLut(kDfgLutSize);
    auto lutTexture = uploadDfgLut(device, lut, kDfgLutSize, std::string(label) + ".dfgLut");
    if (!lutTexture) {
        return std::unexpected(std::move(lutTexture.error()));
    }
    textures.dfgLut = std::move(*lutTexture);

    return textures;
}

} // namespace lmx::engine::ibl
