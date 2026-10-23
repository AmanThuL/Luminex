//----------------------------------------------------------------------------------------------------------------------
/// @file SceneFrameData.cpp
/// @brief Builds the scene, sky and local-light frame data from plain values.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneFrameData.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lmx::render {
using namespace scene_detail;
namespace {

//======================================================================================================================
// A zero-light frame must not index even a fallback grid.
engine::LocalLightMode resolveLocalLightMode(engine::LocalLightMode requested,
                                             uint32_t liveLightCount, bool hasClusters) {
    if (requested == engine::LocalLightMode::Off || liveLightCount == 0)
        return engine::LocalLightMode::Off;
    LMX_ASSERT(requested != engine::LocalLightMode::Clustered || hasClusters,
               "clustered shading requires this frame's grid and list");
    return requested;
}

//======================================================================================================================
DirLightUniform toUniform(const engine::DirectionalLight& light) {
    return {.strength = light.strength,
            .strengthPadding = 0.0f,
            .direction = light.direction,
            .directionPadding = 0.0f};
}

} // namespace

//======================================================================================================================
PassUniforms buildPassUniforms(const PassUniformInputs& inputs) {
    PassUniforms passUniforms{};
    passUniforms.viewProj = inputs.temporalEnabled ? inputs.camera.viewProjectionJittered
                                                   : inputs.camera.viewProjection;
    passUniforms.shadowTransform = inputs.shadowTransform;
    passUniforms.eyePos = inputs.eyePosition;
    passUniforms.time = inputs.timeSeconds;
    passUniforms.preExposure = std::exp2(inputs.exposureEv);
    for (size_t i = 0; i < std::size(passUniforms.lights); ++i) {
        passUniforms.lights[i] = toUniform(inputs.lights[i]);
    }
    passUniforms.shadowCaster = inputs.shadowCaster;
    passUniforms.shadowFilter =
        inputs.shadowFilter == ShadowFilter::PCSS ? kShadowFilterPcss : kShadowFilterPcf;
    passUniforms.viewProjUnjittered = inputs.camera.viewProjection;
    passUniforms.previousViewProjUnjittered = inputs.previousCamera.viewProjection;
    return passUniforms;
}

//======================================================================================================================
LocalLightParams buildLocalLightParams(const LocalLightParamInputs& inputs) {
    // The render area is origin-anchored; lookup uses its active extent and reversed-Z boundaries.
    const engine::LocalLightMode localLightMode =
        resolveLocalLightMode(inputs.requestedMode, inputs.liveLightCount, inputs.hasClusters);
    LocalLightParams localLightParams{
        .mode = static_cast<uint32_t>(localLightMode),
        .rowCount = localLightMode == engine::LocalLightMode::Off ? 0u : inputs.lightRowCount,
        .gridX = kClusterTilesX,
        .gridY = kClusterTilesY,
        .gridZ = kClusterSliceCount,
        .activeOriginX = 0,
        .activeOriginY = 0,
        .activeWidth = inputs.extents.renderWidth,
        .activeHeight = inputs.extents.renderHeight,
        .sliceDepth = {}};
    LMX_ASSERT(inputs.nearZ > 0.0f, "the froxel slice table needs a positive near plane");
    const auto sliceDepths = clusterSliceDepths(inputs.nearZ);
    std::copy(sliceDepths.begin(), sliceDepths.end(), std::begin(localLightParams.sliceDepth));
    return localLightParams;
}

//======================================================================================================================
SkyUniforms buildSkyUniforms(const SkyUniformInputs& inputs) {
    return {.viewProj = inputs.viewProj,
            .eyePos = inputs.eyePos,
            .eyePadding = 0.0f,
            .preExposure = inputs.preExposure,
            .jitterNdcX = inputs.jitterNdc.x,
            .jitterNdcY = inputs.jitterNdc.y,
            .jitterPadding = 0.0f,
            .previousViewProj = inputs.previousViewProj,
            .previousEyePos = inputs.previousEyePos,
            .previousEyePosPadding = 0.0f};
}

} // namespace lmx::render
