//----------------------------------------------------------------------------------------------------------------------
/// @file StageSetup.cpp
/// @brief Creates labelled stage resources without changing caller policy or creation order.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/StageSetup.h"

#include "Engine/Gpu/GpuOwned.h"

#include <array>
#include <string>
#include <utility>

namespace lmx::render {

//======================================================================================================================
lmx::Result<engine::GpuOwned<rojoRHI::Pipeline>>
createFullscreenPipeline(rojoRHI::Device device, rojoRHI::ShaderLibrary library,
                         std::string_view fragmentEntry, rojoRHI::Format colorFormat,
                         std::string_view label) {
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = colorFormat}}};
    return engine::adoptGpu(
        device, device.createPipeline({.vertex = {library, "vertexMain"},
                                       .fragment = {library, fragmentEntry},
                                       .raster = {.cull = rojoRHI::CullMode::None},
                                       .depthStencil = {.depthTest = false,
                                                        .depthWrite = false,
                                                        .depthCompare = rojoRHI::CompareOp::Always},
                                       .colors = colors,
                                       .label = label}));
}

//======================================================================================================================
lmx::Result<engine::GpuTexture> createTexel(rojoRHI::Device& device, rojoRHI::Format format,
                                            rojoRHI::TextureType kind,
                                            std::span<const std::byte> bytes,
                                            std::string_view label) {
    const rojoRHI::TextureData mip{.bytes = bytes, .bytesPerRow = bytes.size()};
    const std::array<rojoRHI::TextureData, 6> mips{mip, mip, mip, mip, mip, mip};
    const uint32_t faces = kind == rojoRHI::TextureType::Cube ? 6u : 1u;
    return engine::GpuTexture::create(device,
                                      {.type = kind,
                                       .format = format,
                                       .extent = {1, 1, 1},
                                       .layers = faces,
                                       .usage = rojoRHI::TextureUsage::Sampled,
                                       .label = label},
                                      std::span{mips.data(), faces});
}

//======================================================================================================================
lmx::Result<ComputePipelines> createComputePipelines(engine::GpuContext& gpu,
                                                     std::span<const char* const> names,
                                                     std::string_view labelPrefix) {
    ComputePipelines result;
    result.libraries.reserve(names.size());
    result.pipelines.reserve(names.size());
    for (uint32_t index = 0; index < names.size(); ++index) {
        auto library = gpu.loadShaderLibrary(names[index]);
        if (!library) {
            return std::unexpected(library.error());
        }
        result.libraries.push_back(std::move(*library));
        auto pipeline = engine::adoptGpu(
            gpu.device(), gpu.device().createPipeline(rojoRHI::ComputePipelineDesc{
                              .compute = {result.libraries.back().get(), "computeMain"},
                              .label = std::string(labelPrefix) + names[index]}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        result.pipelines.push_back(std::move(*pipeline));
    }
    return result;
}

} // namespace lmx::render
