//----------------------------------------------------------------------------------------------------------------------
/// @file HzbStage.cpp
/// @brief Implements padded depth-pyramid allocation and per-mip graph reduction.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/HzbStage.h"
#include "Render/Passes/Occlusion/HzbHistoryBinding.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Align.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Occlusion/OcclusionRoots.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace lmx::render {
struct PreparedHzbResize::Impl {
    const HzbStage* owner = nullptr;
    const engine::GpuContext* context = nullptr;
    const HzbStage::Allocation* baseAllocation = nullptr;
    uint64_t baseGeneration = 0;
    uint32_t outputWidth = 0, outputHeight = 0;
    HzbLayout layout;
    bool initialized = false, noOp = false, consumed = false;
    std::shared_ptr<HzbStage::Allocation> replacement;
};

//======================================================================================================================
PreparedHzbResize::PreparedHzbResize(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

//======================================================================================================================
PreparedHzbResize::PreparedHzbResize(PreparedHzbResize&& other) noexcept = default;

//======================================================================================================================
PreparedHzbResize& PreparedHzbResize::operator=(PreparedHzbResize&& other) noexcept = default;

//======================================================================================================================
PreparedHzbResize::~PreparedHzbResize() noexcept = default;

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
    auto stage = std::unique_ptr<HzbStage>(new HzbStage(gpu));
    (void)cpuReadback;
    const std::array<const char*, 2> names{"HzbReduce", "HzbPublish"};
    auto pipelines = createComputePipelines(gpu, names, "lmx.render.");
    if (!pipelines)
        return std::unexpected(pipelines.error());
    stage->m_reduceLibrary = std::move(pipelines->libraries[0]);
    stage->m_reducePipeline = std::move(pipelines->pipelines[0]);
    stage->m_publishLibrary = std::move(pipelines->libraries[1]);
    stage->m_publishPipeline = std::move(pipelines->pipelines[1]);
    auto buffer =
        engine::GpuBuffer::create(gpu.device(), {.size = sizeof(float),
                                                 .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                 .label = "lmx.render.hzbPublish"});
    if (!buffer)
        return std::unexpected(buffer.error());
    stage->m_publishBuffer = std::move(*buffer);
    engine::registerUniformStruct(
        {"HzbReduceRoot",
         sizeof(HzbReduceRoot),
         {{"paramsAddr", offsetof(HzbReduceRoot, paramsAddr), "Addr"},
          {"source", offsetof(HzbReduceRoot, source), "uint"},
          {"destination", offsetof(HzbReduceRoot, destination), "uint"}}});
    engine::registerUniformStruct(
        {"HzbPublishRoot",
         sizeof(HzbPublishRoot),
         {{"publicationAddr", offsetof(HzbPublishRoot, publicationAddr), "Addr"},
          {"pyramid", offsetof(HzbPublishRoot, pyramid), "uint"},
          {"levelCount", offsetof(HzbPublishRoot, levelCount), "uint"}}});
    engine::registerUniformStruct(
        {"HzbReduceParams",
         sizeof(HzbReduceParams),
         {{"sourceWidth", offsetof(HzbReduceParams, sourceWidth), "uint"},
          {"sourceHeight", offsetof(HzbReduceParams, sourceHeight), "uint"},
          {"destinationWidth", offsetof(HzbReduceParams, destinationWidth), "uint"},
          {"destinationHeight", offsetof(HzbReduceParams, destinationHeight), "uint"},
          {"sourceLevel", offsetof(HzbReduceParams, sourceLevel), "uint"}}});
    return stage;
}

