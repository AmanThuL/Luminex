//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterFrameData.cpp
/// @brief Builds the light clustering and light debug frame data from plain values.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/LocalLights/LightClusterFrameData.h"

namespace lmx::render {

//======================================================================================================================
LightClusterKernelParams buildLightClusterKernelParams(const LightClusterParams& clusters) {
    LightClusterKernelParams params{.rowCount = clusters.rowCount,
                                    .activeWidth = clusters.activeWidth,
                                    .activeHeight = clusters.activeHeight,
                                    .perClusterCap = clusters.perClusterCap,
                                    .globalCapacity = clusters.globalCapacity,
                                    .froxelCount = kClusterCount};
    for (uint32_t column = 0; column < 4; ++column) {
        params.view[column] = clusters.view[int(column)];
        params.inverseProjection[column] = clusters.inverseJitteredProjection[int(column)];
    }
    for (uint32_t boundary = 0; boundary < kClusterSliceBoundaryCount; ++boundary) {
        params.sliceDepth[boundary / 4][int(boundary % 4)] = clusters.sliceDepth[boundary];
    }
    return params;
}

//======================================================================================================================
LightDebugParams buildLightDebugParams(const LightDebugParamsInputs& inputs) {
    return LightDebugParams{inputs.inverseViewProjection, uint32_t(inputs.mode),
                            inputs.clusters.rowCount,     inputs.clusters.activeWidth,
                            inputs.clusters.activeHeight, inputs.outputWidth,
                            inputs.outputHeight,          inputs.clusters.sliceDepth};
}

} // namespace lmx::render
