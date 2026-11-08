//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureTrackerTests.cpp
/// @brief Exercises production exposure feedback frontiers across paced frames.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Exposure/ExposureStage.h"
#include "Support/ExposureBloomTestSupport.h"
#include "Support/TestGpu.h"
#include "Support/TrackerFrameSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
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

constexpr S kAllShaderStages = S::VertexShader | S::FragmentShader | S::ComputeShader;
constexpr A kShaderReadWrite = A::ShaderRead | A::ShaderWrite;
constexpr rojoRHI::SubresourceRange kMip0{0, 1, 0, 1};

struct ExposureOwnerDrain {
    lmx::engine::GpuContext& gpu;
    bool drained = false;
    ~ExposureOwnerDrain();
};

//======================================================================================================================
ExposureOwnerDrain::~ExposureOwnerDrain() {
    if (!drained)
        gpu.waitIdle();
}

struct ExposureTransition {
    bool automatic;
    bool reset;
};

//======================================================================================================================
uint32_t exposurePassIndex(const render::CompiledFrameRecord& record, std::string_view label) {
    const auto found = std::ranges::find(record.debug.passes, label, &render::DebugPass::label);
    REQUIRE(found != record.debug.passes.end());
    return static_cast<uint32_t>(found - record.debug.passes.begin());
}

//======================================================================================================================
uint32_t exposureResourceIndex(const render::CompiledFrameRecord& record, std::string_view name) {
    const auto found =
        std::ranges::find(record.debug.resources, name, &render::DebugResource::name);
    REQUIRE(found != record.debug.resources.end());
    return static_cast<uint32_t>(found - record.debug.resources.begin());
}

