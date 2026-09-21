//----------------------------------------------------------------------------------------------------------------------
/// @file HzbStage.cpp
/// @brief Implements padded depth-pyramid allocation and per-mip graph reduction.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/HzbStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {
namespace {

struct HzbReduceParams {
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    uint32_t destinationWidth;
    uint32_t destinationHeight;
    uint32_t sourceLevel;
};
static_assert(sizeof(HzbReduceParams) == 20);
constexpr uint32_t kThreads = 8;

} // namespace

//======================================================================================================================
uint32_t hzbLevelExtent(uint32_t sourceExtent, uint32_t level) {
    LMX_ASSERT(sourceExtent > 0 && level < 31, "Invalid HZB source extent or level");
    return divRoundUp(sourceExtent, uint32_t{1} << (level + 1));
}

//======================================================================================================================
HzbLayout hzbLayout(uint32_t outputWidth, uint32_t outputHeight) {
    LMX_ASSERT(outputWidth > 0 && outputHeight > 0, "HZB output extent must be nonzero");
    HzbLayout layout;
    layout.levelCount = 1;
    while (hzbLevelExtent(std::max(outputWidth, outputHeight), layout.levelCount - 1) > 16) {
        ++layout.levelCount;
    }
    const uint32_t alignment = uint32_t{1} << (layout.levelCount - 1);
    layout.width = divRoundUp(hzbLevelExtent(outputWidth, 0), alignment) * alignment;
    layout.height = divRoundUp(hzbLevelExtent(outputHeight, 0), alignment) * alignment;
    for (uint32_t level = 0; level < layout.levelCount; ++level) {
        layout.bytes +=
            uint64_t{layout.width >> level} * (layout.height >> level) * sizeof(float) * 2;
    }
    return layout;
}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<HzbStage>> HzbStage::create(rojoRHI::Device& device,
                                                            bool cpuReadback) {
    auto stage = std::make_unique<HzbStage>();
    stage->m_device = &device;
    stage->m_cpuReadback = cpuReadback;
    auto reduceLibrary = device.loadShaderLibrary("Shaders/HzbReduce");
    if (!reduceLibrary) {
        return std::unexpected(reduceLibrary.error());
    }
    stage->m_reduceLibrary = std::move(*reduceLibrary);
    auto reduce = device.createComputePipeline({.library = stage->m_reduceLibrary.get(),
                                                .computeEntry = "computeMain",
                                                .threadsPerThreadgroup = {kThreads, kThreads, 1},
                                                .label = "lmx.render.hzbReducePipeline"});
    if (!reduce) {
        return std::unexpected(reduce.error());
    }
    stage->m_reducePipeline = std::move(*reduce);
    auto publishLibrary = device.loadShaderLibrary("Shaders/HzbPublish");
    if (!publishLibrary) {
        return std::unexpected(publishLibrary.error());
    }
    stage->m_publishLibrary = std::move(*publishLibrary);
    auto publish = device.createComputePipeline({.library = stage->m_publishLibrary.get(),
                                                 .computeEntry = "computeMain",
                                                 .threadsPerThreadgroup = {1, 1, 1},
                                                 .label = "lmx.render.hzbPublishPipeline"});
    if (!publish) {
        return std::unexpected(publish.error());
    }
    stage->m_publishPipeline = std::move(*publish);
    auto buffer = device.createBuffer(
        {.size = sizeof(float), .storageWrite = true, .label = "lmx.render.hzbPublish"}, nullptr);
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    stage->m_publishBuffer = std::move(*buffer);
    return stage;
}