//======================================================================================================================
lmx::Result<PreparedHzbResize> HzbStage::prepareResize(uint32_t outputWidth,
                                                       uint32_t outputHeight) const {
    const HzbLayout layout = hzbLayout(outputWidth, outputHeight);
    const bool initialized = m_allocation && m_allocation->textures[0] && m_allocation->textures[1];
    LMX_ASSERT(!m_allocation || initialized, "HZB allocation requires both pyramid owners");
    auto prepared = std::make_unique<PreparedHzbResize::Impl>();
    prepared->owner = this;
    prepared->context = &m_gpu;
    prepared->baseAllocation = m_allocation.get();
    prepared->baseGeneration = m_allocationGeneration;
    prepared->outputWidth = outputWidth;
    prepared->outputHeight = outputHeight;
    prepared->layout = layout;
    prepared->initialized = initialized;
    prepared->noOp = initialized && outputWidth == m_outputWidth && outputHeight == m_outputHeight;
    if (prepared->noOp)
        return PreparedHzbResize(std::move(prepared));

    auto allocation = std::make_shared<Allocation>();
    for (uint32_t index = 0; index < allocation->textures.size(); ++index) {
        auto texture = engine::GpuTexture::create(
            m_device,
            {.format = rojoRHI::Format::R32Float,
             .extent = {layout.width, layout.height, 1},
             .mips = layout.levelCount,
             .usage = rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::Storage,
             .label = std::format("lmx.render.hzb{}", index)},
            {}, {.storageMips = true});
        if (!texture)
            return std::unexpected(texture.error());
        allocation->textures[index] = std::move(*texture);
    }
    prepared->replacement = std::move(allocation);
    return PreparedHzbResize(std::move(prepared));
}

//======================================================================================================================
bool HzbStage::resizeCurrent(const PreparedHzbResize& prepared) const noexcept {
    if (!prepared.m_impl)
        return false;
    const auto& candidate = *prepared.m_impl;
    if (candidate.owner != this || candidate.context != &m_gpu || candidate.consumed ||
        candidate.baseGeneration != m_allocationGeneration ||
        candidate.baseAllocation != m_allocation.get())
        return false;
    const bool initialized = m_allocation && m_allocation->textures[0] && m_allocation->textures[1];
    if (candidate.initialized != initialized ||
        (m_allocation && (!initialized || m_allocation->generation != m_allocationGeneration)))
        return false;
    const bool sameExtent =
        candidate.outputWidth == m_outputWidth && candidate.outputHeight == m_outputHeight;
    if (candidate.noOp)
        return initialized && sameExtent && !candidate.replacement &&
               candidate.layout.width == m_layout.width &&
               candidate.layout.height == m_layout.height &&
               candidate.layout.levelCount == m_layout.levelCount &&
               candidate.layout.bytes == m_layout.bytes;
    return (!initialized || !sameExtent) && candidate.replacement &&
           candidate.replacement->textures[0] && candidate.replacement->textures[1];
}

//======================================================================================================================
std::array<const engine::GpuTexture*, 2>
HzbStage::resizeTextures(const PreparedHzbResize& prepared) const {
    LMX_ASSERT(resizeCurrent(prepared), "HZB resize candidate must be current for its owner");
    const auto& replacement = prepared.m_impl->replacement;
    if (!replacement)
        return {};
    return {&replacement->textures[0], &replacement->textures[1]};
}

//======================================================================================================================
void HzbStage::commitResize(PreparedHzbResize&& prepared, TextureStateTable& states) noexcept {
    LMX_ASSERT(resizeCurrent(prepared), "HZB resize candidate must be current for its owner");
    auto candidate = std::move(prepared.m_impl);
    candidate->consumed = true;
    if (candidate->noOp)
        return;
    if (m_allocation)
        for (const auto& replaced : m_allocation->textures)
            states.forget(replaced.rhi());
    candidate->replacement->generation = ++m_allocationGeneration;
    m_graphBindings.clear();
    m_allocation = std::move(candidate->replacement);
    m_sources = {};
    m_declaredSerials = {};
    m_layout = candidate->layout;
    m_outputWidth = candidate->outputWidth;
    m_outputHeight = candidate->outputHeight;
    m_next = 0;
    m_lastBuilt = 1;
}

//======================================================================================================================
lmx::Result<void> HzbStage::resize(uint32_t outputWidth, uint32_t outputHeight,
                                   TextureStateTable& states) {
    auto prepared = prepareResize(outputWidth, outputHeight);
    if (!prepared)
        return std::unexpected(prepared.error());
    commitResize(std::move(*prepared), states);
    return {};
}