//======================================================================================================================
std::vector<render::LoweredBufferBarrier> exposureBarriersBefore(const render::LoweredFrame& frame,
                                                                 uint32_t pass) {
    std::vector<render::LoweredBufferBarrier> pending;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<render::BarriersOp>(&op)) {
            for (const auto& barrier : batch->barriers) {
                if (const auto* buffer = std::get_if<render::LoweredBufferBarrier>(&barrier))
                    pending.push_back(*buffer);
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
void requireExposureBoundary(const render::LoweredFrame& frame, uint32_t pass, uint32_t resource,
                             S fromStage, A fromAccess, S toStage, A toAccess) {
    const auto barriers = exposureBarriersBefore(frame, pass);
    const auto found =
        std::ranges::find(barriers, resource, &render::LoweredBufferBarrier::resource);
    REQUIRE(found != barriers.end());
    REQUIRE_FALSE(found->alias);
    REQUIRE(found->from.stage == fromStage);
    REQUIRE(found->from.access == fromAccess);
    REQUIRE(found->from.layout == L::Undefined);
    REQUIRE(found->to.stage == toStage);
    REQUIRE(found->to.access == toAccess);
    REQUIRE(found->to.layout == L::Undefined);
}

//======================================================================================================================
void requireExposureUse(const render::DebugPass& pass, uint32_t resource, uint32_t version,
                        render::UseRole role) {
    const auto count = std::ranges::count_if(pass.uses, [=](const render::DebugUse& use) {
        return use.resource == resource && use.version == version && use.role == role;
    });
    REQUIRE(count == 1);
}

//======================================================================================================================
void requireExposureSourceState(const render::TextureStateTable& states, rojoRHI::Texture texture) {
    const auto ranges = states.physicalRanges(texture, kMip0);
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges.front().range.baseMip == 0);
    REQUIRE(ranges.front().range.mipCount == 1);
    REQUIRE(ranges.front().range.baseLayer == 0);
    REQUIRE(ranges.front().range.layerCount == 1);
    const render::TextureState expected{S::ComputeShader, A::ShaderRead, L::ShaderRead};
    REQUIRE(ranges.front().state == expected);
    REQUIRE(states.find(texture, kMip0) == expected);
    REQUIRE(states.logicalUse(texture) == render::TextureUse::ShaderRead);
}

//======================================================================================================================
void runExposureFeedbackFrames() {
    auto gpu = createTestGpu({.validation = true, .framesInFlight = 3});
    auto stageResult = render::ExposureStage::create(*gpu);
    INFO(errorOf(stageResult));
    REQUIRE(stageResult.has_value());
    auto stage = std::move(*stageResult);
    constexpr uint32_t kExtent = 4;
    const std::vector<uint16_t> pixels(kExtent * kExtent * 4, lmx::test::halfPow2(0));
    auto scene = lmx::test::makeSceneColorTexture(*gpu, kExtent, kExtent, pixels,
                                                  "lmx.test.exposureTracker.scene");
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());
    render::TransientPool pool(*gpu);
    render::TextureStateTable states;
    ExposureOwnerDrain ownerDrain{*gpu};
    constexpr render::FrameExtents kExtents{kExtent, kExtent, kExtent, kExtent};
    constexpr std::array<ExposureTransition, 6> kTransitions{
        {{true, true}, {true, true}, {true, false}, {false, false}, {true, true}, {true, false}}};
    const auto exposureIdentity = stage->bufferOwner().rhi();
    rojoRHI::Buffer histogramIdentity;
    const uint64_t startingFrame = gpu->frameNumber();
    uint64_t lastSubmitted = startingFrame;

    for (uint32_t ordinal = 0; ordinal < kTransitions.size(); ++ordinal) {
        CAPTURE(ordinal);
        const auto transition = kTransitions[ordinal];
        const auto previousLogical = states.logicalUse(scene->rhi());
        const auto previousPhysical = states.find(scene->rhi(), kMip0);
        const auto previousRanges = states.physicalRanges(scene->rhi(), kMip0);
        auto frame = gpu->beginFrame();
        pool.beginFrame();
        render::RenderGraph graph(pool);
        graph.bindTextureStates(states);
        const auto sceneColor =
            graph.importPersistentTexture(*scene, "lmx.test.exposureTracker.scene");
        const auto exposureImport =
            transition.automatic
                ? graph.importBuffer(stage->bufferOwner(), "lmx.render.exposureBuffer",
                                     render::BufferUse::StorageWrite)
                : graph.importBuffer(stage->bufferOwner(), "lmx.render.exposureBuffer");
        render::SceneView view;
        view.autoExposureEnabled = transition.automatic;
        view.exposureReset = transition.reset;
        view.temporal.enabled = false;
        const auto exposureCurrent =
            stage->declareSeed(graph, frame.commands, view, exposureImport);
        stage->declareMetering(graph, frame.commands, view,
                               {.extents = kExtents,
                                .sceneColorRead = sceneColor,
                                .exposureCurrent = exposureCurrent});

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
        REQUIRE(lowered->record.frameId == frame.number);
        REQUIRE(record.debug.schedule.passes == lowered->record.debug.schedule.passes);
        REQUIRE(stage->bufferOwner().rhi() == exposureIdentity);
        REQUIRE(lowered->resources[exposureImport.index].buffer.buffer == exposureIdentity);
        REQUIRE(lowered->resources[exposureImport.index].buffer.size == 2 * sizeof(float));
        REQUIRE(exposureCurrent.index == exposureImport.index);
        REQUIRE(exposureCurrent.version == (transition.reset ? 1u : 0u));

        const auto histogram = exposureResourceIndex(record, "lmx.render.histogramBuffer");
        const auto histogramBuffer = lowered->resources[histogram].buffer;
        REQUIRE(histogramBuffer.size == 256 * sizeof(uint32_t));
        REQUIRE(histogramBuffer.buffer.bits != 0);
        if (histogramIdentity.bits == 0)
            histogramIdentity = histogramBuffer.buffer;
        REQUIRE(histogramBuffer.buffer == histogramIdentity);
        const auto clear = exposurePassIndex(record, "lmx.pass.exposure.clearHistogram");
        const auto accumulate = exposurePassIndex(record, "lmx.pass.exposure.histogram");
        const auto resolve = exposurePassIndex(record, "lmx.pass.exposure.resolve");
        requireExposureUse(record.debug.passes[clear], histogram, 0,
                           render::UseRole::CopyDestination);
        requireExposureUse(record.debug.passes[accumulate], histogram, 1, render::UseRole::Write);
        requireExposureUse(record.debug.passes[accumulate], exposureCurrent.index,
                           exposureCurrent.version, render::UseRole::ShaderRead);
        requireExposureUse(record.debug.passes[accumulate], sceneColor.index, sceneColor.version,
                           render::UseRole::ShaderRead);
        requireExposureUse(record.debug.passes[resolve], histogram, 2, render::UseRole::Read);
        requireExposureUse(record.debug.passes[resolve], exposureCurrent.index,
                           exposureCurrent.version, render::UseRole::Write);

        if (!transition.automatic) {
            REQUIRE(record.debug.schedule.passes.empty());
            REQUIRE(record.debug.sinks.empty());
            REQUIRE(record.debug.passes.size() == 3);
            for (const auto& pass : record.debug.passes)
                REQUIRE(pass.cullReason == render::CullReason::NoSinkReachesIt);
            REQUIRE(lowered->ops.size() == 2);
            REQUIRE(lowered->physicalStates.empty());
            REQUIRE(previousLogical.has_value());
            REQUIRE(previousPhysical.has_value());
            REQUIRE(previousRanges.size() == 1);
            REQUIRE(states.logicalUse(scene->rhi()) == previousLogical);
            REQUIRE(states.find(scene->rhi(), kMip0) == previousPhysical);
            const auto afterRanges = states.physicalRanges(scene->rhi(), kMip0);
            REQUIRE(afterRanges.size() == previousRanges.size());
            REQUIRE(afterRanges.front().state == previousRanges.front().state);
            REQUIRE(afterRanges.front().range.baseMip == previousRanges.front().range.baseMip);
            REQUIRE(afterRanges.front().range.mipCount == previousRanges.front().range.mipCount);
            REQUIRE(afterRanges.front().range.baseLayer == previousRanges.front().range.baseLayer);
            REQUIRE(afterRanges.front().range.layerCount ==
                    previousRanges.front().range.layerCount);
            continue;
        }

        const std::vector<uint32_t> expectedSchedule =
            transition.reset
                ? std::vector<uint32_t>{exposurePassIndex(record, "lmx.pass.exposure.seed"), clear,
                                        accumulate, resolve}
                : std::vector<uint32_t>{clear, accumulate, resolve};
        REQUIRE(record.debug.schedule.passes == expectedSchedule);
        REQUIRE(record.debug.sinks.size() == 1);
        REQUIRE(record.debug.sinks.front().kind == render::SinkKind::Export);
        REQUIRE(record.debug.sinks.front().resource == exposureCurrent.index);
        REQUIRE(record.debug.sinks.front().version == exposureCurrent.version + 1);
        for (const auto& pass : record.debug.passes)
            REQUIRE_FALSE(pass.cullReason.has_value());

        // Prior nominal buffer imports conservatively retain every shader stage.
        requireExposureBoundary(*lowered, clear, histogram, kAllShaderStages, A::ShaderRead,
                                S::Copy, A::CopyWrite);
        requireExposureBoundary(*lowered, accumulate, histogram, S::Copy, A::CopyWrite,
                                S::ComputeShader, kShaderReadWrite);
        requireExposureBoundary(*lowered, resolve, histogram, S::ComputeShader, kShaderReadWrite,
                                S::ComputeShader, A::ShaderRead);
        requireExposureBoundary(*lowered, accumulate, exposureCurrent.index,
                                transition.reset ? S::ComputeShader : kAllShaderStages,
                                kShaderReadWrite, S::ComputeShader, A::ShaderRead);
        requireExposureBoundary(*lowered, resolve, exposureCurrent.index, S::ComputeShader,
                                A::ShaderRead, S::ComputeShader, kShaderReadWrite);
        REQUIRE(exposureBarriersBefore(*lowered, clear).size() == 1);
        REQUIRE(exposureBarriersBefore(*lowered, accumulate).size() == 2);
        REQUIRE(exposureBarriersBefore(*lowered, resolve).size() == 2);
        if (transition.reset) {
            const auto seed = exposurePassIndex(record, "lmx.pass.exposure.seed");
            REQUIRE(record.debug.passes[seed].uses.size() == 1);
            requireExposureUse(record.debug.passes[seed], exposureImport.index,
                               exposureImport.version, render::UseRole::Write);
            requireExposureBoundary(*lowered, seed, exposureImport.index, kAllShaderStages,
                                    kShaderReadWrite, S::ComputeShader, kShaderReadWrite);
            REQUIRE(exposureBarriersBefore(*lowered, seed).size() == 1);
        } else {
            REQUIRE(std::ranges::none_of(record.debug.passes, [](const render::DebugPass& pass) {
                return pass.label == "lmx.pass.exposure.seed";
            }));
        }
        requireExposureSourceState(states, scene->rhi());
    }

    gpu->waitIdle();
    ownerDrain.drained = true;
    REQUIRE(lastSubmitted == startingFrame + kTransitions.size());
    REQUIRE(gpu->frameNumber() == lastSubmitted);
    REQUIRE(gpu->completedFrame() == lastSubmitted);
}

} // namespace
#endif

