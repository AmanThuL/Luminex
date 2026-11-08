//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutline.cpp
/// @brief Implements visible selection coverage and a separate SDR outline composite.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/SelectionOutline/SelectionOutline.h"
#include "Render/Common/Formats.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/SelectionOutline/SelectionOutlineFrameData.h"
#include "Render/Passes/SelectionOutline/SelectionOutlineRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/View/Camera.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {
//======================================================================================================================
void registerSelectionLayouts() {
    engine::registerUniformStruct(
        {"SelectionMaskRoot",
         sizeof(SelectionMaskRoot),
         {{"verticesAddr", offsetof(SelectionMaskRoot, verticesAddr), "Addr"},
          {"instancesAddr", offsetof(SelectionMaskRoot, instancesAddr), "Addr"},
          {"materialsAddr", offsetof(SelectionMaskRoot, materialsAddr), "Addr"},
          {"paramsAddr", offsetof(SelectionMaskRoot, paramsAddr), "Addr"},
          {"diffuse", offsetof(SelectionMaskRoot, diffuse), "uint"},
          {"sampler", offsetof(SelectionMaskRoot, sampler), "uint"},
          {"padding0", offsetof(SelectionMaskRoot, padding0), "uint"},
          {"padding1", offsetof(SelectionMaskRoot, padding1), "uint"}}});
    engine::registerUniformStruct(
        {"SelectionOutlineRoot",
         sizeof(SelectionOutlineRoot),
         {{"paramsAddr", offsetof(SelectionOutlineRoot, paramsAddr), "Addr"},
          {"display", offsetof(SelectionOutlineRoot, display), "uint"},
          {"mask", offsetof(SelectionOutlineRoot, mask), "uint"},
          {"selectedDepth", offsetof(SelectionOutlineRoot, selectedDepth), "uint"},
          {"sceneDepth", offsetof(SelectionOutlineRoot, sceneDepth), "uint"},
          {"padding0", offsetof(SelectionOutlineRoot, padding0), "uint"},
          {"padding1", offsetof(SelectionOutlineRoot, padding1), "uint"}}});
    engine::registerUniformStruct({"SelectionCopyRoot",
                                   sizeof(SelectionCopyRoot),
                                   {{"display", offsetof(SelectionCopyRoot, display), "uint"},
                                    {"padding0", offsetof(SelectionCopyRoot, padding0), "uint"},
                                    {"padding1", offsetof(SelectionCopyRoot, padding1), "uint"},
                                    {"padding2", offsetof(SelectionCopyRoot, padding2), "uint"}}});
    engine::registerUniformStruct({"MaskParams",
                                   sizeof(MaskParams),
                                   {{"viewProj", offsetof(MaskParams, viewProj), "float4x4"},
                                    {"instanceRow", offsetof(MaskParams, instanceRow), "uint"},
                                    {"selected", offsetof(MaskParams, selected), "float"},
                                    {"padding", offsetof(MaskParams, padding), "uint[2]"}}});
    engine::registerUniformStruct({"OutlineParams",
                                   sizeof(OutlineParams),
                                   {{"radius", offsetof(OutlineParams, radius), "float"}}});
}

#ifndef NDEBUG
//======================================================================================================================
void declareTextureRead(rojoRHI::CommandList& commands, const engine::TextureRef& texture) {
    const std::array<rojoRHI::ResourceAccess, 1> access{
        {{.texture = texture.texture,
          .range = {0, texture.mips, 0, texture.layers},
          .stage = rojoRHI::Stage::FragmentShader,
          .access = rojoRHI::Access::ShaderRead}}};
    commands.declareAccess(access);
}

//======================================================================================================================
// GPU-private material flags cannot be observed through a CPU mirror. Their conditional texture
// read stays unobserved, like pointed frame-arena payloads; alphaMode never substitutes for flags.
std::optional<bool> samplesDiffuse(const SceneView& view, uint32_t materialRow) {
    const auto& materials = *view.tables.materials;
    const uint64_t offset = uint64_t{materialRow} * sizeof(engine::MaterialRow);
    LMX_ASSERT(materialRow < view.tables.materialCount && offset <= materials.desc().size &&
                   sizeof(engine::MaterialRow) <= materials.desc().size - offset,
               "selection material row exceeds its prepared storage");
    if (materials.desc().memory != rojoRHI::Memory::Upload) {
        return std::nullopt;
    }
    uint32_t flags = 0;
    std::memcpy(&flags, materials.cpuPointer() + offset + offsetof(engine::MaterialRow, flags),
                sizeof(flags));
    return (flags & engine::kMaterialMasked) != 0;
}

