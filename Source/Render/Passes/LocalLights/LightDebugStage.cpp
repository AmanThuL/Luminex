//----------------------------------------------------------------------------------------------------------------------
/// @file LightDebugStage.cpp
/// @brief Binds post-display light diagnostics to the actual depth, light grid and index list.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/LocalLights/LightDebugStage.h"
#include "Core/Diagnostics/Assert.h"

namespace lmx::render {
namespace {
// Scalar storage matches LightDebugView.slang's constant block, including the scalar depth array.
struct DebugParams {
    glm::mat4 inverseViewProjection;
    uint32_t mode, rowCount, activeWidth, activeHeight, outputWidth, outputHeight;
    std::array<float, kClusterSliceBoundaryCount> sliceDepth;
};
static_assert(offsetof(DebugParams, sliceDepth) == 88);
static_assert(sizeof(DebugParams) == 188);
} // namespace

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<LightDebugStage>> LightDebugStage::create(rojoRHI::Device& device) {
    auto stage = std::unique_ptr<LightDebugStage>(new LightDebugStage);
    auto library = device.loadShaderLibrary("Shaders/LightDebugView");
    if (!library)
        return std::unexpected(library.error());
    stage->m_library = std::move(*library);
    auto pipeline = device.createGraphicsPipeline({.library = stage->m_library.get(),
                                                   .vertexEntry = "vertexMain",
                                                   .fragmentEntry = "fragmentMain",
                                                   .colorFormat = rojoRHI::Format::BGRA8Unorm,
                                                   .cullMode = rojoRHI::CullMode::None,
                                                   .label = "lmx.render.lightDebugPipeline"});
    if (!pipeline)
        return std::unexpected(pipeline.error());
    stage->m_pipeline = std::move(*pipeline);
    return stage;
}

//======================================================================================================================
GraphTexture LightDebugStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                      const LightDebugInputs& inputs) {
    LMX_ASSERT(inputs.mode != engine::LightDebugView::Off,
               "a light debug pass needs an active view");
    LMX_ASSERT(inputs.clusters.activeWidth && inputs.clusters.activeHeight && inputs.outputWidth &&
                   inputs.outputHeight,
               "light debug extents must be nonzero");
    const DebugParams params{inputs.inverseViewProjection, uint32_t(inputs.mode),
                             inputs.clusters.rowCount,     inputs.clusters.activeWidth,
                             inputs.clusters.activeHeight, inputs.outputWidth,
                             inputs.outputHeight,          inputs.clusters.sliceDepth};
    PassDesc pass;
    pass.color = ColorAttachment{.handle = inputs.output};
    pass.textureReads = {inputs.depth, inputs.display};
    pass.bufferReads = {inputs.lights, inputs.grid, inputs.indices};
    graph.addPass("lmx.pass.light.debug", std::move(pass),
                  [this, &commands, inputs, params](const PassResources& resources) {
                      commands.bindPipeline(*m_pipeline);
                      commands.bindFrameData(0, params);
                      commands.bindBuffer(1, **resources.buffer(inputs.lights));
                      commands.bindBuffer(2, **resources.buffer(inputs.grid));
                      commands.bindBuffer(3, **resources.buffer(inputs.indices));
                      commands.bindTexture(0, **resources.texture(inputs.depth));
                      commands.bindTexture(1, **resources.texture(inputs.display));
                      commands.draw(3);
                  });
    return nextVersion(inputs.output);
}
} // namespace lmx::render
