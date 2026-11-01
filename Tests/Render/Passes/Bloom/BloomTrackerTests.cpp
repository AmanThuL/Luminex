//----------------------------------------------------------------------------------------------------------------------
/// @file BloomTrackerTests.cpp
/// @brief Exercises production bloom mip, alias and placement-cache frontiers.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuOwned.h"
#include "Render/Passes/Bloom/BloomStage.h"
#include "Support/ExposureBloomTestSupport.h"
#include "Support/OracleRoots.h"
#include "Support/TestGpu.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifndef NDEBUG
namespace {

namespace render = lmx::render;
using S = rojoRHI::Stage;
using A = rojoRHI::Access;
using L = rojoRHI::Layout;

constexpr rojoRHI::SubresourceRange kMip0{0, 1, 0, 1};
constexpr S kRasterShaderStages = S::VertexShader | S::FragmentShader;
constexpr S kAllShaderStages = kRasterShaderStages | S::ComputeShader;
constexpr render::TextureState kStorageWrite{S::ComputeShader, A::ShaderWrite, L::ShaderWrite};
constexpr render::TextureState kStorageRead{S::ComputeShader, A::ShaderRead, L::ShaderWrite};
constexpr render::TextureState kRasterRead{kRasterShaderStages, A::ShaderRead, L::ShaderRead};
constexpr render::TextureState kSceneRead{S::ComputeShader, A::ShaderRead, L::ShaderRead};
constexpr render::TextureState kOutputWrite{
    S::ColorAttachment, A::ColorAttachmentRead | A::ColorAttachmentWrite, L::ColorAttachment};

struct BloomOwnerDrain {
    lmx::engine::GpuContext& gpu;
    bool drained = false;
    ~BloomOwnerDrain();
};

//======================================================================================================================
BloomOwnerDrain::~BloomOwnerDrain() {
    if (!drained)
        gpu.waitIdle();
}

struct BloomPath {
    uint32_t sceneWidth;
    uint32_t sceneHeight;
    uint32_t bloomWidth;
    uint32_t bloomHeight;
    uint32_t mips;
};

struct BloomSample {
    uint32_t calls = 0;
    lmx::engine::TextureRef source;
    uint32_t rootView = 0;
};

struct BloomCacheSnapshot {
    uint64_t frame;
    render::TransientLayout layout;
    std::array<BloomSample, 2> samples;
    uint64_t heapBytes;
};

struct ExpectedBloomBoundary {
    uint32_t resource;
    uint32_t mip;
    render::TextureState from;
    render::TextureState to;
    bool alias = false;
};

//======================================================================================================================
std::vector<render::LoweredTextureBarrier> bloomBarriersBefore(const render::LoweredFrame& frame,
                                                               uint32_t pass) {
    std::vector<render::LoweredTextureBarrier> pending;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<render::BarriersOp>(&op)) {
            for (const auto& barrier : batch->barriers) {
                if (const auto* texture = std::get_if<render::LoweredTextureBarrier>(&barrier))
                    pending.push_back(*texture);
            }
        }
        if (const auto* run = std::get_if<render::RunOp>(&op)) {
            if (run->pass == pass)
                return pending;
            pending.clear();
        }
    }
    return {};
}

//======================================================================================================================
void requireBloomBoundaries(const render::LoweredFrame& frame, uint32_t pass, uint32_t chain,
                            std::optional<uint32_t> blur,
                            std::initializer_list<ExpectedBloomBoundary> expected) {
    auto actual = bloomBarriersBefore(frame, pass);
    std::erase_if(actual, [=](const render::LoweredTextureBarrier& barrier) {
        return barrier.resource != chain && (!blur || barrier.resource != *blur);
    });
    REQUIRE(actual.size() == expected.size());
    for (const auto& boundary : expected) {
        CAPTURE(pass, boundary.resource, boundary.mip);
        const auto found =
            std::ranges::find_if(actual, [&](const render::LoweredTextureBarrier& barrier) {
                return barrier.resource == boundary.resource &&
                       barrier.range.baseMip == boundary.mip;
            });
        REQUIRE(found != actual.end());
        REQUIRE(found->range.mipCount == 1);
        REQUIRE(found->range.baseLayer == 0);
        REQUIRE(found->range.layerCount == 1);
        REQUIRE(found->from == boundary.from);
        REQUIRE(found->to == boundary.to);
        REQUIRE(found->alias == boundary.alias);
    }
}

