//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStage.cpp
/// @brief Packs scene uniforms and declares scene attachments and dependencies.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Color.h"
#include "Render/Passes/Scene/SceneStageInternal.h"

#include <utility>

namespace lmx::render {
using namespace scene_detail;

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
    //
    // One stop is one doubling, so the slider's unit becomes a multiply in the builder. This is
    // what every fragment applies in manual mode -- ScenePass.slang/Sky.slang, unchanged from
    // before auto-exposure existed, which is what parity with pre-M5 output when auto is off rests
    // on. Auto mode binds the ScenePassAuto.slang/SkyAuto.slang pipelines below, which multiply by
    // gExposureOverride instead (spec 9) and never read PassUniforms.preExposure at all. This CPU
    // value still seeds a reset frame's exposure buffer above and pre-exposes the clear colour
    // below.
    const PassUniforms passUniforms = buildPassUniforms({.camera = cameraState,
                                                         .previousCamera = previousCamera,
                                                         .temporalEnabled = temporalEnabled,
                                                         .shadowTransform = inputs.shadowTransform,
                                                         .eyePosition = inputs.eyePosition,
                                                         .timeSeconds = inputs.timeSeconds,
                                                         .exposureEv = view.exposureEv,
                                                         .lights = view.lights,
                                                         .shadowCaster = view.shadowCaster,
                                                         .shadowFilter = view.shadowFilter});
    const float preExposure = passUniforms.preExposure;

    LMX_ASSERT(inputs.lightGrid.has_value() == inputs.lightIndices.has_value(),
               "cluster grid and index list must be supplied together");
    const LocalLightParams localLightParams = buildLocalLightParams(
        {.requestedMode = view.localLightMode,
         .liveLightCount = view.tables.liveLightCount,
         .lightRowCount = view.tables.lightRowCount,
         .hasClusters = inputs.lightGrid.has_value() && inputs.lightIndices.has_value(),
         .extents = extents,
         .nearZ = cameraState.nearZ});

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
