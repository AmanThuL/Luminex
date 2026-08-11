//----------------------------------------------------------------------------------------------------------------------
/// @file IndirectRhi.cpp
/// @brief Implements I1-I4 (spec section 7) against the maintained RHI: drawIndirect (I1,
///        CPU-written args at byte offset 256), drawIndexedIndirect (I2, compute-written at offset
///        0), dispatchIndirect (I3, CPU-written at offset 64), dispatchIndirect (I4,
///        compute-written at offset 128).
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressCommon.h"
#include "Workload/StressCases.h"

#include "RHI/RHI.h"

#include <array>
#include <cstring>

namespace lmx::noapi::bench {
namespace {

using workload::IndirectArgSource;
using workload::IndirectCase;
using workload::IndirectKind;

//======================================================================================================================
CaseResult runDraw(rhi::Device& device, bool indexed) {
    auto library = device.loadShaderLibrary("Shaders/StressIndirectDraw");
    if (!library) {
        return {"", false, "shader load failed: " + library.error().message};
    }
    auto pipelineResult = device.createGraphicsPipeline({.library = library->get(),
                                                         .vertexEntry = "vertexIndirect",
                                                         .fragmentEntry = "fragmentSolid",
                                                         .colorFormat = rhi::Format::RGBA8Unorm,
                                                         .cullMode = rhi::CullMode::None,
                                                         .label = "indirect.draw"});
    if (!pipelineResult) {
        return {"", false, "pipeline creation failed: " + pipelineResult.error().message};
    }
    auto pipeline = std::move(*pipelineResult);

    auto targetResult = device.createTexture({.width = 4,
                                              .height = 4,
                                              .format = rhi::Format::RGBA8Unorm,
                                              .kind = rhi::TextureKind::Tex2D,
                                              .renderTarget = true,
                                              .cpuReadback = true,
                                              .label = "indirect.target"});
    if (!targetResult) {
        return {"", false, "target creation failed"};
    }
    auto target = std::move(*targetResult);

    constexpr uint64_t kArgsBufferSize = 512;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    std::unique_ptr<rhi::Buffer> argsBuffer;

    if (!indexed) { // I1: CPU-written drawIndirect at offset 256.
        std::vector<uint8_t> bytes(kArgsBufferSize, 0);
        const rhi::DrawIndirectArgs args{.vertexCount = 3, .instanceCount = 1};
        std::memcpy(bytes.data() + 256, &args, sizeof(args));
        auto result = device.createBuffer({.size = kArgsBufferSize, .label = "indirect.i1Args"},
                                          bytes.data());
        if (!result) {
            return {"I1", false, "args buffer creation failed"};
        }
        argsBuffer = std::move(*result);
    } else { // I2: compute-written drawIndexedIndirect at offset 0.
        std::array<uint32_t, 3> indices{0, 1, 2};
        auto indexResult = device.createBuffer(
            {.size = sizeof(indices), .label = "indirect.i2Indices"}, indices.data());
        if (!indexResult) {
            return {"I2", false, "index buffer creation failed"};
        }
        indexBuffer = std::move(*indexResult);

        auto zero = std::vector<uint8_t>(kArgsBufferSize, 0);
        auto argsResult = device.createBuffer(
            {.size = kArgsBufferSize, .storageWrite = true, .label = "indirect.i2Args"},
            zero.data());
        if (!argsResult) {
            return {"I2", false, "args buffer creation failed"};
        }
        argsBuffer = std::move(*argsResult);

        auto argsLibrary = device.loadShaderLibrary("Shaders/IndirectSmoke");
        if (!argsLibrary) {
            return {"I2", false, "IndirectSmoke load failed: " + argsLibrary.error().message};
        }
        auto argsPipelineResult =
            device.createComputePipeline({.library = argsLibrary->get(),
                                          .computeEntry = "computeWriteDrawIndexedArgs",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "indirect.i2ArgsPipeline"});
        if (!argsPipelineResult) {
            return {"I2", false,
                    "IndirectSmoke pipeline failed: " + argsPipelineResult.error().message};
        }
        auto argsPipeline = std::move(*argsPipelineResult);

        rhi::CommandList& cmd = device.beginFrame();
        cmd.beginComputePass("indirect.i2.writeArgs");
        cmd.bindComputePipeline(*argsPipeline);
        cmd.bindStorageBuffer(0, *argsBuffer, rhi::StorageAccess::Write);
        struct IndirectParams {
            uint32_t argsIndex, threadgroupsX, indexCount, firstIndex;
            int32_t baseVertex;
        };
        const IndirectParams params{
            .argsIndex = 0, .threadgroupsX = 0, .indexCount = 3, .firstIndex = 0, .baseVertex = 0};
        cmd.setUniforms(1, &params, sizeof(params));
        cmd.dispatch(1, 1, 1);
        cmd.endComputePass();
        cmd.bufferBarrier(*argsBuffer, rhi::BufferUse::StorageWrite,
                          rhi::BufferUse::IndirectArgument);

        cmd.beginRenderPass(
            {.colorTarget = target.get(), .clear = true, .label = "indirect.i2.draw"});
        cmd.bindPipeline(*pipeline);
        cmd.drawIndexedIndirect(*indexBuffer, *argsBuffer, 0);
        cmd.endRenderPass();
        device.endFrame(nullptr);
        device.waitIdle();

        std::array<uint8_t, 4 * 4 * 4> pixels{};
        target->readback(pixels.data(), pixels.size());
        if (pixels[0] != 255 || pixels[1] != 0 || pixels[2] != 0) {
            return {"I2", false, "target pixel is not the solid draw colour"};
        }
        return {"I2", true, ""};
    }

    // I1: single render pass, CPU-written args already in `argsBuffer`.
    rhi::CommandList& cmd = device.beginFrame();
    cmd.beginRenderPass({.colorTarget = target.get(), .clear = true, .label = "indirect.i1.draw"});
    cmd.bindPipeline(*pipeline);
    cmd.drawIndirect(*argsBuffer, 256);
    cmd.endRenderPass();
    device.endFrame(nullptr);
    device.waitIdle();

    std::array<uint8_t, 4 * 4 * 4> pixels{};
    target->readback(pixels.data(), pixels.size());
    if (pixels[0] != 255 || pixels[1] != 0 || pixels[2] != 0) {
        return {"I1", false, "target pixel is not the solid draw colour"};
    }
    return {"I1", true, ""};
}

//======================================================================================================================
CaseResult runDispatch(rhi::Device& device, bool computeWritten) {
    const char* id = computeWritten ? "I4" : "I3";
    auto library = device.loadShaderLibrary("Shaders/ComputeSmoke");
    if (!library) {
        return {id, false, "ComputeSmoke load failed: " + library.error().message};
    }
    auto pipelineResult = device.createComputePipeline({.library = library->get(),
                                                        .computeEntry = "computeFillBuffer",
                                                        .threadsPerThreadgroup = {64, 1, 1},
                                                        .label = "indirect.dispatchFill"});
    if (!pipelineResult) {
        return {id, false, "pipeline creation failed: " + pipelineResult.error().message};
    }
    auto pipeline = std::move(*pipelineResult);

    auto outResult = device.createBuffer({.size = 64 * sizeof(uint32_t),
                                          .storageWrite = true,
                                          .cpuReadback = true,
                                          .label = "indirect.dispatchOut"},
                                         nullptr);
    if (!outResult) {
        return {id, false, "output buffer creation failed"};
    }
    auto outBuffer = std::move(*outResult);

    constexpr uint64_t kArgsBufferSize = 256;
    constexpr uint32_t kBias = 100;
    std::unique_ptr<rhi::Buffer> argsBuffer;

    rhi::CommandList* cmd = nullptr;
    if (!computeWritten) { // I3: CPU-written at offset 64.
        std::vector<uint8_t> bytes(kArgsBufferSize, 0);
        const rhi::DispatchIndirectArgs args{
            .threadgroupsX = 1, .threadgroupsY = 1, .threadgroupsZ = 1};
        std::memcpy(bytes.data() + 64, &args, sizeof(args));
        auto result = device.createBuffer({.size = kArgsBufferSize, .label = "indirect.i3Args"},
                                          bytes.data());
        if (!result) {
            return {id, false, "args buffer creation failed"};
        }
        argsBuffer = std::move(*result);

        cmd = &device.beginFrame();
        cmd->beginComputePass("indirect.i3.dispatch");
        cmd->bindComputePipeline(*pipeline);
        cmd->bindStorageBuffer(0, *outBuffer, rhi::StorageAccess::Write);
        struct ComputeParams {
            uint32_t bias, extent;
        };
        const ComputeParams params{.bias = kBias, .extent = 0};
        cmd->setUniforms(1, &params, sizeof(params));
        cmd->dispatchIndirect(*argsBuffer, 64);
        cmd->endComputePass();
    } else { // I4: compute-written at offset 128.
        std::vector<uint8_t> zero(kArgsBufferSize, 0);
        auto result = device.createBuffer(
            {.size = kArgsBufferSize, .storageWrite = true, .label = "indirect.i4Args"},
            zero.data());
        if (!result) {
            return {id, false, "args buffer creation failed"};
        }
        argsBuffer = std::move(*result);

        auto argsLibrary = device.loadShaderLibrary("Shaders/IndirectSmoke");
        if (!argsLibrary) {
            return {id, false, "IndirectSmoke load failed: " + argsLibrary.error().message};
        }
        auto argsPipelineResult =
            device.createComputePipeline({.library = argsLibrary->get(),
                                          .computeEntry = "computeWriteDispatchArgs",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "indirect.i4ArgsPipeline"});
        if (!argsPipelineResult) {
            return {id, false, "IndirectSmoke pipeline failed"};
        }
        auto argsPipeline = std::move(*argsPipelineResult);

        cmd = &device.beginFrame();
        cmd->beginComputePass("indirect.i4.writeArgs");
        cmd->bindComputePipeline(*argsPipeline);
        cmd->bindStorageBuffer(0, *argsBuffer, rhi::StorageAccess::Write);
        struct IndirectParams {
            uint32_t argsIndex, threadgroupsX, indexCount, firstIndex;
            int32_t baseVertex;
        };
        const IndirectParams writeParams{.argsIndex = 128 / 4,
                                         .threadgroupsX = 1,
                                         .indexCount = 0,
                                         .firstIndex = 0,
                                         .baseVertex = 0};
        cmd->setUniforms(1, &writeParams, sizeof(writeParams));
        cmd->dispatch(1, 1, 1);
        cmd->endComputePass();
        cmd->bufferBarrier(*argsBuffer, rhi::BufferUse::StorageWrite,
                           rhi::BufferUse::IndirectArgument);

        cmd->beginComputePass("indirect.i4.dispatch");
        cmd->bindComputePipeline(*pipeline);
        cmd->bindStorageBuffer(0, *outBuffer, rhi::StorageAccess::Write);
        struct ComputeParams {
            uint32_t bias, extent;
        };
        const ComputeParams params{.bias = kBias, .extent = 0};
        cmd->setUniforms(1, &params, sizeof(params));
        cmd->dispatchIndirect(*argsBuffer, 128);
        cmd->endComputePass();
    }

    device.endFrame(nullptr);
    device.waitIdle();

    std::array<uint32_t, 64> values{};
    outBuffer->readback(values.data(), values.size() * sizeof(uint32_t));
    if (values[0] != kBias || values[63] != 63u * 3u + kBias) {
        return {id, false, "dispatched fill did not produce the expected pattern"};
    }
    return {id, true, ""};
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runIndirectCasesRhi(const std::string& caseId) {
    std::vector<CaseResult> results;
    auto device = rhi::createDevice();
    if (!device) {
        return {{"I1-I4", false, "device creation failed: " + device.error().message}};
    }
    for (const IndirectCase& indirectCase : workload::indirectCases()) {
        if (caseId != "all" && caseId != indirectCase.id) {
            continue;
        }
        if (indirectCase.kind == IndirectKind::DrawIndirect) {
            results.push_back(runDraw(**device, false));
        } else if (indirectCase.kind == IndirectKind::DrawIndexedIndirect) {
            results.push_back(runDraw(**device, true));
        } else {
            results.push_back(
                runDispatch(**device, indirectCase.argSource == IndirectArgSource::ComputeWritten));
        }
    }
    return results;
}

} // namespace lmx::noapi::bench
