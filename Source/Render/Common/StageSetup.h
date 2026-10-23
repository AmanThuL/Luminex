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

// Existing fullscreen-triangle entry with no depth attachment or face culling.
rojoRHI::GraphicsPipelineDesc fullscreenPipelineDesc(rojoRHI::ShaderLibrary* library,
                                                     std::string_view fragmentEntry,
                                                     rojoRHI::Format colorFormat,
                                                     std::string_view label);

// Initializes one two-dimensional texel or all six cube faces from the same source bytes.
lmx::Result<engine::GpuTexture> createTexel(rojoRHI::Device& device, rojoRHI::Format format,
                                            rojoRHI::TextureKind kind,
                                            std::span<const std::byte> bytes,
                                            std::string_view label);

// Libraries outlive the pipelines that were created from them, as in each stage owner.
struct ComputePipelines {
    std::vector<engine::GpuOwned<rojoRHI::ShaderLibrary>> libraries;
    std::vector<engine::GpuOwned<rojoRHI::ComputePipeline>> pipelines;
};

// Creates each named library followed by its pipeline, in the caller's order.
lmx::Result<ComputePipelines> createComputePipelines(engine::GpuContext& gpu,
                                                     std::span<const char* const> names,
                                                     std::string_view labelPrefix,
                                                     uint32_t (*threadsFor)(uint32_t));

} // namespace lmx::render
