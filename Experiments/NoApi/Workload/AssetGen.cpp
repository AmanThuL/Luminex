//----------------------------------------------------------------------------------------------------------------------
/// @file AssetGen.cpp
/// @brief Defines deterministic synthetic asset generation.
//----------------------------------------------------------------------------------------------------------------------

#include "Workload/AssetGen.h"
#include "Workload/ProdValues.h"
#include "Workload/Splitmix64.h"

#include <algorithm>
#include <cstring>

namespace lmx::noapi::workload {

namespace {

// Domain tags separate one generator's draws from another's so no two generators can accidentally
// agree on an index tuple; none of the frozen algorithm's own indices (material, slot, face, mip,
// texel) ever collide with these.
constexpr uint64_t kIrradianceTag = 1000;
constexpr uint64_t kPrefilteredTag = 1001;
constexpr uint64_t kDfgTag = 1002;

//======================================================================================================================
// Minimal float32 -> IEEE-754 binary16 conversion. Adequate for synthetic data because every
// generator below only ever feeds it small, finite, non-negative magnitudes -- it flushes tiny
// values to zero and clamps large ones rather than handling subnormals or infinities correctly.
uint16_t floatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    const uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

//======================================================================================================================
// A plausible half-float radiance value in [0, 2) from a splitmix64 draw.
uint16_t radianceHalf(uint64_t draw) {
    return floatToHalf(unitFloat(draw) * 2.0f);
}

//======================================================================================================================
MipLevel generateBaseLevel(uint32_t material, MaterialTextureSlot slot) {
    MipLevel level{.width = kMaterialTextureSize, .height = kMaterialTextureSize};
    level.rgba8.resize(static_cast<size_t>(level.width) * level.height * 4);
    for (uint32_t y = 0; y < level.height; ++y) {
        for (uint32_t x = 0; x < level.width; ++x) {
            const uint32_t texel = y * level.width + x;
            const uint64_t draw = splitmix64(kSeed, {material, static_cast<uint64_t>(slot), texel});
            uint8_t bytes[4];
            unitRgba8(draw, bytes);
            const size_t index = (static_cast<size_t>(y) * level.width + x) * 4;
            level.rgba8[index + 0] = bytes[0];
            level.rgba8[index + 1] = bytes[1];
            level.rgba8[index + 2] = bytes[2];
            level.rgba8[index + 3] = bytes[3];
        }
    }
    return level;
}

//======================================================================================================================
// Exact 2x2 box filter for a power-of-two source (every material texture level here is
// power-of-two down to 1x1, so no odd-extent edge case ever arises).
MipLevel boxFilterDown(const MipLevel& src) {
    MipLevel dst{.width = std::max(src.width / 2, 1u), .height = std::max(src.height / 2, 1u)};
    dst.rgba8.resize(static_cast<size_t>(dst.width) * dst.height * 4);
    for (uint32_t y = 0; y < dst.height; ++y) {
        for (uint32_t x = 0; x < dst.width; ++x) {
            uint32_t sum[4] = {0, 0, 0, 0};
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = std::min(x * 2 + dx, src.width - 1);
                    const uint32_t sy = std::min(y * 2 + dy, src.height - 1);
                    const size_t srcIndex = (static_cast<size_t>(sy) * src.width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += src.rgba8[srcIndex + c];
                    }
                }
            }
            const size_t dstIndex = (static_cast<size_t>(y) * dst.width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst.rgba8[dstIndex + c] = static_cast<uint8_t>((sum[c] + 2) / 4);
            }
        }
    }
    return dst;
}

} // namespace

//======================================================================================================================
GeneratedTexture generateMaterialTexture(uint32_t material, MaterialTextureSlot slot) {
    GeneratedTexture texture;
    texture.mips.push_back(generateBaseLevel(material, slot));
    while (texture.mips.back().width > 1 || texture.mips.back().height > 1) {
        texture.mips.push_back(boxFilterDown(texture.mips.back()));
    }
    return texture;
}