//======================================================================================================================
void declareCoverageReads(rojoRHI::CommandList& commands, const SceneView& view,
                          const engine::DrawItem& item, const engine::GpuTexture& diffuse) {
    LMX_ASSERT(item.instanceRow < view.tables.instanceCount &&
                   item.instanceRow < view.tables.instanceRows.size(),
               "selection access requires a current instance row");
    const uint32_t materialRow = view.tables.instanceRows[item.instanceRow].materialRow;
    const uint64_t instanceOffset = uint64_t{item.instanceRow} * sizeof(engine::InstanceRow);
    const uint64_t materialOffset = uint64_t{materialRow} * sizeof(engine::MaterialRow);
    LMX_ASSERT(instanceOffset <= view.tables.instances->desc().size &&
                   sizeof(engine::InstanceRow) <=
                       view.tables.instances->desc().size - instanceOffset,
               "selection instance row exceeds its prepared storage");
    LMX_ASSERT(materialRow < view.tables.materialCount &&
                   materialOffset <= view.tables.materials->desc().size &&
                   sizeof(engine::MaterialRow) <=
                       view.tables.materials->desc().size - materialOffset,
               "selection material row exceeds its prepared storage");
    const std::array<rojoRHI::ResourceAccess, 5> access{{{.buffer = view.tables.vertices->rhi(),
                                                          .size = view.tables.vertices->desc().size,
                                                          .stage = rojoRHI::Stage::VertexShader,
                                                          .access = rojoRHI::Access::ShaderRead},
                                                         {.buffer = view.tables.instances->rhi(),
                                                          .offset = instanceOffset,
                                                          .size = sizeof(engine::InstanceRow),
                                                          .stage = rojoRHI::Stage::VertexShader,
                                                          .access = rojoRHI::Access::ShaderRead},
                                                         {.buffer = view.tables.materials->rhi(),
                                                          .offset = materialOffset,
                                                          .size = sizeof(engine::MaterialRow),
                                                          .stage = rojoRHI::Stage::VertexShader,
                                                          .access = rojoRHI::Access::ShaderRead},
                                                         {.buffer = view.tables.instances->rhi(),
                                                          .offset = instanceOffset,
                                                          .size = sizeof(engine::InstanceRow),
                                                          .stage = rojoRHI::Stage::FragmentShader,
                                                          .access = rojoRHI::Access::ShaderRead},
                                                         {.buffer = view.tables.materials->rhi(),
                                                          .offset = materialOffset,
                                                          .size = sizeof(engine::MaterialRow),
                                                          .stage = rojoRHI::Stage::FragmentShader,
                                                          .access = rojoRHI::Access::ShaderRead}}};
    commands.declareAccess(access);
    if (samplesDiffuse(view, materialRow).value_or(false)) {
        declareTextureRead(commands, diffuse.ref());
    }
}
#endif
} // namespace

//======================================================================================================================
SelectionOutline::SelectionOutline(rojoRHI::Device device, bool readback) : m_device(device) {
    // All GPU texture readbacks use staging, regardless of this caller compatibility option.
    static_cast<void>(readback);
}

