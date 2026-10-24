//----------------------------------------------------------------------------------------------------------------------
/// @file StageSetup.h
/// @brief Shares fullscreen pipeline, texel and compute-pipeline creation mechanics.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/Gpu/GpuTexture.h"

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace lmx::render {

/// Creates a fullscreen-triangle pipeline with no depth attachment or face culling.
/// The library must outlive the returned pipeline; creation failures retain their original detail.
lmx::Result<engine::GpuOwned<rojoRHI::Pipeline>>
createFullscreenPipeline(rojoRHI::Device device, rojoRHI::ShaderLibrary library,
                         std::string_view fragmentEntry, rojoRHI::Format colorFormat,
                         std::string_view label);

/// Initializes one two-dimensional texel or all six cube faces from the same source bytes.
lmx::Result<engine::GpuTexture> createTexel(rojoRHI::Device& device, rojoRHI::Format format,
                                            rojoRHI::TextureType kind,
                                            std::span<const std::byte> bytes,
                                            std::string_view label);

/// Keeps libraries alive until after the pipelines created from them are released.
struct ComputePipelines {
    std::vector<engine::GpuOwned<rojoRHI::ShaderLibrary>>
        libraries; ///< Shader libraries in caller order.
    std::vector<engine::GpuOwned<rojoRHI::Pipeline>>
        pipelines; ///< Corresponding compute pipelines.
};

/// Creates each named library followed by its pipeline, in the caller's order.
lmx::Result<ComputePipelines> createComputePipelines(engine::GpuContext& gpu,
                                                     std::span<const char* const> names,
                                                     std::string_view labelPrefix);

} // namespace lmx::render
