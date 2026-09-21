//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStage.cpp
/// @brief Packs scene uniforms and declares scene attachments and dependencies.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Color.h"
#include "Render/Passes/Scene/SceneStageInternal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

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
GraphTexture SceneStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                 const SceneView& view, const SceneStageInputs& inputs) {
    const bool temporalEnabled = inputs.temporalEnabled;
    const auto& cameraState = inputs.camera;
    const auto& previousCamera = inputs.previousCamera;
    const auto& extents = inputs.extents;
    const auto& jitterPixels = inputs.jitterPixels;
    const auto& clearColor = inputs.clearColor;
    const bool upscaled =
        extents.renderWidth != extents.outputWidth || extents.renderHeight != extents.outputHeight;
    const GraphTexture shadowRead = inputs.shadowRead;
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture sceneDepth = inputs.sceneDepth;
    const GraphTexture motionTargetHandle = inputs.motion;
    const GraphTexture reactiveTargetHandle = inputs.reactive;
    const GraphBuffer exposureCurrent = inputs.exposure;

    // Rasterisation takes the jitter; motion never does. With temporal off the two are the same
    // matrix, derived exactly as this frame's projection * view was before jitter existed.
    const glm::mat4 viewProj =
        temporalEnabled ? cameraState.viewProjectionJittered : cameraState.viewProjection;

    // One stop is one doubling, so the slider's unit becomes a multiply here. This is what every
    // fragment applies in manual mode -- ScenePass.slang/Sky.slang, unchanged from before auto-
    // exposure existed, which is what parity with pre-M5 output when auto is off rests on. Auto
    // mode binds the ScenePassAuto.slang/SkyAuto.slang pipelines below, which multiply by
    // gExposureOverride instead (spec 9) and never read PassUniforms.preExposure at all. This CPU
    // value still seeds a reset frame's exposure buffer above and pre-exposes the clear colour
    // below.
    const float preExposure = std::exp2(view.exposureEv);

    PassUniforms passUniforms{};
    passUniforms.viewProj = viewProj;
    passUniforms.shadowTransform = inputs.shadowTransform;
    passUniforms.eyePos = inputs.eyePosition;
    passUniforms.time = inputs.timeSeconds;
    passUniforms.preExposure = preExposure;
    for (size_t i = 0; i < std::size(passUniforms.lights); ++i) {
        passUniforms.lights[i] = toUniform(view.lights[i]);
    }
    passUniforms.shadowFilter =
        view.shadowFilter == ShadowFilter::PCSS ? kShadowFilterPcss : kShadowFilterPcf;
    passUniforms.viewProjUnjittered = cameraState.viewProjection;
    passUniforms.previousViewProjUnjittered = previousCamera.viewProjection;

    LMX_ASSERT(inputs.lightGrid.has_value() == inputs.lightIndices.has_value(),
               "cluster grid and index list must be supplied together");
    // The render area is origin-anchored; lookup uses its active extent and reversed-Z boundaries.
    const engine::LocalLightMode localLightMode =
        resolveLocalLightMode(view.localLightMode, view.tables.liveLightCount,
                              inputs.lightGrid.has_value() && inputs.lightIndices.has_value());
    LocalLightParams localLightParams{
        .mode = static_cast<uint32_t>(localLightMode),
        .rowCount = localLightMode == engine::LocalLightMode::Off ? 0u : view.tables.lightRowCount,
        .gridX = kClusterTilesX,
        .gridY = kClusterTilesY,
        .gridZ = kClusterSliceCount,
        .activeOriginX = 0,
        .activeOriginY = 0,
        .activeWidth = extents.renderWidth,
        .activeHeight = extents.renderHeight,
        .sliceDepth = {}};
    LMX_ASSERT(cameraState.nearZ > 0.0f, "the froxel slice table needs a positive near plane");
    const auto sliceDepths = clusterSliceDepths(cameraState.nearZ);
    std::copy(sliceDepths.begin(), sliceDepths.end(), std::begin(localLightParams.sliceDepth));

    // The clear has to be the value a fragment writing that colour would have produced, or the
    // background and the geometry would disagree about what space the target holds. That means
    // both steps a fragment takes: the authored display-space colour decodes to linear (once,
    // here), and it is pre-exposed like everything else -- without the second multiply the
    // background would sit still while an exposure change moved every shaded pixel.
    //
    // This always pre-exposes by the *manual* value, even in auto mode: the clear is a CPU-baked
    // hardware clear value, and auto mode's actual exposure lives only in the GPU-side exposure
    // buffer (that is the whole point of not reading it back). In practice this is a non-issue --
    // every scene with a sky draws over the clear entirely -- and is strictly better than the
    // alternative of a blocking readback just to keep an unshaded background pixel exact.
    const glm::vec3 clearLinear =
        lmx::srgbToLinear(glm::vec3(clearColor[0], clearColor[1], clearColor[2])) * preExposure;

    PassDesc sceneDesc;
    sceneDesc.textureReads.push_back(shadowRead);
    sceneDesc.bufferReads.assign(inputs.sceneBuffers.begin(), inputs.sceneBuffers.end());
    sceneDesc.bufferReads.push_back(inputs.drawRows);
    sceneDesc.indirectBufferReads.push_back(inputs.drawArguments);
    if (inputs.lights.has_value()) {
        sceneDesc.bufferReads.push_back(*inputs.lights);
    }
    if (inputs.lightGrid) {
        sceneDesc.bufferReads.push_back(*inputs.lightGrid);
        sceneDesc.bufferReads.push_back(*inputs.lightIndices);
    }
    // Declared only in auto mode: manual mode's shading never reads the feedback buffer (spec 9),
    // so declaring the read here always would be a lie about what the pass depends on.
    if (view.autoExposureEnabled) {
        sceneDesc.bufferReads.push_back(exposureCurrent);
    }
    sceneDesc.color =
        ColorAttachment{.handle = sceneColor,
                        .load = LoadOp::Clear,
                        .store = StoreOp::Store,
                        .clearColor = {clearLinear.r, clearLinear.g, clearLinear.b, clearColor[3]}};
    // 0 is the reversed projection's horizon -- no geometry is ever farther, so every fragment's
    // Greater test passes against a cleared texel, and the sky's GreaterEqual matches it exactly.
    //
    // Stored rather than discarded: nothing in this frame reads it after the pass, but the buffer
    // is the frame's own record of where its geometry is, and discarding leaves it undefined the
    // moment the pass ends -- so depthTarget() would hand a caller garbage rather than depth.
    sceneDesc.depth = DepthAttachment{
        .handle = sceneDepth, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = 0.0f};
    // Attachment 1 on the temporal path only. Zero is the motion of a surface that did not move,
    // which is the right value for the pixels no draw covers: a consumer reading the clear
    // reprojects onto itself rather than onto a neighbour.
    if (temporalEnabled) {
        sceneDesc.extraColor.push_back(ColorAttachment{.handle = motionTargetHandle,
                                                       .load = LoadOp::Clear,
                                                       .store = StoreOp::Store,
                                                       .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}});
        // Attachment 2, on the same terms. Zero is "accumulate freely", which is the right value
        // for a texel no draw covers: the clear colour has no emissive that could switch on.
        sceneDesc.extraColor.push_back(ColorAttachment{.handle = reactiveTargetHandle,
                                                       .load = LoadOp::Clear,
                                                       .store = StoreOp::Store,
                                                       .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}});
    }
    // The active rectangle, declared only when it is not the whole attachment: at scale 1 the pass
    // states exactly what it always did, which is what keeps the frame's declaration -- and every
    // golden over it -- byte for byte the one M6.2 made.
    if (upscaled) {
        sceneDesc.renderAreaWidth = extents.renderWidth;
        sceneDesc.renderAreaHeight = extents.renderHeight;
    }
    // The sky's own jitter, in NDC: its motion pair has to stay unjittered, so its vertex entry
    // point offsets the rasterised position instead of carrying the jitter in its matrix.
    const glm::vec2 jitterNdc{2.0f * jitterPixels.x / static_cast<float>(extents.renderWidth),
                              2.0f * jitterPixels.y / static_cast<float>(extents.renderHeight)};
    graph.addPass("lmx.pass.scene", std::move(sceneDesc),
                  [this, &commands, view, inputs, passUniforms, localLightParams, shadowRead,
                   exposureCurrent, temporalEnabled, cameraState, previousCamera,
                   jitterNdc](const PassResources& resources) {
                      draw(commands, view, inputs, passUniforms, localLightParams, shadowRead,
                           exposureCurrent, temporalEnabled, cameraState, previousCamera, jitterNdc,
                           resources);
                  });

    return nextVersion(sceneColor);
}

} // namespace lmx::render
