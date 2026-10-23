//----------------------------------------------------------------------------------------------------------------------
/// @file HzbStage.cpp
/// @brief Implements padded depth-pyramid allocation and per-mip graph reduction.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/HzbStage.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Occlusion/HzbFrameData.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Align.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/GpuOwned.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {
//======================================================================================================================
HzbLayout hzbLayout(uint32_t outputWidth, uint32_t outputHeight) {
    LMX_ASSERT(outputWidth > 0 && outputHeight > 0, "HZB output extent must be nonzero");
    HzbLayout layout;
    layout.levelCount = 1;
    while (hzbLevelExtent(std::max(outputWidth, outputHeight), layout.levelCount - 1) > 16) {
        ++layout.levelCount;
    }
    const uint32_t alignment = uint32_t{1} << (layout.levelCount - 1);
    layout.width = static_cast<uint32_t>(alignUp(hzbLevelExtent(outputWidth, 0), alignment));
    layout.height = static_cast<uint32_t>(alignUp(hzbLevelExtent(outputHeight, 0), alignment));
    for (uint32_t level = 0; level < layout.levelCount; ++level) {
        layout.bytes +=
            uint64_t{layout.width >> level} * (layout.height >> level) * sizeof(float) * 2;
    }
    return layout;
}

//======================================================================================================================
lmx::Result<std::unique_ptr<HzbStage>> HzbStage::create(engine::GpuContext& gpu, bool cpuReadback) {
    rojoRHI::Device& device = gpu.device();
    auto stage = std::unique_ptr<HzbStage>(new HzbStage);
    stage->m_device = &device;
    stage->m_cpuReadback = cpuReadback;
    auto reduceLibrary = gpu.loadShaderLibrary("HzbReduce");
    if (!reduceLibrary) {
        return std::unexpected(reduceLibrary.error());
    }
    stage->m_reduceLibrary = std::move(*reduceLibrary);
    auto reduce = engine::adoptGpu(device.createComputePipeline(
        {.library = stage->m_reduceLibrary.get(),
         .computeEntry = "computeMain",
         .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
         .label = "lmx.render.hzbReducePipeline"}));
    if (!reduce) {
        return std::unexpected(reduce.error());
    }
    stage->m_reducePipeline = std::move(*reduce);
    auto publishLibrary = gpu.loadShaderLibrary("HzbPublish");
    if (!publishLibrary) {
        return std::unexpected(publishLibrary.error());
    }
    stage->m_publishLibrary = std::move(*publishLibrary);
    auto publish =
        engine::adoptGpu(device.createComputePipeline({.library = stage->m_publishLibrary.get(),
                                                       .computeEntry = "computeMain",
                                                       .threadsPerThreadgroup = {1, 1, 1},
                                                       .label = "lmx.render.hzbPublishPipeline"}));
    if (!publish) {
        return std::unexpected(publish.error());
    }
    stage->m_publishPipeline = std::move(*publish);
    auto buffer = engine::GpuBuffer::create(
        device, {.size = sizeof(float), .storageWrite = true, .label = "lmx.render.hzbPublish"});
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    stage->m_publishBuffer = std::move(*buffer);
    return stage;
}

//======================================================================================================================
lmx::Result<void> HzbStage::resize(uint32_t outputWidth, uint32_t outputHeight,
                                   TextureStateTable& states) {
    if (outputWidth == m_outputWidth && outputHeight == m_outputHeight) {
        return {};
    }
    const HzbLayout layout = hzbLayout(outputWidth, outputHeight);
    std::array<engine::GpuTexture, 2> textures;
    for (uint32_t index = 0; index < textures.size(); ++index) {
        const std::string label = std::format("lmx.render.hzb{}", index);
        auto texture = engine::GpuTexture::create(*m_device, {.width = layout.width,
                                                              .height = layout.height,
                                                              .format = rojoRHI::Format::R32Float,
                                                              .mipLevels = layout.levelCount,
                                                              .sampled = true,
                                                              .storageWrite = true,
                                                              .cpuReadback = m_cpuReadback,
                                                              .label = label});
        if (!texture) {
            return std::unexpected(texture.error());
        }
        textures[index] = std::move(*texture);
    }
    for (const engine::GpuTexture& replaced : m_textures) {
        if (replaced) {
            states.forget(replaced.rhi());
        }
    }
    m_textures = std::move(textures);
    m_sources = {};
    m_layout = layout;
    m_outputWidth = outputWidth;
    m_outputHeight = outputHeight;
    m_next = 0;
    m_lastBuilt = 1;
    return {};
}