//======================================================================================================================
TEST_CASE("exposure feedback resets and leaves manual Off without stale accesses under the tracker",
          "[render][device][tracker]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    runExposureFeedbackFrames();
#endif
}

namespace {
struct RendererExposureFrame {
    bool automatic;
    bool reset;
    bool temporalFlag;
};

// Auto reset, steady auto, manual Off, two manual temporal seeds, consecutive auto resets and a
// steady auto frame: every change of the Renderer's prior-use choice for the exposure import.
constexpr std::array<RendererExposureFrame, 8> kRendererExposureFrames{{{true, true, false},
                                                                        {true, false, false},
                                                                        {false, false, false},
                                                                        {false, false, true},
                                                                        {false, false, true},
                                                                        {true, true, false},
                                                                        {true, true, false},
                                                                        {true, false, false}}};

//======================================================================================================================
bool exposurePassScheduled(const lmx::render::CompiledFrameRecord& record, std::string_view label) {
    const auto found =
        std::ranges::find(record.debug.passes, label, &lmx::render::DebugPass::label);
    if (found == record.debug.passes.end())
        return false;
    const auto index = static_cast<uint32_t>(found - record.debug.passes.begin());
    return std::ranges::find(record.debug.schedule.passes, index) !=
           record.debug.schedule.passes.end();
}
} // namespace

