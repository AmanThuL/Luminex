//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveVendor.cpp
/// @brief Adapts vendor selection and declares vendor history diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/Dispatch.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <utility>

namespace lmx::render {
using temporal_detail::kResolveDepthSlot;
using temporal_detail::kResolveExposureSlot;
using temporal_detail::kResolveHistorySlot;
using temporal_detail::kResolveMotionSlot;
using temporal_detail::kResolveParamsSlot;
using temporal_detail::kResolvePreviousDepthSlot;
using temporal_detail::kResolveReprojectedSlot;
using temporal_detail::kResolveSamplerSlot;
using temporal_detail::temporalUpscaleParams;

//======================================================================================================================
ReconstructionSelection TemporalResolve::prepare(ReconstructionMode requested, uint32_t width,
                                                 uint32_t height) {
    return m_vendor->prepare(requested, width, height);
}

//======================================================================================================================
void TemporalResolve::recordDisabledFrame() {
    m_vendor->recordMode(ReconstructionMode::Raw);
    m_depthUse[0] = rojoRHI::TextureUse::ShaderRead;
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
        temporalUpscaleParams(inputs, inputs.resetReason == HistoryResetReason::None, 2);
    graph.addComputePass(
        "lmx.pass.temporal.reprojectedHistory", std::move(desc),
        [this, &commands, inputs, target, params](const PassResources& resources) {
            const auto texture = [&](GraphTexture handle) {
                auto result = resources.texture(handle);
                LMX_ASSERT(result.has_value(), result.error().message);
                return *result;
            };
            auto exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);
            commands.bindComputePipeline(m_vendor->historyPipeline());
            commands.bindTexture(kResolveDepthSlot, *texture(inputs.depth));
            commands.bindTexture(kResolvePreviousDepthSlot, *texture(inputs.previousDepth));
            commands.bindTexture(kResolveMotionSlot, *texture(inputs.motion));
            commands.bindTexture(kResolveHistorySlot, *texture(inputs.history));
            commands.bindStorageTexture(kResolveReprojectedSlot, *texture(target), {},
                                        rojoRHI::StorageAccess::Write);
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure,
                                       rojoRHI::StorageAccess::Read);
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, params);
            const auto groups = dispatchGroups2D(params.outputWidth, params.outputHeight);
            commands.dispatch(groups[0], groups[1], 1);
        });
    return nextVersion(target);
}

} // namespace lmx::render
