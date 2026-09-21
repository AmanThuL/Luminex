//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutline.cpp
/// @brief Implements visible selection coverage and a separate SDR outline composite.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/SelectionOutline/SelectionOutline.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/View/Camera.h"
#include "Render/Common/Formats.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {
// Matches SelectionMask.slang. Coverage is non-color data; all scene transforms remain borrowed.
struct MaskParams {
    glm::mat4 viewProj;
    uint32_t instanceRow;
    float selected;
    uint32_t padding[2]{};
};
static_assert(sizeof(MaskParams) == 80);
static_assert(offsetof(MaskParams, instanceRow) == 64);
static_assert(offsetof(MaskParams, selected) == 68);

struct OutlineParams {
    float radius;
};
static_assert(sizeof(OutlineParams) == 4);
} // namespace

//======================================================================================================================
SelectionOutline::SelectionOutline(rojoRHI::Device& device, bool readback)
    : m_device(device), m_readback(readback) {}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<SelectionOutline>>
SelectionOutline::create(rojoRHI::Device& device, uint32_t width, uint32_t height, bool readback) {
    auto self = std::unique_ptr<SelectionOutline>(new SelectionOutline(device, readback));
    auto maskLibrary = device.loadShaderLibrary("Shaders/SelectionMask");
    if (!maskLibrary) {
        return std::unexpected(maskLibrary.error());
    }
    self->m_maskLibrary = std::move(*maskLibrary);
    auto outlineLibrary = device.loadShaderLibrary("Shaders/SelectionOutline");
    if (!outlineLibrary) {
        return std::unexpected(outlineLibrary.error());
    }
    self->m_outlineLibrary = std::move(*outlineLibrary);
    for (bool doubleSided : {false, true}) {
        auto pipeline = device.createGraphicsPipeline(
            {.library = self->m_maskLibrary.get(),
             .vertexEntry = "vertexMain",
             .fragmentEntry = "fragmentMain",
             .colorFormat = rojoRHI::Format::R8Unorm,
             .depthFormat = rojoRHI::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .cullMode = doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
             .depthCompare = rojoRHI::DepthCompare::Greater,
             .label = doubleSided ? "lmx.selection.maskDoubleSided" : "lmx.selection.mask"});
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        (doubleSided ? self->m_doubleSidedPipeline : self->m_maskPipeline) = std::move(*pipeline);
        auto depthPipeline = device.createGraphicsPipeline(
            {.library = self->m_maskLibrary.get(),
             .vertexEntry = "vertexMain",
             .fragmentEntry = "depthMain",
             .colorFormat = rojoRHI::Format::Unknown,
             .depthFormat = rojoRHI::Format::D32Float,
             .depthTestEnable = true,
             .depthWriteEnable = true,
             .cullMode = doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
             .depthCompare = rojoRHI::DepthCompare::Greater,
             .label = doubleSided ? "lmx.selection.depthDoubleSided" : "lmx.selection.depth"});
        if (!depthPipeline) {
            return std::unexpected(depthPipeline.error());
        }
        (doubleSided ? self->m_doubleSidedDepthPipeline : self->m_depthPipeline) =
            std::move(*depthPipeline);
    }
    auto outline = device.createGraphicsPipeline({.library = self->m_outlineLibrary.get(),
                                                  .vertexEntry = "vertexMain",
                                                  .fragmentEntry = "fragmentMain",
                                                  .colorFormat = kDisplayFormat,
                                                  .cullMode = rojoRHI::CullMode::None,
                                                  .label = "lmx.selection.outline"});
    if (!outline) {
        return std::unexpected(outline.error());
    }
    self->m_outlinePipeline = std::move(*outline);
    auto passthrough = device.createGraphicsPipeline({.library = self->m_outlineLibrary.get(),
                                                      .vertexEntry = "vertexMain",
                                                      .fragmentEntry = "fragmentCopy",
                                                      .colorFormat = kDisplayFormat,
                                                      .cullMode = rojoRHI::CullMode::None,
                                                      .label = "lmx.selection.passthrough"});
    if (!passthrough) {
        return std::unexpected(passthrough.error());
    }
    self->m_passthroughPipeline = std::move(*passthrough);
    auto sampler =
        device.createSampler({.maxAnisotropy = 16, .label = "lmx.selection.materialSampler"});
    if (!sampler) {
        return std::unexpected(sampler.error());
    }
    self->m_sampler = std::move(*sampler);
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    const rojoRHI::TextureMip mip{.data = white.data(), .bytesPerRow = 4};
    auto texture = device.createTexture({.width = 1,
                                         .height = 1,
                                         .format = rojoRHI::Format::RGBA8Unorm,
                                         .sampled = true,
                                         .label = "lmx.selection.white"},
                                        std::span{&mip, 1});
    if (!texture) {
        return std::unexpected(texture.error());
    }
    self->m_white = std::move(*texture);
    {
        auto result = self->resize(width, height);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return self;
}

//======================================================================================================================
rojoRHI::Result<void> SelectionOutline::resize(uint32_t width, uint32_t height) {
    auto target = m_device.createTexture({.width = width,
                                          .height = height,
                                          .format = kDisplayFormat,
                                          .renderTarget = true,
                                          .sampled = true,
                                          .cpuReadback = m_readback,
                                          .label = "lmx.selection.display"});
    if (!target) {
        return std::unexpected(target.error());
    }
    m_target = std::move(*target);
    return {};
}

//======================================================================================================================
GraphTexture SelectionOutline::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                       const SceneView& view,
                                       const SelectionOutlineInputs& inputs) {
    const auto display = inputs.display;
    const auto& camera = inputs.camera;
    const auto selectedDraw = inputs.selectedDraw;
    const auto backingScale = inputs.backingScale;
    const auto visible = inputs.visible;
    LMX_ASSERT(selectedDraw < view.items.size(), "selection must name a current draw");
    if (!visible) {
        const auto output = graph.importTexture(*m_target, kDisplayFormat, "selectionDisplay",
                                                rojoRHI::TextureUse::ShaderRead);
        PassDesc passthrough;
        passthrough.textureReads = {display};
        passthrough.color = ColorAttachment{.handle = output};
        // The UI has already chosen this target. Refresh it while keeping display's terminal
        // shader-read state consistent with both ordinary outline frames and the next import.
        graph.addPass("lmx.pass.selection.passthrough", std::move(passthrough),
                      [this, &commands, display](const PassResources& resources) {
                          const auto source = resources.texture(display);
                          LMX_ASSERT(source.has_value(),
                                     "selection display source must be declared");
                          commands.bindPipeline(*m_passthroughPipeline);
                          commands.bindTexture(0, **source);
                          commands.draw(3);
                      });
        return nextVersion(output);
    }
    const uint32_t width = m_target->width();
    const uint32_t height = m_target->height();
    const GraphTexture mask = graph.createTexture({.width = width,
                                                   .height = height,
                                                   .format = rojoRHI::Format::R8Unorm,
                                                   .renderTarget = true,
                                                   .sampled = true},
                                                  "selectionMask");
    const auto depthDescriptor = TransientTextureDesc{.width = width,
                                                      .height = height,
                                                      .format = rojoRHI::Format::D32Float,
                                                      .renderTarget = true,
                                                      .sampled = true};
    const GraphTexture depth = graph.createTexture(depthDescriptor, "selectionDepth");
    const GraphTexture sceneDepth = graph.createTexture(depthDescriptor, "selectionSceneDepth");
    // Both passes share unjittered rasterization. The selected silhouette stays intact behind
    // occluders; scene depth only gates its border during composite, never creates an edge.
    const glm::mat4 viewProjection =
        camera.projectionMatrix(static_cast<float>(width) / height) * camera.viewMatrix();
    const auto drawCoverage = [this, &commands, view, selectedDraw,
                               viewProjection](bool selectedOnly) {
        commands.bindSampler(0, *m_sampler);
        commands.bindBuffer(0, *view.tables.vertices);
        commands.bindBuffer(engine::kSceneInstancesSlot, *view.tables.instances);
        commands.bindBuffer(engine::kSceneMaterialsSlot, *view.tables.materials);
        for (uint32_t index = 0; index < view.items.size(); ++index) {
            const auto& item = view.items[index];
            if (selectedOnly && index != selectedDraw) {
                continue;
            }
            const bool masked = item.alphaMode == engine::AlphaMode::Mask;
            const bool doubleSided = masked && item.doubleSided;
            commands.bindPipeline(
                selectedOnly ? (doubleSided ? *m_doubleSidedPipeline : *m_maskPipeline)
                             : (doubleSided ? *m_doubleSidedDepthPipeline : *m_depthPipeline));
            commands.bindTexture(0, item.diffuse ? *item.diffuse : *m_white);
            const MaskParams params{
                .viewProj = viewProjection, .instanceRow = item.instanceRow, .selected = 1.0f};
            commands.bindFrameData(1, params);
            commands.drawIndexed(*view.tables.indices, item.mesh.indexCount, item.mesh.firstIndex);
        }
    };
    // Read-only aliases keep editor passes independent of Renderer's graph-local handles.
    const std::vector<GraphBuffer> sceneBuffers{
        graph.importBuffer(*view.tables.vertices, "lmx.selection.vertices"),
        graph.importBuffer(*view.tables.indices, "lmx.selection.indices"),
        graph.importBuffer(*view.tables.instances, "lmx.selection.instances"),
        graph.importBuffer(*view.tables.materials, "lmx.selection.materials")};
    PassDesc coverage;
    coverage.bufferReads = sceneBuffers;
    coverage.color = ColorAttachment{.handle = mask, .clearColor = {0, 0, 0, 0}};
    coverage.depth = DepthAttachment{.handle = depth, .store = StoreOp::Store};
    graph.addPass("lmx.pass.selection.coverage", std::move(coverage),
                  [drawCoverage](const PassResources&) { drawCoverage(true); });
    PassDesc visibility;
    visibility.bufferReads = sceneBuffers;
    visibility.depth = DepthAttachment{.handle = sceneDepth, .store = StoreOp::Store};
    graph.addPass("lmx.pass.selection.visibility", std::move(visibility),
                  [drawCoverage](const PassResources&) { drawCoverage(false); });
    const GraphTexture coverageRead = nextVersion(mask);
    const GraphTexture selectedDepthRead = nextVersion(depth);
    const GraphTexture sceneDepthRead = nextVersion(sceneDepth);
    const GraphTexture output = graph.importTexture(*m_target, kDisplayFormat, "selectionDisplay",
                                                    rojoRHI::TextureUse::ShaderRead);
    PassDesc composite;
    composite.textureReads = {display, coverageRead, selectedDepthRead, sceneDepthRead};
    composite.color = ColorAttachment{.handle = output};
    const float radius = std::clamp(backingScale * 1.5f, 1.0f, 6.0f);
    graph.addPass("lmx.pass.selection.outline", std::move(composite),
                  [this, &commands, display, coverageRead, selectedDepthRead, sceneDepthRead,
                   radius](const PassResources& resources) {
                      const auto source = resources.texture(display);
                      const auto coverageTexture = resources.texture(coverageRead);
                      const auto selectedDepthTexture = resources.texture(selectedDepthRead);
                      const auto sceneDepthTexture = resources.texture(sceneDepthRead);
                      LMX_ASSERT(source && coverageTexture && selectedDepthTexture &&
                                     sceneDepthTexture,
                                 "outline input must be declared");
                      commands.bindPipeline(*m_outlinePipeline);
                      commands.bindTexture(0, **source);
                      commands.bindTexture(1, **coverageTexture);
                      commands.bindTexture(2, **selectedDepthTexture);
                      commands.bindTexture(3, **sceneDepthTexture);
                      commands.bindFrameData(0, OutlineParams{radius});
                      commands.draw(3);
                  });
    return nextVersion(output);
}

} // namespace lmx::render