//======================================================================================================================
rojoRHI::Result<void> HzbStage::resize(uint32_t outputWidth, uint32_t outputHeight) {
    if (outputWidth == m_outputWidth && outputHeight == m_outputHeight) {
        return {};
    }
    const HzbLayout layout = hzbLayout(outputWidth, outputHeight);
    std::array<std::unique_ptr<rojoRHI::Texture>, 2> textures;
    for (uint32_t index = 0; index < textures.size(); ++index) {
        const std::string label = std::format("lmx.render.hzb{}", index);
        auto texture = m_device->createTexture({.width = layout.width,
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
    if (m_sources[m_lastBuilt].built) {
        return graph.importTexture(*m_textures[m_lastBuilt], rojoRHI::Format::R32Float, name,
                                   rojoRHI::TextureUse::ShaderRead);
    }
    return graph.importTexture(*m_textures[m_lastBuilt], rojoRHI::Format::R32Float, name);
}

//======================================================================================================================
GraphTexture HzbStage::build(RenderGraph& graph, rojoRHI::CommandList& commands, GraphTexture depth,
                             HzbSource source) {
    LMX_ASSERT(m_textures[m_next], "HZB must be resized before build");
    LMX_ASSERT(source.activeWidth > 0 && source.activeHeight > 0 &&
                   source.activeWidth <= m_outputWidth && source.activeHeight <= m_outputHeight,
               "HZB source active rectangle exceeds output allocation");
    const uint32_t slot = m_next;
    const std::string name = std::format("lmx.render.hzb{}", slot);
    GraphTexture pyramid =
        m_sources[slot].built
            ? graph.importTexture(*m_textures[slot], rojoRHI::Format::R32Float, name,
                                  rojoRHI::TextureUse::ShaderRead)
            : graph.importTexture(*m_textures[slot], rojoRHI::Format::R32Float, name);
    for (uint32_t level = 0; level < m_layout.levelCount; ++level) {
        const bool fromDepth = level == 0;
        const GraphTexture input = fromDepth ? depth : pyramid;
        const rojoRHI::TextureSubresourceRange inputRange{.baseMipLevel = fromDepth ? 0 : level - 1,
                                                          .mipLevelCount = 1};
        const rojoRHI::TextureSubresourceRange outputRange{.baseMipLevel = level,
                                                           .mipLevelCount = 1};
        const HzbReduceParams params{
            .sourceWidth =
                fromDepth ? source.activeWidth : hzbLevelExtent(source.activeWidth, level - 1),
            .sourceHeight =
                fromDepth ? source.activeHeight : hzbLevelExtent(source.activeHeight, level - 1),
            .destinationWidth = hzbLevelExtent(source.activeWidth, level),
            .destinationHeight = hzbLevelExtent(source.activeHeight, level),
            .sourceLevel = inputRange.baseMipLevel};
        ComputePassDesc desc;
        desc.shaderTextureReads.emplace_back(input, inputRange);
        desc.textureWrites.emplace_back(pyramid, outputRange);
        graph.addComputePass(
            std::format("lmx.pass.hzb.level{}", level), std::move(desc),
            [this, &commands, input, pyramid, outputRange, params](const PassResources& resources) {
                auto sourceTexture = resources.texture(input);
                auto outputTexture = resources.texture(pyramid);
                LMX_ASSERT(sourceTexture && outputTexture, "HZB declared textures unavailable");
                commands.bindComputePipeline(*m_reducePipeline);
                commands.bindTexture(0, **sourceTexture);
                commands.bindStorageTexture(1, **outputTexture, {.range = outputRange},
                                            rojoRHI::StorageAccess::Write);
                commands.bindFrameData(0, params);
                commands.dispatch(divRoundUp(params.destinationWidth, kThreads),
                                  divRoundUp(params.destinationHeight, kThreads), 1);
            });
        pyramid = nextVersion(pyramid);
    }

    // A real read of every mip leaves one honest terminal use for the next frame's import.
    // Merely exporting the texture roots the writers but emits no terminal transition.
    const GraphBuffer publication =
        m_published ? graph.importBuffer(*m_publishBuffer, "lmx.render.hzbPublish",
                                         rojoRHI::BufferUse::StorageWrite)
                    : graph.importBuffer(*m_publishBuffer, "lmx.render.hzbPublish");
    ComputePassDesc publish;
    publish.shaderTextureReads.push_back(pyramid);
    publish.bufferWrites.push_back(publication);
    const uint32_t levels = m_layout.levelCount;
    graph.addComputePass(
        "lmx.pass.hzb.publish", std::move(publish),
        [this, &commands, pyramid, publication, levels](const PassResources& resources) {
            auto texture = resources.texture(pyramid);
            auto buffer = resources.buffer(publication);
            LMX_ASSERT(texture && buffer, "HZB publication resources unavailable");
            commands.bindComputePipeline(*m_publishPipeline);
            commands.bindTexture(0, **texture);
            commands.bindStorageBuffer(1, **buffer, rojoRHI::StorageAccess::Write);
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