//======================================================================================================================
std::shared_ptr<HzbGraphBinding> HzbStage::beginGraph(RenderGraph& graph,
                                                      rojoRHI::CommandList& commands) {
    LMX_ASSERT(m_gpu.inFrame(), "pyramid graph requires its creating context's open frame");
    std::erase_if(m_graphBindings, [](const auto& borrow) { return borrow.binding.expired(); });
    for (const auto& borrow : m_graphBindings)
        if (borrow.graph == &graph && borrow.commands == &commands &&
            borrow.frame == m_gpu.frameNumber())
            if (auto binding = borrow.binding.lock())
                return binding;
    auto binding = std::shared_ptr<HzbGraphBinding>(new HzbGraphBinding(*this, graph, commands));
    m_graphBindings.push_back({&graph, &commands, m_gpu.frameNumber(), binding});
    return binding;
}

//======================================================================================================================
std::shared_ptr<HzbHistoryBinding> HzbStage::importPrevious(HzbGraphBinding& graph) {
    LMX_ASSERT(m_allocation, "HZB must be resized before import");
    const auto pyramid = graph.importSlot(m_lastBuilt);
    if (m_sources[m_lastBuilt].built)
        graph.graph().setHzbObserverFinalUse(pyramid);
    return std::shared_ptr<HzbHistoryBinding>(
        new HzbHistoryBinding(graph.shared_from_this(), m_lastBuilt, m_sources[m_lastBuilt]));
}

//======================================================================================================================
GraphTexture HzbStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                               GraphTexture depth, HzbSource source) {
    auto binding = beginGraph(graph, commands);
    return declare(*binding, depth, source);
}

