//----------------------------------------------------------------------------------------------------------------------
/// @file Ibl.h
/// @brief Declares CPU cubemap, IBL generation, and GPU upload helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "RHI/RHI.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

/// Deterministic image-based-lighting asset generation, entirely on the CPU: a cosine-convolved
/// irradiance cube, a GGX-prefiltered specular chain, and the split-sum DFG lookup table
/// (Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013).
///
/// Every generator here is a pure function driven by the Hammersley sequence rather than an RNG,
/// runs single-threaded in a fixed face-major/scanline order, and accumulates in float32. Two runs
/// of the same build therefore produce byte-identical output, which is what lets an IBL set be
/// compared with memcmp instead of a tolerance.
namespace lmx::engine::ibl {

inline constexpr uint32_t kCubeFaceCount = 6; ///< Number of slices in an RHI cubemap.

/// Output extents. Diffuse irradiance is a very low-frequency signal, so 16x16 faces carry it
/// without visible error; the specular chain trades face size for roughness across five mips.
inline constexpr uint32_t kIrradianceFaceSize = 16;   ///< Shipped diffuse cube face extent.
inline constexpr uint32_t kSpecularBaseFaceSize = 64; ///< Shipped specular base face extent.
inline constexpr uint32_t kSpecularMipCount = 5;      ///< Shipped specular roughness levels.
inline constexpr uint32_t kDfgLutSize = 64;           ///< Shipped DFG table extent.

/// Fixed sample budgets. Named rather than tuned per call: the count is part of what makes a
/// generated asset reproducible, so changing one changes every output byte.
inline constexpr uint32_t kSpecularSampleCount = 1024; ///< Samples per prefilter texel.
inline constexpr uint32_t kDfgSampleCount = 1024;      ///< Samples per DFG texel.

/// Linear-light cube-map faces the generators read and write.
///
/// Face order is the RHI's cube order -- +X, -X, +Y, -Y, +Z, -Z (RHI.h's TextureKind::Cube) -- so
/// index i here is the slice createTexture uploads face i to.
///
/// Within a face, texel (x, y) of a faceSize x faceSize row-major image maps to the direction every
/// graphics API's cube-map convention agrees on. With
///     u = 2 * (x + 0.5) / faceSize - 1,   v = 2 * (y + 0.5) / faceSize - 1
/// the direction (before normalization) is
///     +X: ( 1, -v, -u)      -X: (-1, -v,  u)
///     +Y: ( u,  1,  v)      -Y: ( u, -1, -v)
///     +Z: ( u, -v,  1)      -Z: (-u, -v, -1)
/// x therefore runs left-to-right and y top-to-bottom within a face, which is the row order
/// createTexture's upload expects. A direction that Shaders/Sky.slang samples the scene's sky cube
/// with lands on the same texel these generators read, so an irradiance cube built here is oriented
/// like the environment it came from rather than mirrored or rotated off it.
struct CpuCubemap {
    uint32_t faceSize = 0;                                    ///< Square face extent in texels.
    std::array<std::vector<glm::vec4>, kCubeFaceCount> faces; ///< Linear RGBA face texels.
};

/// The unit direction texel (x, y) of `face` looks along, per the convention documented above.
glm::vec3 faceDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t faceSize);

/// A cube whose every texel carries the same linear radiance -- the analytic environment the
/// generators' energy-conservation properties are stated against, and the input a scene with a
/// single authored sky colour supplies.
CpuCubemap makeConstantCubemap(const glm::vec3& radiance, uint32_t faceSize);

/// Cosine-weighted convolution of `env`, stored as irradiance divided by pi: the value a Lambert
/// surface multiplies its albedo by directly. Exact summation over every source texel with
/// solid-angle weights, not importance sampling, and normalized by the accumulated weight -- so a
/// constant environment convolves back to exactly its own radiance rather than to within a
/// discretization error.
///
/// Cost is O(outFaceSize^2 * env.faceSize^2): every output texel visits every source texel. That is
/// deliberate for the input sizes this project generates from -- an authored constant sky (faceSize
/// 1) or a small captured environment (faceSize <= 32) -- and would need an importance-sampled or
/// spherical-harmonic path before a full-resolution captured cube could feed it.
CpuCubemap computeIrradiance(const CpuCubemap& env, uint32_t outFaceSize);

/// GGX-prefiltered radiance chain, one CpuCubemap per mip, face size halving per level with a floor
/// of 1. The caller chooses how many levels the chain carries (the shipped 64px, five-level chain
/// ends at 4px). Mip m covers perceptual roughness m / (mipCount - 1) with alpha = roughness^2, so
/// mip 0 is a mirror resample of `env` and the last mip is roughness 1. Karis's split-sum
/// prefilter: the normal, view and reflection vectors are all assumed equal to the texel direction,
/// GGX importance sampling draws kSpecularSampleCount half-vectors from the Hammersley sequence,
/// and the samples are averaged with N.L weights. The weights normalize, so a constant environment
/// prefilters to itself at every roughness.
std::vector<CpuCubemap> prefilterSpecular(const CpuCubemap& env, uint32_t baseFaceSize,
                                          uint32_t mipCount);

/// The split-sum DFG term: `size` x `size` row-major (scale, bias) pairs that reconstruct a
/// specular response as F0 * scale + bias. Height-correlated Smith visibility with alpha =
/// roughness^2, matching the direct-lighting BRDF, and kDfgSampleCount GGX importance samples.
///
/// Both axes are texel-centered, so texel (x, y) stands for
///     N.V                 = (x + 0.5) / size
///     perceptual roughness = (y + 0.5) / size
/// and a shader reads it with a clamped linear sample at float2(NoV, roughness). Neither axis ever
/// reaches exactly 0 or 1: the endpoints are half a texel inside the domain, which is what keeps a
/// linear sample at NoV = 1 from interpolating past the last computed column.
std::vector<glm::vec2> computeDfgLut(uint32_t size);

/// The uploaded set a Scene holds. Cube textures are RGBA16Float (linear radiance above 1.0 must
/// survive), the DFG table is RG16Float.
struct IblTextures {
    std::unique_ptr<rhi::Texture> irradiance;     ///< Diffuse irradiance cubemap.
    std::unique_ptr<rhi::Texture> prefilteredEnv; ///< GGX-prefiltered environment chain.
    std::unique_ptr<rhi::Texture> dfgLut;         ///< Split-sum DFG lookup table.
};

/// Uploads a single-level linear-light cubemap as an RGBA16Float sampled texture. Every component
/// must be finite and representable by binary16. `label` is copied into the GPU object's debug
/// label; `env` remains owned by the caller and need only stay alive for this call.
rhi::Result<std::unique_ptr<rhi::Texture>> uploadCubemap(rhi::Device& device, const CpuCubemap& env,
                                                         std::string_view label);

/// Generates all three assets from `env` and uploads them. `label` is the scene name; the textures
/// are labelled "<label>.irradiance", "<label>.prefilteredEnv" and "<label>.dfgLut", alongside the
/// scene's "<label>.sky".
rhi::Result<IblTextures> generate(rhi::Device& device, const CpuCubemap& env,
                                  std::string_view label);

} // namespace lmx::engine::ibl
