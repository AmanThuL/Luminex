#pragma once

#include "Render/Graph/RenderGraph.h"
#include "Support/OracleRoots.h"
#include "Support/TestGpu.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace lmx::test {

inline constexpr uint32_t kBufferReadWriteElements = 256;
inline constexpr uint32_t kBufferReadWriteFrames = 4;
inline constexpr uint32_t kBufferReadWriteFill = 0x03030303;
inline constexpr uint32_t kBufferReadWriteBias = 7;
inline constexpr uint64_t kBufferReadWriteBytes = sizeof(uint32_t) * kBufferReadWriteElements;

struct BufferReadWriteGraphRun {
    engine::GpuOwned<rojoRHI::ShaderLibrary> library;
    engine::GpuOwned<rojoRHI::Pipeline> pipeline;
    engine::GpuBuffer buffer;
    std::array<render::CompiledFrameRecord, kBufferReadWriteFrames> records;
    std::array<uint64_t, kBufferReadWriteFrames> submittedFrames{};
};

//======================================================================================================================
// The same owner is read and written in place. Only the graph supplies dependencies, including
// the prior-frame write at the fourth submission's three-slot wrap.
inline BufferReadWriteGraphRun runBufferReadWriteGraph(engine::GpuContext& gpu) {
    using namespace rojoRHI;
    using namespace render;

    auto library = gpu.loadShaderLibrary("BufferHazardSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline =
        engine::adoptGpu(gpu.device(), gpu.device().createPipeline(ComputePipelineDesc{
                                           .compute = {library->get(), "computeAddFromBuffer"},
                                           .label = "lmx.test.graph.inPlacePipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());
    auto buffer =
        engine::GpuBuffer::create(gpu.device(), {.size = kBufferReadWriteBytes,
                                                 .usage = BufferUsage::ShaderWrite,
                                                 .label = "lmx.test.graph.inPlaceBuffer"});
    INFO(errorOf(buffer));
    REQUIRE(buffer.has_value());

    BufferReadWriteGraphRun result{.library = std::move(*library),
                                   .pipeline = std::move(*pipeline),
                                   .buffer = std::move(*buffer)};
    for (uint32_t ordinal = 0; ordinal < kBufferReadWriteFrames; ++ordinal) {
        auto frame = gpu.beginFrame();
        auto& commands = frame.commands;
        RenderGraph graph;
        const auto imported = ordinal == 0
                                  ? graph.importBuffer(result.buffer, "lmx.test.graph.inPlace")
                                  : graph.importBuffer(result.buffer, "lmx.test.graph.inPlace",
                                                       BufferUse::StorageWrite);
        auto current = imported;
        if (ordinal == 0) {
            CopyPassDesc fill;
            fill.bufferDestinations.push_back(current);
            graph.addCopyPass("lmx.test.graph.inPlaceFill", std::move(fill),
                              [&, current](const PassResources& resources) {
                                  commands.fillBuffer(resources.buffer(current), 0,
                                                      kBufferReadWriteBytes, kBufferReadWriteFill);
                              });
            current = nextVersion(current);
        }

        ComputePassDesc write;
        write.bufferWrites.push_back(current);
        graph.addComputePass(
            "lmx.test.graph.inPlaceAdd", std::move(write),
            [&, current](const PassResources& resources) {
                const auto address = resources.address(current);
                commands.setPipeline(result.pipeline.get());
                commands.pushRoot(BufferHazardRoot{
                    .output = address, .source = address, .bias = kBufferReadWriteBias});
#ifndef NDEBUG
                const ResourceAccess access{.buffer = resources.buffer(current),
                                            .size = kBufferReadWriteBytes,
                                            .stage = Stage::ComputeShader,
                                            .access = Access::ShaderRead | Access::ShaderWrite};
                commands.declareAccess(std::span(&access, 1));
#endif
                commands.dispatch(kBufferReadWriteElements / 64, 1, 1);
            });
        graph.exportBuffer(nextVersion(current));
        result.records[ordinal] = graph.execute(commands, frame.number);
        gpu.submitAndEndFrame(frame);
        result.submittedFrames[ordinal] = frame.number;
    }
    return result;
}

} // namespace lmx::test