//======================================================================================================================
GraphTexture HzbStage::declare(HzbGraphBinding& binding, GraphTexture depth, HzbSource source) {
    LMX_ASSERT(m_allocation, "HZB must be resized before build");
    LMX_ASSERT(source.activeWidth > 0 && source.activeHeight > 0 &&
                   source.activeWidth <= m_outputWidth && source.activeHeight <= m_outputHeight,
               "HZB source active rectangle exceeds output allocation");
    auto& graph = binding.graph();
    auto& commands = binding.commands();
    const uint32_t slot = m_next;
    const uint64_t serial = ++m_declarationSerial;
    const uint64_t actualFrame = m_gpu.frameNumber();
    source.outputWidth = m_outputWidth;
    source.outputHeight = m_outputHeight;
    source.levelCount = m_layout.levelCount;
    source.built = true;
    auto& states = graph.textureStates();
    GraphTexture pyramid = binding.importSlot(slot);
    graph.setFinalUse(pyramid, TextureUse::ShaderRead);
    const auto allocation = m_allocation;
    const auto graphLease = binding.shared_from_this();
    for (uint32_t level = 0; level < m_layout.levelCount; ++level) {
        const bool fromDepth = level == 0;
        const GraphTexture input = fromDepth ? depth : pyramid;
        const rojoRHI::SubresourceRange inputRange{.baseMip = fromDepth ? 0 : level - 1,
                                                   .mipCount = 1};
        const rojoRHI::SubresourceRange outputRange{.baseMip = level, .mipCount = 1};
        const HzbReduceParams params =
            buildHzbReduceParams(source.activeWidth, source.activeHeight, level);
        ComputePassDesc desc;
        desc.shaderTextureReads.emplace_back(input, inputRange);
        desc.textureWrites.emplace_back(pyramid, outputRange);
        graph.addComputePass(
            std::format("lmx.pass.hzb.level{}", level), std::move(desc),
            [this, &commands, input, pyramid, inputRange, outputRange, params, allocation, slot,
             serial, actualFrame, source, level, &states,
             graphLease](const PassResources& resources) {
                (void)graphLease;
                LMX_ASSERT(m_gpu.inFrame() && m_gpu.frameNumber() == actualFrame,
                           "pyramid recording requires its declaring context frame");
                commands.setPipeline(m_reducePipeline.get());
                const auto paramsAddress = frameData(commands, params);
                engine::registerFrameRoot("HzbReduceParams", paramsAddress);
                engine::registerFrameRoot(
                    "HzbReduceRoot",
                    commands.pushRoot(
                        HzbReduceRoot{paramsAddress, static_cast<uint32_t>(resources.view(input)),
                                      static_cast<uint32_t>(
                                          resources.storageView(pyramid, outputRange.baseMip))}));
#ifndef NDEBUG
                const std::array<rojoRHI::ResourceAccess, 2> accesses{
                    {{.texture = resources.texture(input).texture,
                      .range = inputRange,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderRead},
                     {.texture = resources.texture(pyramid).texture,
                      .range = outputRange,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderWrite}}};
                commands.declareAccess(accesses);
#endif
                const auto groups =
                    dispatchGroups2D(params.destinationWidth, params.destinationHeight);
                commands.dispatch(groups[0], groups[1], 1);
                auto& content = allocation->contents[slot];
                if (level == 0 || content.serial != serial) {
                    content = {.owner = &m_gpu,
                               .commands = &commands,
                               .actualFrame = actualFrame,
                               .generation = allocation->generation,
                               .serial = serial,
                               .source = source};
                }
                content.complete = false;
                if (content.recordedLevels == level)
                    ++content.recordedLevels;
                const auto texture = allocation->textures[slot].ref();
                states.seedPhysical(texture.texture, texture.mips, texture.layers,
                                    texture.initialLayout);
                if (level != 0)
                    states.set(texture.texture, inputRange,
                               {rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderRead,
                                rojoRHI::Layout::ShaderRead});
                states.set(texture.texture, outputRange,
                           {rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                            rojoRHI::Layout::ShaderWrite});
            });
        pyramid = nextVersion(pyramid);
        binding.advance(slot, pyramid);
    }
    const GraphBuffer publication = binding.importPublication();
    ComputePassDesc publish;
    publish.shaderTextureReads.push_back(pyramid);
    publish.bufferWrites.push_back(publication);
    const uint32_t levels = m_layout.levelCount;
    graph.addComputePass(
        "lmx.pass.hzb.publish", std::move(publish),
        [this, &commands, pyramid, publication, levels, allocation, slot, serial, actualFrame,
         &states, graphLease](const PassResources& resources) {
            (void)graphLease;
            LMX_ASSERT(m_gpu.inFrame() && m_gpu.frameNumber() == actualFrame,
                       "pyramid publication requires its declaring context frame");
            commands.setPipeline(m_publishPipeline.get());
            engine::registerFrameRoot("HzbPublishRoot",
                                      commands.pushRoot(HzbPublishRoot{
                                          resources.address(publication),
                                          static_cast<uint32_t>(resources.view(pyramid)), levels}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 2> accesses{
                {{.texture = resources.texture(pyramid).texture,
                  .range = {0, levels, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.buffer = resources.buffer(publication),
                  .size = sizeof(float),
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(1, 1, 1);
            auto& content = allocation->contents[slot];
            content.complete = content.owner == &m_gpu && content.commands == &commands &&
                               content.actualFrame == actualFrame && content.serial == serial &&
                               content.generation == allocation->generation &&
                               content.recordedLevels == levels;
            const auto texture = allocation->textures[slot].ref();
            states.seedPhysical(texture.texture, texture.mips, texture.layers,
                                texture.initialLayout);
            states.set(texture.texture, {0, levels, 0, 1},
                       {rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderRead,
                        rojoRHI::Layout::ShaderRead});
            m_published = true;
            m_publishPhysical = {rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                                 rojoRHI::Layout::Undefined};
        });
    graph.exportBuffer(nextVersion(publication));
    binding.advancePublication(nextVersion(publication));
    graph.exportTexture(pyramid);
    m_sources[slot] = source;
    m_declaredSerials[slot] = serial;
    m_lastBuilt = slot;
    m_next ^= 1;
    return pyramid;
}
} // namespace lmx::render
