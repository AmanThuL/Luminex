//----------------------------------------------------------------------------------------------------------------------
/// @file StageSetup.h
/// @brief Shares fullscreen pipeline, texel and compute-pipeline creation mechanics.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
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
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
createTexel(rojoRHI::Device& device, rojoRHI::Format format, rojoRHI::TextureKind kind,
            std::span<const std::byte> bytes, std::string_view label);

// Libraries outlive the pipelines that were created from them, as in each stage owner.
struct ComputePipelines {
    std::vector<std::unique_ptr<rojoRHI::ShaderLibrary>> libraries;
    std::vector<std::unique_ptr<rojoRHI::ComputePipeline>> pipelines;
};

// Creates each named library followed by its pipeline, in the caller's order.
rojoRHI::Result<ComputePipelines> createComputePipelines(rojoRHI::Device& device,
                                                         std::span<const char* const> names,
                                                         std::string_view labelPrefix,
                                                         uint32_t (*threadsFor)(uint32_t));

} // namespace lmx::render
