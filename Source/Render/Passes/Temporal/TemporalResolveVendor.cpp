//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveVendor.cpp
/// @brief Adapts vendor selection and declares vendor history diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"
#include "Render/Passes/Temporal/TemporalRoots.h"

#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <array>
#include <utility>

namespace lmx::render {

//======================================================================================================================
ReconstructionSelection TemporalResolve::prepare(ReconstructionMode requested, uint32_t width,
                                                 uint32_t height) {
    return m_vendor->prepare(requested, width, height);
}

//======================================================================================================================
void TemporalResolve::recordDisabledFrame() {
    m_vendor->recordMode(ReconstructionMode::Raw);
}

//======================================================================================================================
bool TemporalResolve::vendorReset() const {
    return m_vendor->reset();
}

//======================================================================================================================
uint32_t TemporalResolve::vendorScalerGeneration() const {
    return m_vendor->generation();
}

//======================================================================================================================
GraphTexture TemporalResolve::declareVendorHistory(RenderGraph& graph,
                                                   rojoRHI::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    const auto target = graph.createTexture({.width = inputs.extents.outputWidth,
                                             .height = inputs.extents.outputHeight,
                                             .format = rojoRHI::Format::RGBA16Float,
                                             .sampled = true,
                                             .storageWrite = true},
                                            "lmx.render.temporalReprojected");
    ComputePassDesc desc;
    desc.shaderTextureReads = {inputs.depth, inputs.previousDepth, inputs.motion, inputs.history};
    desc.bufferReads = {inputs.exposure};
    desc.textureWrites = {target};
    const auto params =
        buildTemporalUpscaleParams({.extents = inputs.extents,
                                    .previousExtents = inputs.previousExtents,
                                    .camera = inputs.camera,
                                    .previousCamera = inputs.previousCamera,
                                    .historyValid = inputs.resetReason == HistoryResetReason::None,
                                    .rejectionWanted = false,
                                    .reprojectedWanted = true});
    graph.addComputePass(
        "lmx.pass.temporal.reprojectedHistory", std::move(desc),
        [this, &commands, inputs, target, params](const PassResources& resources) {
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("TemporalUpscaleParams", paramsAddress);
            commands.setPipeline(m_vendor->historyPipeline());
            engine::registerFrameRoot(
                "VendorTemporalHistoryRoot",
                commands.pushRoot(VendorTemporalHistoryRoot{
                    .paramsAddr = paramsAddress,
                    .exposureAddr = resources.address(inputs.exposure),
                    .depth = static_cast<uint32_t>(resources.view(inputs.depth)),
                    .previousDepth = static_cast<uint32_t>(resources.view(inputs.previousDepth)),
                    .motion = static_cast<uint32_t>(resources.view(inputs.motion)),
                    .history = static_cast<uint32_t>(resources.view(inputs.history)),
                    .reprojected = static_cast<uint32_t>(resources.storageView(target, 0)),
                    .sampler = static_cast<uint32_t>(m_sampler.get())}));
#ifndef NDEBUG
            const auto read = [&](GraphTexture handle) {
                return rojoRHI::ResourceAccess{.texture = resources.texture(handle).texture,
                                               .range = {0, 1, 0, 1},
                                               .stage = rojoRHI::Stage::ComputeShader,
                                               .access = rojoRHI::Access::ShaderRead};
            };
            const std::array<rojoRHI::ResourceAccess, 6> accesses{
                {read(inputs.depth),
                 read(inputs.previousDepth),
                 read(inputs.motion),
                 read(inputs.history),
                 {.texture = resources.texture(target).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite},
                 {.buffer = resources.buffer(inputs.exposure),
                  .size = 2 * sizeof(float),
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(params.outputWidth, params.outputHeight);
            commands.dispatch(groups[0], groups[1], 1);
        });
    return nextVersion(target);
}

} // namespace lmx::render
