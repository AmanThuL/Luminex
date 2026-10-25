//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.cpp
/// @brief Creates neutral frame resources for unavailable temporal and exposure stages.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererStandIns.h"

#include <array>
#include <utility>

namespace lmx::render {

//======================================================================================================================
lmx::Result<RendererStandIns> createRendererStandIns(rojoRHI::Device& device, uint32_t width,
                                                     uint32_t height) {
    auto depth = engine::GpuTexture::create(
        device, {.format = rojoRHI::Format::D32Float,
                 .extent = {width, height, 1},
                 .usage = rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled,
                 .label = "lmx.render.standInDepth"});
    if (!depth) {
        return std::unexpected(depth.error());
    }
    constexpr std::array<float, 2> exposureValues{1.0f, 1.0f};
    auto exposure = engine::GpuBuffer::create(device,
                                              {.size = sizeof(exposureValues),
                                               .memory = rojoRHI::Memory::Gpu,
                                               .label = "lmx.render.standInExposure"},
                                              std::as_bytes(std::span{exposureValues}));
    if (!exposure) {
        return std::unexpected(exposure.error());
    }
    return RendererStandIns{.depth = std::move(*depth), .exposure = std::move(*exposure)};
}

} // namespace lmx::render