//======================================================================================================================
void requireBloomUse(const render::DebugPass& pass, uint32_t resource, uint32_t version,
                     uint32_t mip, render::UseRole role) {
    const auto count = std::ranges::count_if(pass.uses, [=](const render::DebugUse& use) {
        return use.resource == resource && use.version == version && use.role == role &&
               use.range.baseMip == mip && use.range.mipCount == 1 && use.range.baseLayer == 0 &&
               use.range.layerCount == 1;
    });
    REQUIRE(count == 1);
}

//======================================================================================================================
uint32_t bloomWrittenResource(const render::DebugPass& threshold) {
    REQUIRE(threshold.label == "lmx.pass.bloom.threshold");
    const auto count =
        std::ranges::count(threshold.uses, render::UseRole::Write, &render::DebugUse::role);
    REQUIRE(count == 1);
    const auto found =
        std::ranges::find(threshold.uses, render::UseRole::Write, &render::DebugUse::role);
    REQUIRE(found != threshold.uses.end());
    REQUIRE(found->version == 0);
    return found->resource;
}

//======================================================================================================================
const render::TransientPlacementDesc& bloomPlacement(const render::LoweredFrame& frame,
                                                     uint32_t resource) {
    const auto found = std::ranges::find(frame.transientLayout.placements, resource,
                                         &render::TransientPlacementDesc::resource);
    REQUIRE(found != frame.transientLayout.placements.end());
    return *found;
}

//======================================================================================================================
void requireBloomDescriptor(const render::LoweredFrame& frame, uint32_t resource,
                            const BloomPath& path, bool blur) {
    const auto& placement = bloomPlacement(frame, resource);
    const auto* desc = std::get_if<render::TransientTextureDesc>(&placement.desc);
    REQUIRE(desc != nullptr);
    REQUIRE(desc->width == path.bloomWidth);
    REQUIRE(desc->height == path.bloomHeight);
    REQUIRE(desc->format == rojoRHI::Format::RGBA16Float);
    REQUIRE(desc->kind == rojoRHI::TextureType::Tex2D);
    REQUIRE(desc->mipLevels == (blur ? path.mips - 1 : path.mips));
    REQUIRE_FALSE(desc->renderTarget);
    REQUIRE(desc->sampled == (blur || path.mips == 1));
    REQUIRE(desc->storageRead);
    REQUIRE(desc->storageWrite);
    REQUIRE(placement.footprint.size > 0);
    REQUIRE(placement.footprint.alignment > 0);
    REQUIRE(placement.offset % placement.footprint.alignment == 0);
}

//======================================================================================================================
void requireBloomAlias(const render::LoweredFrame& frame, uint32_t first, uint32_t second) {
    const auto& a = bloomPlacement(frame, first);
    const auto& b = bloomPlacement(frame, second);
    REQUIRE(a.resource != b.resource);
    REQUIRE(a.desc == b.desc);
    REQUIRE(a.footprint == b.footprint);
    REQUIRE(a.offset == b.offset);
    REQUIRE(a.label == b.label);
    const auto& debug = frame.record.debug;
    const auto earlier =
        std::ranges::find(debug.transients, first, &render::DebugTransient::resource);
    const auto later =
        std::ranges::find(debug.transients, second, &render::DebugTransient::resource);
    REQUIRE(earlier != debug.transients.end());
    REQUIRE(later != debug.transients.end());
    REQUIRE(earlier->used);
    REQUIRE(later->used);
    REQUIRE_FALSE(earlier->aliases);
    REQUIRE(later->aliases);
    REQUIRE(earlier->offset == a.offset);
    REQUIRE(later->offset == b.offset);
    REQUIRE(earlier->size == later->size);
    REQUIRE(earlier->alignment == later->alignment);
    const auto earlierEnd = std::ranges::find(debug.schedule.passes, earlier->lastPass);
    const auto laterStart = std::ranges::find(debug.schedule.passes, later->firstPass);
    REQUIRE(earlierEnd != debug.schedule.passes.end());
    REQUIRE(laterStart != debug.schedule.passes.end());
    REQUIRE(earlierEnd < laterStart);
    const auto alias =
        std::ranges::find_if(debug.transitions, [=](const render::DebugTransition& transition) {
            return transition.resource == second && transition.aliasedFrom == first;
        });
    REQUIRE(alias != debug.transitions.end());
    REQUIRE(alias->beforePass == later->firstPass);
}