namespace {
//======================================================================================================================
SyntheticCubemap generateSyntheticCube(uint32_t baseFaceSize, uint32_t mipCount, uint64_t tag) {
    SyntheticCubemap cube{.faceSize = baseFaceSize};
    cube.mips.resize(mipCount);
    for (uint32_t mipLevel = 0; mipLevel < mipCount; ++mipLevel) {
        const uint32_t faceSize = std::max(baseFaceSize >> mipLevel, 1u);
        for (uint32_t face = 0; face < 6; ++face) {
            CubeFace& out = cube.mips[mipLevel][face];
            out.texelsRgba16.resize(static_cast<size_t>(faceSize) * faceSize * 4);
            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const uint64_t draw = splitmix64(kSeed, {tag, face, mipLevel, x, y});
                    const size_t index = (static_cast<size_t>(y) * faceSize + x) * 4;
                    out.texelsRgba16[index + 0] = radianceHalf(draw);
                    out.texelsRgba16[index + 1] = radianceHalf(draw + 1);
                    out.texelsRgba16[index + 2] = radianceHalf(draw + 2);
                    out.texelsRgba16[index + 3] = floatToHalf(1.0f);
                }
            }
        }
    }
    return cube;
}
} // namespace

//======================================================================================================================
SyntheticCubemap generateSyntheticIrradiance() {
    return generateSyntheticCube(prod::kIrradianceFaceSize, 1, kIrradianceTag);
}

//======================================================================================================================
SyntheticCubemap generateSyntheticPrefilteredEnv() {
    return generateSyntheticCube(prod::kPrefilteredBaseFaceSize, prod::kPrefilteredMipCount,
                                 kPrefilteredTag);
}

//======================================================================================================================
DfgLut generateSyntheticDfgLut() {
    DfgLut lut{.size = prod::kDfgLutSize};
    lut.texelsRg16.resize(static_cast<size_t>(lut.size) * lut.size);
    for (uint32_t y = 0; y < lut.size; ++y) {
        for (uint32_t x = 0; x < lut.size; ++x) {
            const uint64_t draw = splitmix64(kSeed, {kDfgTag, x, y});
            lut.texelsRg16[static_cast<size_t>(y) * lut.size + x] = {
                floatToHalf(unitFloat(draw)), floatToHalf(unitFloat(draw + 1))};
        }
    }
    return lut;
}

//======================================================================================================================
QuadGeometry sharedQuad() {
    constexpr float kHalfExtent = 0.5f;
    QuadGeometry quad;
    // +Y normal, CCW winding when viewed from above, matching makePlane's convention
    // (Source/Render/Mesh.cpp's addFace: "cross(u, v) == normal gives every generated face an
    // outward CCW winding") restated here without a Render dependency. The index order below is
    // what actually delivers that winding for these four corners: cross(v2 - v0, v1 - v0) and
    // cross(v3 - v0, v2 - v0) both work out to +Y, matching addFace's own convention exactly.
    quad.vertices = {
        QuadVertex{.px = -kHalfExtent,
                   .py = 0.0f,
                   .pz = -kHalfExtent,
                   .nx = 0,
                   .ny = 1,
                   .nz = 0,
                   .tx = 1,
                   .ty = 0,
                   .tz = 0,
                   .tw = 1,
                   .u = 0,
                   .v = 0},
        QuadVertex{.px = kHalfExtent,
                   .py = 0.0f,
                   .pz = -kHalfExtent,
                   .nx = 0,
                   .ny = 1,
                   .nz = 0,
                   .tx = 1,
                   .ty = 0,
                   .tz = 0,
                   .tw = 1,
                   .u = 1,
                   .v = 0},
        QuadVertex{.px = kHalfExtent,
                   .py = 0.0f,
                   .pz = kHalfExtent,
                   .nx = 0,
                   .ny = 1,
                   .nz = 0,
                   .tx = 1,
                   .ty = 0,
                   .tz = 0,
                   .tw = 1,
                   .u = 1,
                   .v = 1},
        QuadVertex{.px = -kHalfExtent,
                   .py = 0.0f,
                   .pz = kHalfExtent,
                   .nx = 0,
                   .ny = 1,
                   .nz = 0,
                   .tx = 1,
                   .ty = 0,
                   .tz = 0,
                   .tw = 1,
                   .u = 0,
                   .v = 1},
    };
    quad.indices = {0, 2, 1, 0, 3, 2};
    return quad;
}

} // namespace lmx::noapi::workload
