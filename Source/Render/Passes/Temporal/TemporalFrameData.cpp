//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalFrameData.cpp
/// @brief Builds temporal kernel blocks from camera, history and pixel-extent facts.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Temporal/TemporalFrameData.h"

namespace lmx::render {

//======================================================================================================================
temporal_detail::TemporalResolveParams
buildTemporalResolveParams(const TemporalResolveParamsInputs& inputs) {
    return {.width = inputs.extents.outputWidth,
            .height = inputs.extents.outputHeight,
            .historyValid = inputs.historyValid ? 1u : 0u,
            .writeDiagnostics =
                (inputs.rejectionWanted ? 1u : 0u) | (inputs.reprojectedWanted ? 2u : 0u),
            .inverseViewProjection = inputs.camera.inverseViewProjection,
            .previousViewProjection = inputs.previousCamera.viewProjection,
            .previousNearZ = inputs.previousCamera.nearZ};
}

//======================================================================================================================
temporal_detail::TemporalUpscaleParams
buildTemporalUpscaleParams(const TemporalUpscaleParamsInputs& inputs) {
    return {.width = inputs.extents.renderWidth,
            .height = inputs.extents.renderHeight,
            .historyValid = inputs.historyValid ? 1u : 0u,
            .writeDiagnostics =
                (inputs.rejectionWanted ? 1u : 0u) | (inputs.reprojectedWanted ? 2u : 0u),
            .inverseViewProjection = inputs.camera.inverseViewProjection,
            .previousViewProjection = inputs.previousCamera.viewProjection,
            .previousNearZ = inputs.previousCamera.nearZ,
            .outputWidth = inputs.extents.outputWidth,
            .outputHeight = inputs.extents.outputHeight,
            .allocatedWidth = inputs.extents.outputWidth,
            .allocatedHeight = inputs.extents.outputHeight,
            .previousRenderWidth = inputs.previousExtents.renderWidth,
            .previousRenderHeight = inputs.previousExtents.renderHeight,
            .jitterOffset = jitterTexelOffset(inputs.camera.jitterPixels)};
}

//======================================================================================================================
temporal_detail::SpatialUpscaleParams buildSpatialUpscaleParams(const FrameExtents& extents,
                                                                glm::vec2 jitterPixels) {
    return {.renderWidth = extents.renderWidth,
            .renderHeight = extents.renderHeight,
            .outputWidth = extents.outputWidth,
            .outputHeight = extents.outputHeight,
            .allocatedWidth = extents.outputWidth,
            .allocatedHeight = extents.outputHeight,
            .jitterOffset = jitterTexelOffset(jitterPixels)};
}

//======================================================================================================================
temporal_detail::TemporalReprojectParams buildTemporalReprojectParams(const FrameExtents& extents,
                                                                      glm::vec2 jitterPixels) {
    const auto resampling = buildSpatialUpscaleParams(extents, jitterPixels);
    return {.width = resampling.outputWidth,
            .height = resampling.outputHeight,
            .renderWidth = resampling.renderWidth,
            .renderHeight = resampling.renderHeight,
            .allocatedWidth = resampling.allocatedWidth,
            .allocatedHeight = resampling.allocatedHeight,
            .jitterOffset = resampling.jitterOffset};
}

//======================================================================================================================
temporal_detail::TemporalDebugViewParams buildTemporalDebugViewParams(TemporalDebugView view,
                                                                      const FrameExtents& extents) {
    return {.view = view == TemporalDebugView::Off ? 1u : static_cast<uint32_t>(view),
            .renderWidth = extents.renderWidth,
            .renderHeight = extents.renderHeight,
            .outputWidth = extents.outputWidth,
            .outputHeight = extents.outputHeight};
}

//======================================================================================================================
temporal_detail::VendorPackParams buildVendorPackParams(const FrameExtents& extents) {
    return {.width = extents.renderWidth, .height = extents.renderHeight};
}

} // namespace lmx::render
