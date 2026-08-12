//----------------------------------------------------------------------------------------------------------------------
/// @file MisuseRhi.cpp
/// @brief Implements MisuseRhi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements M1-M6's misuse triggers (spec section 7) against the maintained RHI. Each
///        function is expected to LMX_ASSERT and abort the process; StressRunner.cpp's runMisuse
///        re-execs this binary in a child process and captures the death. See that file's header
///        comment for the parent/child protocol.
///
///        M5 and M6 have no direct public-RHI hook for "destroyed handle" or "unretired frame slot"
///        (Device owns the frame loop and its internal uniform ring privately; Texture/Buffer are
///        plain caller-owned unique_ptrs with no liveness registry the public header documents).
///        The nearest expressible realizations are recorded at each trigger below rather than
///        silently assumed identical to the prototype side's cleaner mechanism.

#include "Bench/StressRunner.h"

#include "RHI/RHI.h"

#include <string>

namespace lmx::experimental::noapi::bench {
namespace {

//======================================================================================================================
void triggerM1() { // Pass-scope violation: dispatch recorded outside any compute pass.
    auto device = rhi::createDevice();
    rhi::CommandList& cmd = (*device)->beginFrame();
    cmd.dispatch(1, 1, 1); // No beginComputePass: expected to assert.
}

//======================================================================================================================
void triggerM2() { // Usage mismatch: a texture created without storage usage bound for storage
                   // write.
    auto device = rhi::createDevice();
    auto texture = (*device)->createTexture({.width = 4,
                                             .height = 4,
                                             .format = rhi::Format::RGBA8Unorm,
                                             .sampled = true, // storageWrite deliberately omitted
                                             .label = "misuse.m2.texture"});
    rhi::CommandList& cmd = (*device)->beginFrame();
    cmd.beginComputePass("misuse.m2");
    cmd.bindStorageTexture(0, **texture, {}, rhi::StorageAccess::Write); // Expected to assert.
}

//======================================================================================================================
void triggerM3() { // View/range overflow: a view whose mip range exceeds the texture's mip count.
    auto device = rhi::createDevice();
    auto texture = (*device)->createTexture({.width = 4,
                                             .height = 4,
                                             .format = rhi::Format::RGBA8Unorm,
                                             .mipLevels = 1,
                                             .storageRead = true,
                                             .label = "misuse.m3.texture"});
    rhi::CommandList& cmd = (*device)->beginFrame();
    cmd.beginComputePass("misuse.m3");
    cmd.bindStorageTexture(
        0, **texture,
        {.range = {.baseMipLevel = 5, .mipLevelCount = 1}, .format = rhi::Format::Unknown},
        rhi::StorageAccess::Read); // baseMipLevel 5 exceeds the texture's 1 allocated level.
}

//======================================================================================================================
void triggerM4() { // Alignment violation: an indirect-argument offset not a multiple of the
                   // documented kIndirectArgsAlignment (4 bytes).
    auto device = rhi::createDevice();
    auto buffer = (*device)->createBuffer({.size = 256, .label = "misuse.m4.buffer"}, nullptr);
    auto library = (*device)->loadShaderLibrary("Shaders/ComputeSmoke");
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeFillBuffer",
                                                      .threadsPerThreadgroup = {64, 1, 1},
                                                      .label = "misuse.m4.pipeline"});
    rhi::CommandList& cmd = (*device)->beginFrame();
    cmd.beginComputePass("misuse.m4");
    cmd.bindComputePipeline(**pipeline);
    cmd.dispatchIndirect(**buffer, 3); // 3 is not a multiple of 4: expected to assert.
}

//======================================================================================================================
void triggerM5() { // Invalid handle: destroying a texture while it is still bound in an open pass'
                   // argument table is the nearest publicly-documented RHI liveness contract
                   // (RHI/Include/RHI/RHI.h has no separate "handle" type to invalidate).
    auto device = rhi::createDevice();
    auto texture = (*device)->createTexture({.width = 4,
                                             .height = 4,
                                             .format = rhi::Format::RGBA8Unorm,
                                             .sampled = true,
                                             .label = "misuse.m5.texture"});
    rhi::Texture* raw = texture->get();
    texture->reset(); // Destroys the texture while nothing has proven the device is idle.
    rhi::CommandList& cmd = (*device)->beginFrame();
    cmd.beginComputePass("misuse.m5");
    cmd.bindTexture(0, *raw); // Expected to assert (or fault) on the freed object.
}

//======================================================================================================================
void triggerM6() { // Retired frame-slot use: a second beginFrame() before the first's endFrame(),
                   // the nearest publicly-observable analogue of reusing a frame slot the device
                   // has not proven retired (Device owns the frame-in-flight bookkeeping
                   // privately).
    auto device = rhi::createDevice();
    (*device)->beginFrame();
    (*device)->beginFrame(); // Expected to assert: a frame is already open.
}

} // namespace

//======================================================================================================================
int runMisuseChildRhi(const std::string& caseId) {
    if (caseId == "M1") {
        triggerM1();
    } else if (caseId == "M2") {
        triggerM2();
    } else if (caseId == "M3") {
        triggerM3();
    } else if (caseId == "M4") {
        triggerM4();
    } else if (caseId == "M5") {
        triggerM5();
    } else if (caseId == "M6") {
        triggerM6();
    } else {
        return 2;
    }
    return 1; // Reaching here means the misuse did not abort the process, which is itself a
              // failure.
}

} // namespace lmx::experimental::noapi::bench
