//----------------------------------------------------------------------------------------------------------------------
/// @file RendererStandIns.cpp
/// @brief Creates neutral frame resources and declares a scene-linear clear.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/RendererStandIns.h"

#include "Core/Math/Color.h"

#include <array>
#include <utility>

namespace lmx::render {

//======================================================================================================================
GraphTexture declareSceneClear(RenderGraph& graph, GraphTexture sceneColor, GraphTexture depth,
                               const float clearColor[4]) {
    const glm::vec3 clearLinear =
        lmx::srgbToLinear(glm::vec3(clearColor[0], clearColor[1], clearColor[2]));
    PassDesc pass;
    pass.color =
        ColorAttachment{.handle = sceneColor,
                        .load = LoadOp::Clear,
                        .store = StoreOp::Store,
                        .clearColor = {clearLinear.r, clearLinear.g, clearLinear.b, clearColor[3]}};
    pass.depth = DepthAttachment{
        .handle = depth, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    graph.addPass("lmx.pass.sceneClear", std::move(pass), [](const PassResources&) {});
    return nextVersion(sceneColor);
}

//======================================================================================================================
lmx::Result<RendererStandIns> createRendererStandIns(rojoRHI::Device& device, uint32_t width,
                                                     uint32_t height) {
    auto depth = engine::GpuTexture::create(device, {.width = width,
                                                     .height = height,
                                                     .format = rojoRHI::Format::D32Float,
                                                     .renderTarget = true,
                                                     .sampled = true,
                                                     .label = "lmx.render.standInDepth"});
    if (!depth) {
        return std::unexpected(depth.error());
    }
    constexpr std::array<float, 2> exposureValues{1.0f, 1.0f};
    auto exposure = engine::GpuBuffer::create(device,
                                              {.size = sizeof(exposureValues),
                                               .storageRead = true,
                                               .label = "lmx.render.standInExposure"},
                                              exposureValues.data());
    if (!exposure) {
        return std::unexpected(exposure.error());
    }
    return RendererStandIns{.depth = std::move(*depth), .exposure = std::move(*exposure)};
}

} // namespace lmx::render
