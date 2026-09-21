//----------------------------------------------------------------------------------------------------------------------
/// @file IblUpload.h
/// @brief Declares GPU uploads for CPU-generated environment lighting.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Asset/Ibl.h"
#include <rojoRHI/RHI.h>

#include <memory>
#include <string_view>

namespace lmx::scene::ibl {

/// The uploaded set a Scene holds. Cube textures are RGBA16Float (linear radiance above 1.0 must
/// survive), the DFG table is RG16Float.
struct IblTextures {
    std::unique_ptr<rojoRHI::Texture> irradiance;     ///< Diffuse irradiance cubemap.
    std::unique_ptr<rojoRHI::Texture> prefilteredEnv; ///< GGX-prefiltered environment chain.
    std::unique_ptr<rojoRHI::Texture> dfgLut;         ///< Split-sum DFG lookup table.
};

/// Uploads a single-level linear-light cubemap as an RGBA16Float sampled texture. Every component
/// must be finite and representable by binary16. `label` is copied into the GPU object's debug
/// label; `env` remains owned by the caller and need only stay alive for this call.
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
uploadCubemap(rojoRHI::Device& device, const asset::ibl::CpuCubemap& env, std::string_view label);

/// Generation quality may vary with the source without changing the shader's roughness levels.
struct GenerationOptions {
    /// Base extent must fit kSpecularMipCount mip levels (at least 16 texels for five levels).
    uint32_t specularBaseFaceSize = asset::ibl::kSpecularBaseFaceSize;
    /// Optional lower-resolution copy of the same radiance for the exact diffuse convolution.
    /// Borrowed only for generate(); null uses the main environment for both integrals.
    const asset::ibl::CpuCubemap* irradianceSource = nullptr;
};

/// Generates all three assets from `env` and uploads them. `label` is the scene name; the textures
/// are labelled "<label>.irradiance", "<label>.prefilteredEnv" and "<label>.dfgLut", alongside the
/// scene's "<label>.sky".
rojoRHI::Result<IblTextures> generate(rojoRHI::Device& device, const asset::ibl::CpuCubemap& env,
                                      std::string_view label, GenerationOptions options = {});

} // namespace lmx::scene::ibl
