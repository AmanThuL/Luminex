//----------------------------------------------------------------------------------------------------------------------
/// @file LifeNoApi.cpp
/// @brief Implements LifeNoApi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements S-LIFE (spec section 7) against the address-first prototype. See
/// LifeRhi.cpp's
///        header comment for the shared per-frame content/verification design and the
///        wait-idle-per-frame simplification this correctness run makes on both sides.
///        Allocation counters here cover both the private texture-placement memory (one allocation
///        per extent) and the texture object itself, since the prototype's memory and texture
///        lifetimes are separate contracts (Memory.h vs Texture.h) that both need proving settled.

#include "Bench/StressCommon.h"
#include "Workload/StressCases.h"

#include "NoApi/NoApi.h"

#include <array>
#include <cstring>
#include <sstream>

namespace lmx::experimental::noapi::bench {
namespace {

struct Counters {
    uint32_t allocationCreates = 0, allocationDestroys = 0;
    uint32_t textureCreates = 0, textureDestroys = 0;
};

struct ExtentResource {
    Allocation memory{};
    Texture* texture = nullptr;
};

//======================================================================================================================
ExtentResource makeExtentResource(Device* device, Counters& counters, workload::Extent extent) {
    ExtentResource resource;
    const TextureDesc desc{.kind = TextureKind::Texture2D,
                           .extent = {extent.width, extent.height, 1},
                           .format = Format::RGBA8Unorm,
                           .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource |
                                    TextureUsage::CopyDestination,
                           .label = "slife.target"};
    const SizeAlign required = textureSizeAlign(device, desc);
    Result<Allocation> allocation = allocate(device, {.size = required.size,
                                                      .alignment = required.alignment,
                                                      .kind = MemoryKind::Private,
                                                      .label = "slife.memory"});
    if (!allocation) {
        return resource;
    }
    resource.memory = *allocation;
    counters.allocationCreates += 1;
    Result<Texture*> texture = createTexture(device, desc, resource.memory.gpu);
    if (!texture) {
        return resource;
    }
    resource.texture = *texture;
    counters.textureCreates += 1;
    return resource;
}

//======================================================================================================================
void destroyExtentResource(Device* device, Counters& counters, ExtentResource& resource) {
    if (resource.texture != nullptr) {
        destroyTexture(device, resource.texture);
        counters.textureDestroys += 1;
        resource.texture = nullptr;
    }
    if (resource.memory.gpu != kNullAddress) {
        deallocate(device, resource.memory);
        counters.allocationDestroys += 1;
        resource.memory = {};
    }
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runLifeCasesNoApi(const std::string& caseId) {
    if (caseId != "all" && caseId != "S-LIFE") {
        return {};
    }

    Result<Device*> deviceResult = createDevice({.label = "slife"});
    if (!deviceResult) {
        return {{"S-LIFE", false, "device creation failed"}};
    }
    Device* device = *deviceResult;
    Queue* queue = mainQueue(device);

    Result<Semaphore*> fenceResult = createSemaphore(device, 0, "slife.fence");
    Result<Allocation> rootStorageResult = allocate(
        device,
        {.size = 4096, .alignment = 256, .kind = MemoryKind::Shared, .label = "slife.root"});
    Result<Allocation> uploadResult = allocate(
        device,
        {.size = 4096, .alignment = 256, .kind = MemoryKind::Shared, .label = "slife.upload"});
    Result<Allocation> readbackResult = allocate(
        device, {.size = uint64_t{workload::kExtentE2.width} * workload::kExtentE2.height * 4,
                 .alignment = 256,
                 .kind = MemoryKind::Readback,
                 .label = "slife.readback"});
    if (!fenceResult || !rootStorageResult || !uploadResult || !readbackResult) {
        return {{"S-LIFE", false, "harness setup failed"}};
    }
    Semaphore* fence = *fenceResult;
    LinearAllocator root(*rootStorageResult);
    LinearAllocator upload(*uploadResult);
    uint64_t fenceValue = 0;

    Counters counters;
    ExtentResource current = makeExtentResource(device, counters, workload::kExtentE1);
    if (current.texture == nullptr) {
        return {{"S-LIFE", false, "initial E1 resource creation failed"}};
    }

    std::ostringstream trace;
    bool ok = true;
    std::string failure;

    for (const workload::LifetimeFrame& frame : workload::lifetimeSchedule()) {
        if (!ok) {
            break;
        }
        if (frame.resizeIssuedThisFrame) {
            ExtentResource next = makeExtentResource(device, counters, frame.resizeTarget);
            if (next.texture == nullptr) {
                ok = false;
                failure =
                    "resize resource creation failed at frame " + std::to_string(frame.frameIndex);
                break;
            }
            destroyExtentResource(device, counters, current); // Safe: the previous frame's
                                                              // submitAndWait already retired it.
            current = next;
        }

        root.reset();
        CommandBuffer* commands = beginCommands(queue, &root, "slife.frame");
        const float clearValue = static_cast<float>(frame.frameIndex + 1) / 16.0f;
        const std::array<ColorAttachment, 1> colorTargets{
            ColorAttachment{.texture = current.texture,
                            .load = LoadAction::Clear,
                            .store = StoreAction::Store,
                            .clearColor = {clearValue, clearValue, clearValue, 1.0f}}};
        beginRenderPass(commands, {.colorTargets = colorTargets, .label = "slife.clear"});
        endRenderPass(commands);

        if (frame.midFlightUpload) {
            const std::array<uint8_t, 16> patch{200, 100, 50, 255, 200, 100, 50, 255,
                                                200, 100, 50, 255, 200, 100, 50, 255};
            const Suballocation staging = upload.allocate(patch.size(), 16);
            std::memcpy(staging.cpu, patch.data(), patch.size());
            barrier(commands, Stage::RasterColorOut, Stage::Copy);
            // bytesPerRow is the region's own stride: a 2-texel-wide RGBA8 region is 2 * 4 = 8
            // bytes.
            copyToTexture(commands, current.texture,
                          {.mipLevel = 0, .origin = {}, .extent = {2, 2, 1}}, staging.gpu,
                          {.bytesPerRow = 8});
            barrier(commands, Stage::Copy, Stage::Copy);
        } else {
            barrier(commands, Stage::RasterColorOut, Stage::Copy);
        }
        copyFromTexture(
            commands, readbackResult->gpu, {.bytesPerRow = uint64_t{frame.extent.width} * 4},
            current.texture,
            {.mipLevel = 0, .origin = {}, .extent = {frame.extent.width, frame.extent.height, 1}});
        endCommands(commands);
        const std::array<CommandBuffer*, 1> list{commands};
        fenceValue += 1;
        submit(queue, list, fence, fenceValue);
        waitSemaphore(fence, fenceValue);

        const auto* pixels = static_cast<const uint8_t*>(readbackResult->cpu);
        const auto expected = static_cast<uint8_t>(clearValue * 255.0f);
        // Frame 4's mid-flight upload overwrites the 2x2 corner at (0,0)-(1,1), so the clear check
        // reads a texel outside that patch (row 2) rather than texel 0 itself on that frame.
        const uint64_t clearCheckOffset =
            frame.midFlightUpload ? uint64_t{2} * frame.extent.width * 4 : 0;
        if (pixels[clearCheckOffset] < expected - 2 || pixels[clearCheckOffset] > expected + 2) {
            ok = false;
            failure = "frame " + std::to_string(frame.frameIndex) + " clear content mismatch";
            break;
        }
        if (frame.midFlightUpload && pixels[0] != 200) {
            ok = false;
            failure =
                "frame " + std::to_string(frame.frameIndex) + " mid-flight upload not observed";
            break;
        }

        trace << "frame " << frame.frameIndex << ": extent=" << frame.extent.width << "x"
              << frame.extent.height << " textureCreates=" << counters.textureCreates
              << " textureDestroys=" << counters.textureDestroys
              << " allocationCreates=" << counters.allocationCreates
              << " allocationDestroys=" << counters.allocationDestroys << "\n";
    }

    if (!ok) {
        destroyExtentResource(device, counters, current);
        destroySemaphore(device, fence);
        deallocate(device, *readbackResult);
        deallocate(device, *uploadResult);
        deallocate(device, *rootStorageResult);
        destroyDevice(device);
        return {{"S-LIFE", false, failure + "\n" + trace.str()}};
    }

    destroyExtentResource(device, counters, current);
    const bool settled = counters.textureCreates == counters.textureDestroys &&
                         counters.allocationCreates == counters.allocationDestroys;
    trace << "final: textureCreates=" << counters.textureCreates
          << " textureDestroys=" << counters.textureDestroys
          << " allocationCreates=" << counters.allocationCreates
          << " allocationDestroys=" << counters.allocationDestroys
          << " settled=" << (settled ? "yes" : "no") << "\n";

    destroySemaphore(device, fence);
    deallocate(device, *readbackResult);
    deallocate(device, *uploadResult);
    deallocate(device, *rootStorageResult);
    destroyDevice(device);

    return {{"S-LIFE", settled, trace.str()}};
}

} // namespace lmx::experimental::noapi::bench