//======================================================================================================================
lmx::Result<std::unique_ptr<SelectionOutline>>
SelectionOutline::create(engine::GpuContext& gpu, uint32_t width, uint32_t height, bool readback) {
    rojoRHI::Device& device = gpu.device();
    auto self = std::unique_ptr<SelectionOutline>(new SelectionOutline(device, readback));
    registerSelectionLayouts();
    auto maskLibrary = gpu.loadShaderLibrary("SelectionMask");
    if (!maskLibrary) {
        return std::unexpected(maskLibrary.error());
    }
    self->m_maskLibrary = std::move(*maskLibrary);
    auto outlineLibrary = gpu.loadShaderLibrary("SelectionOutline");
    if (!outlineLibrary) {
        return std::unexpected(outlineLibrary.error());
    }
    self->m_outlineLibrary = std::move(*outlineLibrary);
    for (bool doubleSided : {false, true}) {
        const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::R8Unorm}}};
        auto pipeline = engine::adoptGpu(
            device,
            device.createPipeline(
                {.vertex = {self->m_maskLibrary.get(), "vertexMain"},
                 .fragment = {self->m_maskLibrary.get(), "fragmentMain"},
                 .raster = {.cull =
                                doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back},
                 .depthStencil = {.depthTest = true,
                                  .depthWrite = true,
                                  .depthCompare = rojoRHI::CompareOp::Greater},
                 .colors = colors,
                 .depthFormat = rojoRHI::Format::D32Float,
                 .label = doubleSided ? "lmx.selection.maskDoubleSided" : "lmx.selection.mask"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        (doubleSided ? self->m_doubleSidedPipeline : self->m_maskPipeline) = std::move(*pipeline);
        auto depthPipeline = engine::adoptGpu(
            device,
            device.createPipeline(
                {.vertex = {self->m_maskLibrary.get(), "vertexMain"},
                 .fragment = {self->m_maskLibrary.get(), "depthMain"},
                 .raster = {.cull =
                                doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back},
                 .depthStencil = {.depthTest = true,
                                  .depthWrite = true,
                                  .depthCompare = rojoRHI::CompareOp::Greater},
                 .depthFormat = rojoRHI::Format::D32Float,
                 .label = doubleSided ? "lmx.selection.depthDoubleSided" : "lmx.selection.depth"}));
        if (!depthPipeline) {
            return std::unexpected(depthPipeline.error());
        }
        (doubleSided ? self->m_doubleSidedDepthPipeline : self->m_depthPipeline) =
            std::move(*depthPipeline);
    }
    auto outline = createFullscreenPipeline(device, self->m_outlineLibrary.get(), "fragmentMain",
                                            kDisplayFormat, "lmx.selection.outline");
    if (!outline) {
        return std::unexpected(outline.error());
    }
    self->m_outlinePipeline = std::move(*outline);
    auto passthrough =
        createFullscreenPipeline(device, self->m_outlineLibrary.get(), "fragmentCopy",
                                 kDisplayFormat, "lmx.selection.passthrough");
    if (!passthrough) {
        return std::unexpected(passthrough.error());
    }
    self->m_passthroughPipeline = std::move(*passthrough);
    auto sampler = engine::adoptGpu(
        device,
        device.createSampler({.maxAnisotropy = 16, .label = "lmx.selection.materialSampler"}));
    if (!sampler) {
        return std::unexpected(sampler.error());
    }
    self->m_sampler = std::move(*sampler);
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    auto texture = createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureType::Tex2D,
                               std::as_bytes(std::span{white}), "lmx.selection.white");
    if (!texture) {
        return std::unexpected(texture.error());
    }
    self->m_white = std::move(*texture);
    {
        auto result = self->replaceTarget(width, height, nullptr);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return self;
}

//======================================================================================================================
lmx::Result<void> SelectionOutline::resize(uint32_t width, uint32_t height,
                                           TextureStateTable& states) {
    return replaceTarget(width, height, &states);
}

//======================================================================================================================
lmx::Result<void> SelectionOutline::replaceTarget(uint32_t width, uint32_t height,
                                                  TextureStateTable* states) {
    auto target = engine::GpuTexture::create(
        m_device, {.format = kDisplayFormat,
                   .extent = {width, height, 1},
                   .usage = rojoRHI::TextureUsage::RenderTarget | rojoRHI::TextureUsage::Sampled,
                   .label = "lmx.selection.display"});
    if (!target) {
        return std::unexpected(target.error());
    }
    if (states != nullptr && m_target) {
        states->forget(m_target.rhi());
    }
    m_target = std::move(*target);
    return {};
}

//======================================================================================================================
GraphTexture SelectionOutline::importTarget(RenderGraph& graph) {
    const GraphTexture output = graph.importPersistentTexture(m_target, "selectionDisplay");
    // The UI samples the target after the graph, a pass it encodes outside it, so the target is
    // left as a shader read rather than as the attachment write that refreshed it.
    graph.setFinalUse(output, TextureUse::ShaderRead);
    return output;
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
    const auto selectedRow = view.items[selectedDraw].instanceRow;
    LMX_ASSERT(selectedRow < view.tables.instanceRows.size(),
               "selection requires a canonical instance row");
    if (!visible || (view.tables.instanceRows[selectedRow].flags & engine::kInstanceDisabled)) {
        const auto output = importTarget(graph);
        PassDesc passthrough;
        passthrough.fragmentTextureReads = {display};
        passthrough.color = ColorAttachment{.handle = output};
        // The UI has already chosen this target. Refresh it while keeping display's terminal
        // shader-read state consistent with both ordinary outline frames and the next import.
        graph.addPass("lmx.pass.selection.passthrough", std::move(passthrough),
                      [this, &commands, display](const PassResources& resources) {
                          const auto source = lmx::render::texture(resources, display);
                          commands.setPipeline(m_passthroughPipeline.get());
                          engine::registerFrameRoot(
                              "SelectionCopyRoot",
                              commands.pushRoot(SelectionCopyRoot{
                                  .display = static_cast<uint32_t>(source.view)}));
#ifndef NDEBUG
                          declareTextureRead(commands, source);
#endif
                          commands.draw(3);
                      });
        return nextVersion(output);
    }
    const uint32_t width = m_target.desc().extent.width;
    const uint32_t height = m_target.desc().extent.height;
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
        for (uint32_t index = 0; index < view.items.size(); ++index) {
            const auto& item = view.items[index];
            LMX_ASSERT(item.instanceRow < view.tables.instanceRows.size(),
                       "outline coverage requires canonical instance rows");
            if (view.tables.instanceRows[item.instanceRow].flags & engine::kInstanceDisabled)
                continue;
            if (selectedOnly && index != selectedDraw) {
                continue;
            }
            const bool masked = item.alphaMode == engine::AlphaMode::Mask;
            const bool doubleSided = masked && item.doubleSided;
            commands.setPipeline(
                selectedOnly
                    ? (doubleSided ? m_doubleSidedPipeline.get() : m_maskPipeline.get())
                    : (doubleSided ? m_doubleSidedDepthPipeline.get() : m_depthPipeline.get()));
            const auto& diffuse = item.diffuse ? *item.diffuse : m_white;
            const MaskParams params = buildMaskParams(viewProjection, item.instanceRow);
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("MaskParams", paramsAddress);
            engine::registerFrameRoot("SelectionMaskRoot",
                                      commands.pushRoot(SelectionMaskRoot{
                                          .verticesAddr = view.tables.vertices->address(),
                                          .instancesAddr = view.tables.instances->address(),
                                          .materialsAddr = view.tables.materials->address(),
                                          .paramsAddr = paramsAddress,
                                          .diffuse = static_cast<uint32_t>(diffuse.view()),
                                          .sampler = static_cast<uint32_t>(m_sampler.get())}));
#ifndef NDEBUG
            if (item.mesh.indexCount != 0) {
                declareCoverageReads(commands, view, item, diffuse);
            }
#endif
            commands.drawIndexed(view.tables.indices->rhi(), rojoRHI::IndexType::UInt32,
                                 item.mesh.indexCount, 1, item.mesh.firstIndex, 0, 0);
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
    const GraphTexture output = importTarget(graph);
    PassDesc composite;
    // Fullscreen, like the passthrough: only the fragment stage reads.
    composite.fragmentTextureReads = {display, coverageRead, selectedDepthRead, sceneDepthRead};
    composite.color = ColorAttachment{.handle = output};
    const OutlineParams outlineParams = buildOutlineParams(backingScale);
    graph.addPass("lmx.pass.selection.outline", std::move(composite),
                  [this, &commands, display, coverageRead, selectedDepthRead, sceneDepthRead,
                   outlineParams](const PassResources& resources) {
                      const auto source = lmx::render::texture(resources, display);
                      const auto coverageTexture = lmx::render::texture(resources, coverageRead);
                      const auto selectedDepthTexture =
                          lmx::render::texture(resources, selectedDepthRead);
                      const auto sceneDepthTexture =
                          lmx::render::texture(resources, sceneDepthRead);
                      commands.setPipeline(m_outlinePipeline.get());
                      const auto paramsAddress = frameData(commands, outlineParams);
                      engine::registerFrameRoot("OutlineParams", paramsAddress);
                      engine::registerFrameRoot(
                          "SelectionOutlineRoot",
                          commands.pushRoot(SelectionOutlineRoot{
                              .paramsAddr = paramsAddress,
                              .display = static_cast<uint32_t>(source.view),
                              .mask = static_cast<uint32_t>(coverageTexture.view),
                              .selectedDepth = static_cast<uint32_t>(selectedDepthTexture.view),
                              .sceneDepth = static_cast<uint32_t>(sceneDepthTexture.view)}));
#ifndef NDEBUG
                      for (const auto& texture :
                           {source, coverageTexture, selectedDepthTexture, sceneDepthTexture}) {
                          declareTextureRead(commands, texture);
                      }
#endif
                      commands.draw(3);
                  });
    return nextVersion(output);
}

} // namespace lmx::render