//======================================================================================================================
void requireBloomPersistentState(const render::TextureStateTable& states, rojoRHI::Texture texture,
                                 const render::TextureState& expected, render::TextureUse logical) {
    const auto ranges = states.physicalRanges(texture, kMip0);
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges.front().range.baseMip == 0);
    REQUIRE(ranges.front().range.mipCount == 1);
    REQUIRE(ranges.front().range.baseLayer == 0);
    REQUIRE(ranges.front().range.layerCount == 1);
    REQUIRE(ranges.front().state == expected);
    REQUIRE(states.find(texture, kMip0) == expected);
    REQUIRE(states.logicalUse(texture) == logical);
}

//======================================================================================================================
void requireBloomSample(const BloomSample& sample, const BloomPath& path) {
    REQUIRE(sample.calls == 1);
    REQUIRE(sample.source.texture.bits != 0);
    REQUIRE(sample.source.view != lmx::engine::kNoView);
    REQUIRE(sample.rootView == static_cast<uint32_t>(sample.source.view));
    REQUIRE(sample.source.extent.width == path.bloomWidth);
    REQUIRE(sample.source.extent.height == path.bloomHeight);
    REQUIRE(sample.source.format == rojoRHI::Format::RGBA16Float);
    REQUIRE(sample.source.mips == (path.mips == 1 ? 1u : path.mips - 1));
    REQUIRE(sample.source.layers == 1);
}

//======================================================================================================================
void requireBloomCachedSample(const BloomSample& before, const BloomSample& after) {
    REQUIRE(before.source.texture == after.source.texture);
    REQUIRE(before.source.view == after.source.view);
    REQUIRE(before.source.extent.width == after.source.extent.width);
    REQUIRE(before.source.extent.height == after.source.extent.height);
    REQUIRE(before.source.format == after.source.format);
    REQUIRE(before.source.mips == after.source.mips);
    REQUIRE(before.source.layers == after.source.layers);
    REQUIRE(before.source.initialLayout == after.source.initialLayout);
    REQUIRE(before.rootView == after.rootView);
}

//======================================================================================================================
void requireBloomChain(const render::LoweredFrame& frame, uint32_t firstPass, uint32_t chain,
                       std::optional<uint32_t> blur, bool aliases, const BloomPath& path,
                       render::GraphTexture returned, render::GraphTexture target,
                       uint32_t sampleNumber) {
    const auto& passes = frame.record.debug.passes;
    const S chainPrior = path.mips == 1 ? kRasterShaderStages : S::ComputeShader;
    const render::TextureState chainInitial{aliases ? chainPrior : S::None, A::None, L::Undefined};
    const render::TextureState blurInitial{aliases ? kAllShaderStages : S::None, A::None,
                                           L::Undefined};
    requireBloomUse(passes[firstPass], chain, 0, 0, render::UseRole::Write);
    requireBloomBoundaries(frame, firstPass, chain, blur,
                           {{chain, 0, chainInitial, kStorageWrite, aliases}});
    if (path.mips == 1) {
        REQUIRE_FALSE(blur.has_value());
        REQUIRE(returned.index == chain);
        REQUIRE(returned.version == 1);
    } else {
        REQUIRE(path.mips == 3);
        REQUIRE(blur.has_value());
        REQUIRE(returned.index == *blur);
        REQUIRE(returned.version == 2);
        const auto down0 = firstPass + 1;
        const auto down1 = firstPass + 2;
        const auto up1 = firstPass + 3;
        const auto up0 = firstPass + 4;
        REQUIRE(passes[down0].label == "lmx.pass.bloom.downsample0");
        REQUIRE(passes[down1].label == "lmx.pass.bloom.downsample1");
        REQUIRE(passes[up1].label == "lmx.pass.bloom.upsample1");
        REQUIRE(passes[up0].label == "lmx.pass.bloom.upsample0");
        requireBloomUse(passes[down0], chain, 1, 0, render::UseRole::Read);
        requireBloomUse(passes[down0], chain, 1, 1, render::UseRole::Write);
        requireBloomUse(passes[down1], chain, 2, 1, render::UseRole::Read);
        requireBloomUse(passes[down1], chain, 2, 2, render::UseRole::Write);
        requireBloomUse(passes[up1], chain, 3, 1, render::UseRole::Read);
        requireBloomUse(passes[up1], chain, 3, 2, render::UseRole::Read);
        requireBloomUse(passes[up1], *blur, 0, 1, render::UseRole::Write);
        requireBloomUse(passes[up0], chain, 3, 0, render::UseRole::Read);
        requireBloomUse(passes[up0], *blur, 1, 1, render::UseRole::Read);
        requireBloomUse(passes[up0], *blur, 1, 0, render::UseRole::Write);
        requireBloomBoundaries(frame, down0, chain, blur,
                               {{chain, 0, kStorageWrite, kStorageRead},
                                {chain, 1, chainInitial, kStorageWrite, aliases}});
        requireBloomBoundaries(frame, down1, chain, blur,
                               {{chain, 1, kStorageWrite, kStorageRead},
                                {chain, 2, chainInitial, kStorageWrite, aliases}});
        requireBloomBoundaries(frame, up1, chain, blur,
                               {{chain, 2, kStorageWrite, kStorageRead},
                                {*blur, 1, blurInitial, kStorageWrite, aliases}});
        requireBloomBoundaries(frame, up0, chain, blur,
                               {{*blur, 0, blurInitial, kStorageWrite, aliases},
                                {*blur, 1, kStorageWrite, kStorageRead}});
    }

    const uint32_t samplePass = firstPass + (path.mips == 1 ? 1u : 5u);
    const auto& sample = passes[samplePass];
    REQUIRE(sample.kind == render::PassKind::Raster);
    REQUIRE(sample.label == (sampleNumber == 0 ? "lmx.test.bloomTracker.sample0"
                                               : "lmx.test.bloomTracker.sample1"));
    REQUIRE(sample.uses.size() == 2);
    requireBloomUse(sample, returned.index, returned.version, 0, render::UseRole::Read);
    const auto targetUse = std::ranges::find_if(sample.uses, [=](const render::DebugUse& use) {
        return use.resource == target.index && use.version == sampleNumber &&
               use.role == render::UseRole::ColorAttachment;
    });
    REQUIRE(targetUse != sample.uses.end());
    // Raster graph frontiers include both shader stages; the actual Load is fragment-only.
    requireBloomBoundaries(frame, samplePass, chain, blur,
                           {{returned.index, 0, kStorageWrite, kRasterRead}});
}