//======================================================================================================================
TEST_CASE("exposure transitions through the renderer under the tracker",
          "[render][device][tracker]") {
    lmx::test::runTrackedFrames(
        "material-lab",
        {.frames = static_cast<uint32_t>(kRendererExposureFrames.size()),
         .perFrame =
             [](lmx::render::SceneView& view, uint32_t frame) {
                 const auto& state = kRendererExposureFrames[frame];
                 // The flag selects the manual seed and the Renderer's prior-use import; the
                 // frame's temporal reconstruction reads the seeded pair after the scene pass.
                 view.temporal.enabled = state.temporalFlag;
                 view.localLightMode = lmx::engine::LocalLightMode::Off;
                 view.classifyMode = lmx::render::ClassifyMode::Cpu;
                 view.occlusionEnabled = false;
                 view.autoExposureEnabled = state.automatic;
                 view.exposureReset = state.reset;
                 view.exposureEv = static_cast<float>(frame % 3) - 1.0f;
                 REQUIRE_FALSE(view.items.empty());
             },
         .inspect =
             [](const lmx::render::CompiledFrameRecord& record, uint32_t frame) {
                 const auto& state = kRendererExposureFrames[frame];
                 const bool seeds = state.automatic ? state.reset : state.temporalFlag;
                 REQUIRE(exposurePassScheduled(record, "lmx.pass.exposure.seed") == seeds);
                 REQUIRE(exposurePassScheduled(record, "lmx.pass.exposure.histogram") ==
                         state.automatic);
                 REQUIRE(exposurePassScheduled(record, "lmx.pass.exposure.resolve") ==
                         state.automatic);
             }});
}