//======================================================================================================================
GraphTexture HzbStage::importPrevious(RenderGraph& graph) const {
    LMX_ASSERT(m_textures[m_lastBuilt], "HZB must be resized before import");
    const std::string name = std::format("lmx.render.hzb{}", m_lastBuilt);
    rojoRHI::Texture& texture = m_textures[m_lastBuilt].rhi();
    const bool built = m_sources[m_lastBuilt].built;
    if (!built) {
        // Unbuilt -- before the first build or since a resize replaced the pyramids -- means no
        // earlier frame wrote it, whatever an earlier read of the same texture left behind.
        graph.textureStates().forget(texture);
    }
    const GraphTexture pyramid =
        graph.importPersistentTexture(texture, rojoRHI::Format::R32Float, name);
    if (built) {
        // The build left every mip as the publish pass's shader read; a consumer of the previous
        // pyramid reads it again in that state, which is what the next build of this slot waits on.
        graph.setFinalUse(pyramid, TextureUse::ShaderRead);
    }
    return pyramid;
}

//======================================================================================================================
GraphTexture HzbStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                               GraphTexture depth, HzbSource source) {
    LMX_ASSERT(m_textures[m_next], "HZB must be resized before build");
    LMX_ASSERT(source.activeWidth > 0 && source.activeHeight > 0 &&
                   source.activeWidth <= m_outputWidth && source.activeHeight <= m_outputHeight,
               "HZB source active rectangle exceeds output allocation");
    const uint32_t slot = m_next;
    const std::string name = std::format("lmx.render.hzb{}", slot);
    if (!m_sources[slot].built) {
        // An unbuilt slot was never written, so its build waits on nothing an earlier frame did.
        graph.textureStates().forget(m_textures[slot].rhi());
    }
    GraphTexture pyramid =
        graph.importPersistentTexture(m_textures[slot].rhi(), rojoRHI::Format::R32Float, name);
    // Every mip ends as the publish pass's shader read whatever reads the pyramid later in the
    // frame, which keeps one state per pyramid for the next frame's import to order behind.
    graph.setFinalUse(pyramid, TextureUse::ShaderRead);
    for (uint32_t level = 0; level < m_layout.levelCount; ++level) {
        const bool fromDepth = level == 0;
        const GraphTexture input = fromDepth ? depth : pyramid;
        const rojoRHI::TextureSubresourceRange inputRange{.baseMipLevel = fromDepth ? 0 : level - 1,
                                                          .mipLevelCount = 1};
        const rojoRHI::TextureSubresourceRange outputRange{.baseMipLevel = level,
                                                           .mipLevelCount = 1};
        const HzbReduceParams params =
            buildHzbReduceParams(source.activeWidth, source.activeHeight, level);
        ComputePassDesc desc;
        desc.shaderTextureReads.emplace_back(input, inputRange);
        desc.textureWrites.emplace_back(pyramid, outputRange);
        graph.addComputePass(
            std::format("lmx.pass.hzb.level{}", level), std::move(desc),
            [this, &commands, input, pyramid, outputRange, params](const PassResources& resources) {
                auto& sourceTexture = lmx::render::texture(resources, input);
                auto& outputTexture = lmx::render::texture(resources, pyramid);
                commands.bindComputePipeline(*m_reducePipeline);
                commands.bindTexture(0, sourceTexture);
                commands.bindStorageTexture(1, outputTexture, {.range = outputRange},
                                            rojoRHI::StorageAccess::Write);
                commands.bindFrameData(0, params);
                const auto groups =
                    dispatchGroups2D(params.destinationWidth, params.destinationHeight);
                commands.dispatch(groups[0], groups[1], 1);
            });
        pyramid = nextVersion(pyramid);
    }

    // A real read of every mip leaves one honest terminal use for the next frame's import.
    // Merely exporting the texture roots the writers but emits no terminal transition.
    const GraphBuffer publication =
        m_published ? graph.importBuffer(m_publishBuffer.rhi(), "lmx.render.hzbPublish",
                                         BufferUse::StorageWrite)
                    : graph.importBuffer(m_publishBuffer.rhi(), "lmx.render.hzbPublish");
    ComputePassDesc publish;
    publish.shaderTextureReads.push_back(pyramid);
    publish.bufferWrites.push_back(publication);
    const uint32_t levels = m_layout.levelCount;
    graph.addComputePass(
        "lmx.pass.hzb.publish", std::move(publish),
        [this, &commands, pyramid, publication, levels](const PassResources& resources) {
            auto& texture = lmx::render::texture(resources, pyramid);
            auto& buffer = lmx::render::buffer(resources, publication);
            commands.bindComputePipeline(*m_publishPipeline);
            commands.bindTexture(0, texture);
            commands.bindStorageBuffer(1, buffer, rojoRHI::StorageAccess::Write);
            commands.bindFrameData(0, levels);
            commands.dispatch(1, 1, 1);
        });
    graph.exportBuffer(nextVersion(publication));
    graph.exportTexture(pyramid);
    source.outputWidth = m_outputWidth;
    source.outputHeight = m_outputHeight;
    source.levelCount = levels;
    source.built = true;
    m_sources[slot] = source;
    m_lastBuilt = slot;
    m_next ^= 1;
    m_published = true;
    return pyramid;
}

} // namespace lmx::render
