//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.cpp
/// @brief Creates neutral depth while the temporal stage is unavailable.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererStandIns.h"

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
    return RendererStandIns{.depth = std::move(*depth)};
}

} // namespace lmx::render