//======================================================================================================================
void runBloomTrackerPath(const BloomPath& path) {
    CAPTURE(path.sceneWidth, path.sceneHeight);
    auto gpu = createTestGpu({.validation = true, .framesInFlight = 3});
    auto stageResult = render::BloomStage::create(*gpu);
    INFO(errorOf(stageResult));
    REQUIRE(stageResult.has_value());
    auto stage = std::move(*stageResult);
    auto sampleLibrary = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::BGRA8Unorm}}};
    auto samplePipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(rojoRHI::GraphicsPipelineDesc{
                           .vertex = {sampleLibrary->get(), "vertexMain"},
                           .fragment = {sampleLibrary->get(), "fragmentMain"},
                           .raster = {.cull = rojoRHI::CullMode::None},
                           .depthStencil = {.depthTest = false,
                                            .depthWrite = false,
                                            .depthCompare = rojoRHI::CompareOp::Always},
                           .colors = colors,
                           .label = "lmx.test.bloomTracker.samplePipeline"}));
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());
    const auto pipeline = samplePipeline->get();
    const std::vector<uint16_t> pixels(size_t{path.sceneWidth} * path.sceneHeight * 4,
                                       lmx::test::halfPow2(3));
    auto scene = lmx::test::makeSceneColorTexture(*gpu, path.sceneWidth, path.sceneHeight, pixels,
                                                  "lmx.test.bloomTracker.scene");
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());
    auto output = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.format = rojoRHI::Format::BGRA8Unorm,
                                                   .extent = {path.bloomWidth, path.bloomHeight, 1},
                                                   .usage = rojoRHI::TextureUsage::RenderTarget,
                                                   .label = "lmx.test.bloomTracker.output"});
    INFO(errorOf(output));
    REQUIRE(output.has_value());
    render::TransientPool pool(*gpu);
    render::TextureStateTable states;
    BloomOwnerDrain ownerDrain{*gpu};
    constexpr std::array<bool, 6> kEnabled{true, false, true, true, false, true};
    constexpr std::array<std::string_view, 2> kSampleLabels{"lmx.test.bloomTracker.sample0",
                                                            "lmx.test.bloomTracker.sample1"};
    std::array<std::optional<BloomCacheSnapshot>, render::kTransientFrameSlots> cache;
    const uint64_t startingFrame = gpu->frameNumber();
    uint64_t lastSubmitted = startingFrame;
    uint32_t cacheHits = 0;

    for (uint32_t ordinal = 0; ordinal < kEnabled.size(); ++ordinal) {
        CAPTURE(ordinal);
        const bool enabled = kEnabled[ordinal];
        const auto previousSceneLogical = states.logicalUse(scene->rhi());
        const auto previousScenePhysical = states.find(scene->rhi(), kMip0);
        const auto previousOutputLogical = states.logicalUse(output->rhi());
        const auto previousOutputPhysical = states.find(output->rhi(), kMip0);
        const auto previousSceneRanges = states.physicalRanges(scene->rhi(), kMip0);
        const auto previousOutputRanges = states.physicalRanges(output->rhi(), kMip0);
        auto frame = gpu->beginFrame();
        auto& commands = frame.commands;
        pool.beginFrame();
        render::RenderGraph graph(pool);
        graph.bindTextureStates(states);
        const auto source = graph.importPersistentTexture(*scene, "lmx.test.bloomTracker.scene");
        const auto target = graph.importPersistentTexture(*output, "lmx.test.bloomTracker.output");
        auto targetVersion = target;
        std::array<render::GraphTexture, 2> returned;
        std::array<BloomSample, 2> samples{};
        for (uint32_t call = 0; call < returned.size(); ++call) {
            returned[call] = stage->declare(graph, commands,
                                            {.displayInput = source,
                                             .sceneWidth = path.sceneWidth,
                                             .sceneHeight = path.sceneHeight,
                                             .bloomThreshold = 1.0f});
            if (!enabled)
                continue;
            render::PassDesc sampleDesc{.color = render::ColorAttachment{.handle = targetVersion}};
            sampleDesc.textureReads.emplace_back(returned[call], kMip0);
            graph.addPass(kSampleLabels[call], std::move(sampleDesc),
                          [&commands, &samples, call, sampled = returned[call],
                           pipeline](const render::PassResources& resources) {
                              const auto texture = resources.texture(sampled);
                              const auto view = resources.view(sampled);
                              ++samples[call].calls;
                              samples[call].source = texture;
                              samples[call].rootView = static_cast<uint32_t>(view);
                              commands.setPipeline(pipeline);
                              commands.pushRoot(lmx::test::FullscreenSampleRoot{
                                  .source = samples[call].rootView});
                              const rojoRHI::ResourceAccess access{.texture = texture.texture,
                                                                   .range = kMip0,
                                                                   .stage = S::FragmentShader,
                                                                   .access = A::ShaderRead};
                              commands.declareAccess(std::span{&access, 1});
                              commands.draw(3);
                          });
            targetVersion = render::nextVersion(targetVersion);
        }
        if (enabled)
            graph.exportTexture(targetVersion);
        auto lowered = graph.lower(frame.number);
        if (!lowered)
            gpu->submitAndEndFrame(frame);
        INFO(errorOf(lowered));
        REQUIRE(lowered.has_value());
        const auto record = graph.execute(frame.commands, frame.number);
        gpu->submitAndEndFrame(frame);

        REQUIRE(frame.number == lastSubmitted + 1);
        lastSubmitted = frame.number;
        REQUIRE(record.frameId == frame.number);
        REQUIRE(record.debug.schedule.passes == lowered->record.debug.schedule.passes);
        REQUIRE(pool.frameNumber() == frame.number);
        REQUIRE(pool.retiringGenerationCount() == 0);
        REQUIRE(pool.liveGenerationCount() <= render::kTransientFrameSlots);
        REQUIRE(record.debug.transients.size() == (path.mips == 1 ? 2u : 4u));
        if (!enabled) {
            REQUIRE(record.debug.schedule.passes.empty());
            REQUIRE(record.debug.sinks.empty());
            REQUIRE(lowered->transientLayout.placements.empty());
            REQUIRE(lowered->transientLayout.bytes == 0);
            REQUIRE(record.debug.memory.highWater == 0);
            REQUIRE(record.debug.memory.requested == 0);
            REQUIRE(lowered->ops.size() == 2);
            REQUIRE(lowered->physicalStates.empty());
            for (const auto& pass : record.debug.passes)
                REQUIRE(pass.cullReason == render::CullReason::NoSinkReachesIt);
            for (const auto& transient : record.debug.transients)
                REQUIRE_FALSE(transient.used);
            for (const auto& sample : samples)
                REQUIRE(sample.calls == 0);
            REQUIRE(previousSceneLogical.has_value());
            REQUIRE(previousScenePhysical.has_value());
            REQUIRE(previousOutputLogical.has_value());
            REQUIRE(previousOutputPhysical.has_value());
            REQUIRE(previousSceneRanges.size() == 1);
            REQUIRE(previousOutputRanges.size() == 1);
            REQUIRE(states.logicalUse(scene->rhi()) == previousSceneLogical);
            REQUIRE(states.find(scene->rhi(), kMip0) == previousScenePhysical);
            REQUIRE(states.logicalUse(output->rhi()) == previousOutputLogical);
            REQUIRE(states.find(output->rhi(), kMip0) == previousOutputPhysical);
            requireBloomPersistentState(states, scene->rhi(), *previousScenePhysical,
                                        *previousSceneLogical);
            requireBloomPersistentState(states, output->rhi(), *previousOutputPhysical,
                                        *previousOutputLogical);
            continue;
        }

        const uint32_t passesPerCall = path.mips == 1 ? 2u : 6u;
        REQUIRE(record.debug.passes.size() == 2 * passesPerCall);
        std::vector<uint32_t> expectedSchedule(2 * passesPerCall);
        std::iota(expectedSchedule.begin(), expectedSchedule.end(), 0u);
        REQUIRE(record.debug.schedule.passes == expectedSchedule);
        REQUIRE(record.debug.sinks.size() == 1);
        REQUIRE(record.debug.sinks.front().kind == render::SinkKind::Export);
        REQUIRE(record.debug.sinks.front().resource == target.index);
        REQUIRE(record.debug.sinks.front().version == 2);
        REQUIRE(lowered->transientLayout.placements.size() == record.debug.transients.size());
        REQUIRE(lowered->transientLayout.bytes > 0);
        REQUIRE(record.debug.memory.aliasSavings > 0);
        std::array<uint32_t, 2> chains;
        for (uint32_t call = 0; call < returned.size(); ++call) {
            const uint32_t firstPass = call * passesPerCall;
            chains[call] = bloomWrittenResource(record.debug.passes[firstPass]);
            const std::optional<uint32_t> blur =
                path.mips == 1 ? std::nullopt : std::optional<uint32_t>{returned[call].index};
            requireBloomDescriptor(*lowered, chains[call], path, false);
            if (blur)
                requireBloomDescriptor(*lowered, *blur, path, true);
            requireBloomChain(*lowered, firstPass, chains[call], blur, call == 1, path,
                              returned[call], target, call);
            requireBloomSample(samples[call], path);
            for (uint32_t index = firstPass; index < firstPass + passesPerCall; ++index)
                REQUIRE_FALSE(record.debug.passes[index].cullReason.has_value());
        }
        requireBloomAlias(*lowered, chains[0], chains[1]);
        if (path.mips != 1)
            requireBloomAlias(*lowered, returned[0].index, returned[1].index);
        REQUIRE(samples[0].source.texture != samples[1].source.texture);
        requireBloomPersistentState(states, scene->rhi(), kSceneRead,
                                    render::TextureUse::ShaderRead);
        requireBloomPersistentState(states, output->rhi(), kOutputWrite,
                                    render::TextureUse::RenderTarget);

        const auto slot = frame.number % render::kTransientFrameSlots;
        if (cache[slot]) {
            const auto& previous = *cache[slot];
            REQUIRE(frame.number - previous.frame == render::kTransientFrameSlots);
            REQUIRE(lowered->transientLayout == previous.layout);
            REQUIRE(pool.heapBytes() == previous.heapBytes);
            for (uint32_t call = 0; call < samples.size(); ++call)
                requireBloomCachedSample(previous.samples[call], samples[call]);
            ++cacheHits;
        }
        cache[slot] =
            BloomCacheSnapshot{frame.number, lowered->transientLayout, samples, pool.heapBytes()};
    }

    gpu->waitIdle();
    ownerDrain.drained = true;
    REQUIRE(cacheHits == 2);
    REQUIRE(lastSubmitted == startingFrame + kEnabled.size());
    REQUIRE(gpu->frameNumber() == lastSubmitted);
    REQUIRE(gpu->completedFrame() == lastSubmitted);
}

} // namespace
#endif

//======================================================================================================================
TEST_CASE("bloom toggles preserve odd and tiny mip frontiers under the tracker",
          "[render][device][tracker]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    constexpr std::array<BloomPath, 2> kPaths{{{17, 11, 9, 6, 3}, {3, 1, 2, 1, 1}}};
    for (const auto& path : kPaths)
        runBloomTrackerPath(path);
#endif
}
