//----------------------------------------------------------------------------------------------------------------------
/// @file LightDebugStage.cpp
/// @brief Binds post-display light diagnostics to the actual depth, light grid and index list.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/LocalLights/LightDebugStage.h"
#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/Formats.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/LocalLights/LightClusterFrameData.h"
#include "Render/Passes/LocalLights/LightRoots.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace lmx::render {
namespace {
// The parameters begin with a float4x4, which a shader pointer load reads sixteen-byte aligned.
constexpr uint32_t kParamsAlignment = 16;
// The scalar block is 188 bytes, but the Metal struct a whole-struct pointer load reads is padded
// to its 16-byte alignment. The upload covers that padded size, zero-filled, so the load never
// reads past the upload at the end of a frame-arena page.
constexpr size_t kParamsUploadBytes =
    (sizeof(LightDebugParams) + kParamsAlignment - 1) / kParamsAlignment * kParamsAlignment;
static_assert(kParamsUploadBytes == 192);
} // namespace

//======================================================================================================================
lmx::Result<std::unique_ptr<LightDebugStage>> LightDebugStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    auto stage = std::unique_ptr<LightDebugStage>(new LightDebugStage);
    auto library = gpu.loadShaderLibrary("LightDebugView");
    if (!library) {
        return std::unexpected(library.error());
    }
    stage->m_library = std::move(*library);
    auto pipeline = createFullscreenPipeline(device, stage->m_library.get(), "fragmentMain",
                                             kDisplayFormat, "lmx.render.lightDebugPipeline");
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    stage->m_pipeline = std::move(*pipeline);
    const auto field = [](std::string name, size_t offset, std::string type) {
        return engine::SchemaUniformField{.name = std::move(name),
                                          .offsetBytes = static_cast<uint32_t>(offset),
                                          .type = std::move(type)};
    };
    engine::registerUniformStruct(
        {.name = "LightDebugParams",
         .sizeBytes = sizeof(LightDebugParams),
         .fields = {field("inverseViewProjection",
                          offsetof(LightDebugParams, inverseViewProjection), "float4x4"),
                    field("mode", offsetof(LightDebugParams, mode), "uint"),
                    field("rowCount", offsetof(LightDebugParams, rowCount), "uint"),
                    field("activeWidth", offsetof(LightDebugParams, activeWidth), "uint"),
                    field("activeHeight", offsetof(LightDebugParams, activeHeight), "uint"),
                    field("outputWidth", offsetof(LightDebugParams, outputWidth), "uint"),
                    field("outputHeight", offsetof(LightDebugParams, outputHeight), "uint"),
                    field("sliceDepth", offsetof(LightDebugParams, sliceDepth), "float[25]")}});
    engine::registerUniformStruct(
        {.name = "LightDebugRoot",
         .sizeBytes = sizeof(LightDebugRoot),
         .fields = {field("params", offsetof(LightDebugRoot, params), "Addr"),
                    field("lights", offsetof(LightDebugRoot, lights), "Addr"),
                    field("grid", offsetof(LightDebugRoot, grid), "Addr"),
                    field("indices", offsetof(LightDebugRoot, indices), "Addr"),
                    field("depth", offsetof(LightDebugRoot, depth), "uint"),
                    field("display", offsetof(LightDebugRoot, display), "uint"),
                    field("padding0", offsetof(LightDebugRoot, padding0), "uint"),
                    field("padding1", offsetof(LightDebugRoot, padding1), "uint")}});
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
    const LightDebugParams params =
        buildLightDebugParams({.mode = inputs.mode,
                               .clusters = inputs.clusters,
                               .inverseViewProjection = inputs.inverseViewProjection,
                               .outputWidth = inputs.outputWidth,
                               .outputHeight = inputs.outputHeight});
    PassDesc pass;
    pass.color = ColorAttachment{.handle = inputs.output};
    pass.textureReads = {inputs.depth, inputs.display};
    pass.bufferReads = {inputs.lights, inputs.grid, inputs.indices};
    graph.addPass(
        "lmx.pass.light.debug", std::move(pass),
        [this, &commands, inputs, params](const PassResources& resources) {
            std::array<std::byte, kParamsUploadBytes> paramBytes{};
            std::memcpy(paramBytes.data(), &params, sizeof(params));
            const rojoRHI::Addr paramsAddr =
                commands.uploadFrameData(std::span<const std::byte>{paramBytes}, kParamsAlignment);
            engine::registerFrameRoot("LightDebugParams", paramsAddr);
            commands.setPipeline(m_pipeline.get());
            engine::registerFrameRoot(
                "LightDebugRoot",
                commands.pushRoot(LightDebugRoot{
                    .params = paramsAddr,
                    .lights = resources.address(inputs.lights),
                    .grid = resources.address(inputs.grid),
                    .indices = resources.address(inputs.indices),
                    .depth = static_cast<uint32_t>(resources.view(inputs.depth)),
                    .display = static_cast<uint32_t>(resources.view(inputs.display))}));
#ifndef NDEBUG
            // Every view loads depth and, off the sky, the grid; Overflow alone loads the display
            // image, and Missed alone walks the light rows and index lists.
            const auto bufferRead = [&](GraphBuffer handle, uint64_t size) {
                return rojoRHI::ResourceAccess{.buffer = resources.buffer(handle),
                                               .size = size,
                                               .range = {},
                                               .stage = rojoRHI::Stage::FragmentShader,
                                               .access = rojoRHI::Access::ShaderRead};
            };
            const auto textureRead = [&](GraphTexture handle) {
                return rojoRHI::ResourceAccess{
                    .texture = resources.texture(handle).texture,
                    .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1},
                    .stage = rojoRHI::Stage::FragmentShader,
                    .access = rojoRHI::Access::ShaderRead};
            };
            std::array<rojoRHI::ResourceAccess, 5> accesses{};
            uint32_t count = 0;
            accesses[count++] = textureRead(inputs.depth);
            accesses[count++] =
                bufferRead(inputs.grid, uint64_t{kClusterCount} * sizeof(ClusterRecord));
            if (inputs.mode == engine::LightDebugView::Overflow) {
                accesses[count++] = textureRead(inputs.display);
            }
            // Missed walks the rows; with no addressable row it loads neither rows nor lists.
            if (inputs.mode == engine::LightDebugView::Missed && params.rowCount != 0) {
                accesses[count++] =
                    bufferRead(inputs.lights, uint64_t{params.rowCount} * sizeof(engine::LightRow));
                // A froxel's range lies inside the list but its bound is a GPU value.
                accesses[count++] = bufferRead(inputs.indices, rojoRHI::kWholeSize);
            }
            commands.declareAccess(std::span{accesses}.first(count));
#endif
            commands.draw(3);
        });
    return nextVersion(inputs.output);
}
} // namespace lmx::render
