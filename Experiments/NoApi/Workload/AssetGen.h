//----------------------------------------------------------------------------------------------------------------------
/// @file AssetGen.h
/// @brief Declares deterministic synthetic asset generation for the M5.1 workload: material
///        textures, IBL content, and the shared quad (spec section 6).
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Workload/RepresentativeGraph.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lmx::noapi::workload {

/// The five material texture slots the representative graph binds, named by their ScenePass slot
/// numbers (spec section 6: "t0 base color sRGB, t1 normal, t4 metallic-roughness, t5 occlusion,
/// t6 emissive sRGB").
enum class MaterialTextureSlot : uint32_t {
    BaseColor = 0,
    Normal = 1,
    MetallicRoughness = 4,
    Occlusion = 5,
    Emissive = 6,
};

/// One generated mip level: `width * height` RGBA8 texels, tightly packed, row-major.
struct MipLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba8;
};

/// A full mip chain, level 0 first.
struct GeneratedTexture {
    std::vector<MipLevel> mips;
};

/// Generates one material's texture for one slot: a kMaterialTextureSize^2 base level whose every
/// texel is an independent splitmix64(kSeed, {material, slot, texelIndex}) draw (spec section 6),
/// followed by a full 2x2 box-filtered mip chain down to 1x1. `material` is in
/// [0, kMaterialCount); the box filter runs in the texture's own encoded byte space (no colour
/// conversion), matching the offline bake's colour/data channel handling for non-sRGB roles and
/// approximating it for sRGB roles -- exact sRGB-correct filtering is out of scope for a synthetic
/// asset whose only job is to exercise binding traffic and barriers, not to look correct.
GeneratedTexture generateMaterialTexture(uint32_t material, MaterialTextureSlot slot);

/// One synthetic cubemap face: `faceSize * faceSize` RGBA16 half-float texels (as raw 16-bit
/// words; a caller reinterprets per its own half-float representation), row-major.
struct CubeFace {
    std::vector<uint16_t> texelsRgba16;
};

/// A synthetic cubemap: kCubeFaceCount faces, one CubeFace per mip level (level 0 first).
struct SyntheticCubemap {
    uint32_t faceSize = 0;
    std::vector<std::array<CubeFace, 6>> mips;
};

/// Generates a synthetic irradiance cube (prod::kIrradianceFaceSize, one mip) or prefiltered
/// environment chain (prod::kPrefilteredBaseFaceSize, prod::kPrefilteredMipCount mips), every
/// texel an independent splitmix64(kSeed, {"irradiance"|"prefiltered", face, mip, x, y}) draw
/// mapped into a plausible half-float radiance range. Deterministic content standing in for a real
/// convolution (spec section 6: "deterministic synthetic content from the same generator") -- the
/// representative graph never samples these, so physical correctness is not a requirement.
SyntheticCubemap generateSyntheticIrradiance();
SyntheticCubemap generateSyntheticPrefilteredEnv();

/// Generates the synthetic DFG LUT: prod::kDfgLutSize^2 RG16 half-float texels (raw 16-bit words),
/// each an independent splitmix64(kSeed, {"dfg", x, y}) draw.
struct DfgLut {
    uint32_t size = 0;
    std::vector<std::array<uint16_t, 2>> texelsRg16;
};
DfgLut generateSyntheticDfgLut();

/// The vertex layout the shared quad is generated in. Mirrors `lmx::render::Vertex`
/// (Source/Render/Mesh.h): 48-byte packed position/normal/tangent4/uv, restated here without an
/// RHI or Render dependency since this library is API-neutral.
struct QuadVertex {
    float px = 0, py = 0, pz = 0;         ///< Object-space position.
    float nx = 0, ny = 0, nz = 0;         ///< Object-space unit normal.
    float tx = 0, ty = 0, tz = 0, tw = 0; ///< xyz tangent, w handedness.
    float u = 0, v = 0;                    ///< Primary texture coordinates.
};
static_assert(sizeof(QuadVertex) == 48, "must match the mirrored production vertex stride");

/// The shared quad every draw instances (spec section 6: "one shared indexed quad (4 vertices ...
/// 6 indices)"): a unit XZ-plane quad, +Y normal, CCW winding, generated once at init.
struct QuadGeometry {
    std::array<QuadVertex, 4> vertices;
    std::array<uint32_t, 6> indices;
};
QuadGeometry sharedQuad();

} // namespace lmx::noapi::workload
