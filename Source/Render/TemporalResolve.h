//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolve.h
/// @brief Declares the temporal reconstruction stage: its inputs, outputs, histories and passes.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "RHI/RHI.h"
#include "Render/RenderGraph.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"

#include <cstdint>
#include <memory>

namespace lmx::render {

/// How a temporal frame turns its inputs into the image the display transform reads.
enum class ReconstructionMode : uint8_t {
    /// The raw jittered frame reaches bloom and display, and a copy of it becomes the history.
    /// It shares every input with NativeTaa, which is what lets a test compare the two at
    /// identical inputs, and switching between the modes needs no reset because both leave a real
    /// frame in the colour slot.
    Raw,
    /// The resolve accumulates this frame over the reprojected history and writes the colour slot
    /// directly, so this frame's output is the next frame's history and there is no copy.
    NativeTaa
};

/// What the temporal passes draw into the display target instead of the frame's own picture. Off
/// is the shipped image; every other view overwrites it with a diagnostic and is meant to be read
/// rather than looked at -- except ReprojectedHistory, which is a colour signal and is encoded
/// through the display pass's own tone map and transfer so the two compare side by side.
enum class TemporalDebugView : uint8_t {
    Off,                ///< The display transform's own output reaches the viewport unchanged.
    MotionVectors,      ///< Motion recentred on grey, with the invalid sentinel drawn magenta.
    ReprojectionError,  ///< The reprojected history's difference from this frame's scene colour.
    ReprojectedHistory, ///< The exposure-corrected history, tone mapped; rejected pixels blue.
    RejectionMask,      ///< Flat colours per rejection reason, with the clipped flag added green.
    BlendWeight,        ///< Grey: how much of this frame the blend kept.
    HistoryAge          ///< Grey: the per-pixel accumulation age over the warmup period.
};

/// Everything one temporal frame hands the reconstruction stage. Graph handles are this frame's
/// versions; values are this frame's, with the previous frame's where the algorithm needs both.
struct TemporalInputs {
    GraphTexture sceneColor;    ///< RGBA16Float, pre-exposed scene-linear, jittered raster.
    GraphTexture depth;         ///< D32Float, reversed, this frame's slot.
    GraphTexture previousDepth; ///< D32Float, the other slot (previous declared temporal frame).
    GraphTexture motion;        ///< RG16Float, the ADR 0013 convention.
    GraphTexture reactive;      ///< R8Unorm: 0 accumulates freely, 1 rejects history.
    GraphTexture history; ///< RGBA16Float, the previous colour slot; valid when reset is None.
    /// RGBA16Float, this frame's colour slot -- what the frame's output is written into, by the
    /// resolve directly under NativeTaa and by the raw commit copy under Raw. It is the handle
    /// TemporalResolveOutputs::resolved is a later version of, and the invariant "every temporal
    /// frame's colour slot holds that frame's output" is what makes the pair symmetric.
    GraphTexture colorSlot;
    /// The exposure pair `{applied, previous}` at the version the scene pass read -- after the
    /// seed, so "applied this frame" means the same thing to shading, metering and the resolve.
    GraphBuffer exposure;
    CameraFrameState camera; ///< This frame's camera, unjittered where the resolve reads it.
    CameraFrameState previousCamera; ///< The previous declared temporal frame's camera.
    FrameExtents extents;            ///< This frame's render and output extents.
    /// The previous declared temporal frame's extents, equal to `extents` on a reset frame,
    /// because the previous depth slot is addressed at the extent it was rendered at.
    FrameExtents previousExtents;
    HistoryResetReason resetReason = HistoryResetReason::None; ///< Why history may not be reused.
    ReconstructionMode mode = ReconstructionMode::Raw;         ///< Which path this frame takes.
};

/// The handles the stage produces. The two diagnostics are graph transients declared only when a
/// debug view sinks them, so a frame showing no view leaves them default-constructed.
struct TemporalResolveOutputs {
    GraphTexture resolved;    ///< This frame's colour slot: the resolve's output, or the raw copy.
    GraphTexture rejection;   ///< RGBA8Unorm transient: reason, blend weight, clipped flag.
    GraphTexture reprojected; ///< RGBA16Float transient: the exposure-corrected history.
};

/// Declared temporal frames a history takes to converge: one jitter period.
constexpr uint32_t kTemporalWarmupFrames = 16;
/// Share of this frame a still pixel keeps, and so the accumulation's steady-state time constant.
constexpr float kBaseAlpha = 0.1f;
/// Share of this frame a pixel moving kMotionAlphaPixels or more keeps.
constexpr float kMaxMotionAlpha = 0.25f;
/// Per-frame motion, in pixels, at which the blend reaches kMaxMotionAlpha.
constexpr float kMotionAlphaPixels = 8.0f;
/// Standard deviations of the 3x3 neighbourhood the history is clipped into.
constexpr float kClipGamma = 1.0f;
/// Relative view-distance disagreement at which a history fetch counts as a disocclusion.
constexpr float kDisocclusionTolerance = 0.05f;
/// Distance in render texels, from an output pixel's centre to this frame's jittered sample, at
/// which the sample-proximity term reaches its floor.
constexpr float kUpscaleSampleRadius = 1.0f;
/// Share of the motion-derived blend weight a pixel keeps at kUpscaleSampleRadius: a pixel that
/// fell between samples trusts this frame less rather than not at all.
constexpr float kUpscaleMinWeight = 0.25f;
/// Pre-exposed emissive luminance the scene pass maps to a fully reactive pixel.
constexpr float kReactiveEmissiveScale = 4.0f;
/// Storage format of `lmx.render.reactive`: one byte of "do not accumulate me" per pixel.
constexpr rhi::Format kReactiveFormat = rhi::Format::R8Unorm;

/// Why a pixel could not reuse its history, in the order Shaders/TemporalResolve.slang and
/// Shaders/TemporalUpscale.slang test them:
/// the first that applies wins. The rejection transient stores `code / 255` in its red channel,
/// with kRejectionClippedBit set alongside when the neighbourhood clip moved the history.
constexpr uint32_t kRejectionReasonNone = 0;        ///< The history was reprojected and blended.
constexpr uint32_t kRejectionReasonOffScreen = 1;   ///< The history position left the extent.
constexpr uint32_t kRejectionReasonInvalid = 2;     ///< Motion carried the undefined sentinel.
constexpr uint32_t kRejectionReasonDisoccluded = 3; ///< The previous frame saw another surface.
constexpr uint32_t kRejectionReasonReactive = 4;    ///< The pixel asked not to accumulate at all.
constexpr uint32_t kRejectionClippedBit = 0x80;     ///< Flag: the neighbourhood clip moved it.

/// The frame's temporal reconstruction stage: the two history pairs, the pipelines that read and
/// write them, and the passes that make one frame's picture out of the last one's.
///
/// Renderer-owned and composed rather than inherited from: the Renderer builds `TemporalInputs`
/// from its own graph handles, calls declare(), and routes the result. Nothing outside reads the
/// stage's internals, which is what leaves M6.4's upscaler free to replace the whole of it behind
/// the same inputs.
class TemporalResolve {
public:
    /// `cpuReadback` puts both history pairs in shared storage so Texture::readback() works, on
    /// Renderer::create()'s terms: it is what the GPU tests and the offscreen path need, and the
    /// windowed App leaves it false.
    static rhi::Result<std::unique_ptr<TemporalResolve>> create(rhi::Device& device,
                                                                bool cpuReadback);

    /// Non-copyable: it owns GPU targets whose identity is what the frame's imports name.
    TemporalResolve(const TemporalResolve&) = delete;
    /// Non-assignable, on the copy constructor's terms.
    TemporalResolve& operator=(const TemporalResolve&) = delete;

    /// Recreates both slot pairs at the new extent, replacing any already held. The caller
    /// guarantees the GPU is idle first, on Renderer::resize()'s terms.
    rhi::Result<void> resize(uint32_t width, uint32_t height);

    /// The depth target of `slot`, which a temporal frame renders into and the next one samples as
    /// its previous depth. Never null after a successful create().
    rhi::Texture& depthSlot(uint32_t slot);

    /// The colour history of `slot`, which holds that frame's output whichever mode produced it.
    rhi::Texture& colorSlot(uint32_t slot);

    /// Imports the depth of `slot`. Stated as ShaderRead for the reason M6.1's single depth target
    /// was: the depth is public through Renderer::depthTarget() and a caller may sample it after
    /// this graph, and that stage set also covers the attachment work the scene pass does with it.
    GraphTexture importDepth(RenderGraph& graph, uint32_t slot);

    /// Imports the colour history of `slot` with the terminal use recordFrame() last recorded for
    /// it, so a mode switch between two frames imports the slot with the use the other mode
    /// actually left it in.
    GraphTexture importColor(RenderGraph& graph, uint32_t slot);

    /// Declares the reprojection diagnostic (when history is valid; culled unless the
    /// ReprojectionError view sinks it), then the reconstruction the frame's mode and extents
    /// select -- the resolve or the upscale under NativeTaa, the copy or the spatial commit under
    /// Raw -- then the debug view over `displayResult`. Upscaling is a property of
    /// `TemporalInputs::extents`, not of the mode: both modes keep their meaning at every scale.
    ///
    /// The native `lmx.pass.temporal.resolve` runs only where `extents.render == extents.output`
    /// and either the frame resets or `previousExtents` matches `extents`. The native kernel reads
    /// the previous depth slot at *this* frame's extent, so a scale-1 frame whose predecessor
    /// rasterised at another size would test disocclusion against a slot addressed at the wrong
    /// extent; that one frame takes `lmx.pass.temporal.upscale` instead, which carries the previous
    /// render extent explicitly. A steady scale-1 frame therefore declares exactly what M6.2
    /// declared.
    ///
    /// `displayResult` names the display version the debug view draws over on entry -- the version
    /// the display pass produces, which the caller may not have declared yet -- and is replaced by
    /// the version the debug view produced. The graph's schedule is topological, so declaring the
    /// view against a version its producer has yet to declare still orders it after that producer.
    TemporalResolveOutputs declare(RenderGraph& graph, rhi::CommandList& commands,
                                   const TemporalInputs& inputs, TemporalDebugView debugView,
                                   GraphTexture& displayResult);

    /// Records what the frame just declared left in each colour slot, so the next frame's imports
    /// state the use its barriers must be derived against. `historyValid` is what decides whether
    /// the reprojection diagnostic survived culling, and so whether the other slot was read at all;
    /// `upscaled` is what turns a Raw frame's copy into the spatial pass whose output bloom and the
    /// display transform then sample, so the slot ends the frame read rather than written.
    void recordFrame(uint32_t slot, ReconstructionMode mode, TemporalDebugView debugView,
                     bool historyValid, bool upscaled);

    /// Bytes both colour history slots hold; the allocation is permanent for the stage's life.
    uint64_t colorBytes() const;

    /// Bytes both depth slots hold, on colorBytes()' terms.
    uint64_t depthBytes() const;

private:
    explicit TemporalResolve(rhi::Device& device, bool cpuReadback)
        : m_device(device), m_cpuReadback(cpuReadback) {}

    // The reprojection diagnostic (ADR 0013), unchanged from M6.1 but now reading the previous
    // colour slot: it measures the motion vectors and the history, and nothing downstream shades
    // from it, so it is culled unless the ReprojectionError view keeps it alive.
    GraphTexture declareReprojection(RenderGraph& graph, rhi::CommandList& commands,
                                     const TemporalInputs& inputs);

    // The accumulation itself. The two diagnostic transients are created and written only when
    // `rejectionWanted`/`reprojectedWanted` say a debug view sinks them, which is what keeps a
    // shipped frame from paying for either.
    void declareResolve(RenderGraph& graph, rhi::CommandList& commands,
                        const TemporalInputs& inputs, bool rejectionWanted, bool reprojectedWanted,
                        TemporalResolveOutputs& outputs);

    // M6.1's copy, now mode-gated: under Raw this frame's raw scene colour becomes the colour slot,
    // which keeps "every temporal frame's colour slot holds that frame's output" true in both
    // modes. Its consumer is the next frame, so the version it produces is exported. Declared only
    // when the render extent is the output one; otherwise the spatial pass below stands in for it,
    // because a copy of a smaller rectangle would leave the rest of the slot stale.
    GraphTexture declareHistoryCommit(RenderGraph& graph, rhi::CommandList& commands,
                                      const TemporalInputs& inputs);

    // The Raw mode's upscaled commit: Shaders/SpatialUpscale.slang resamples the active rectangle
    // of this frame's scene colour into the whole colour slot. Same invariant as the copy it
    // replaces -- this frame's colour slot holds this frame's output -- and the same export, since
    // the consumer is the next frame.
    GraphTexture declareSpatialCommit(RenderGraph& graph, rhi::CommandList& commands,
                                      const TemporalInputs& inputs);

    // The NativeTaa mode's upscaling accumulation: Shaders/TemporalUpscale.slang over the output
    // extent, reading the render extent's active rectangle. Same declaration, diagnostics and
    // export as declareResolve(); it also serves the one scale-1 frame whose predecessor ran at
    // another render extent, which is why it carries the previous render extent in its block.
    void declareUpscale(RenderGraph& graph, rhi::CommandList& commands,
                        const TemporalInputs& inputs, bool rejectionWanted, bool reprojectedWanted,
                        TemporalResolveOutputs& outputs);

    // A raster pass rather than a compute one: the display target is BGRA8Unorm, which carries no
    // storage-write usage in this RHI, and a fullscreen triangle overwrites every texel of it just
    // as a dispatch would. The target is not an sRGB view, so the shader's encodings reach the
    // bytes unchanged.
    GraphTexture declareDebugView(RenderGraph& graph, rhi::CommandList& commands,
                                  TemporalDebugView debugView, const TemporalInputs& inputs,
                                  const TemporalResolveOutputs& outputs, GraphTexture diagnostic,
                                  bool readsDiagnostic, GraphTexture displayResult);

    rhi::Device& m_device;
    std::unique_ptr<rhi::ShaderLibrary> m_reprojectLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_resolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_debugViewLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_spatialUpscaleLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_temporalUpscaleLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_reprojectPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_resolvePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_spatialUpscalePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_temporalUpscalePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_debugViewPipeline;
    // Clamped, not wrapped: a history fetch lands where this frame's motion points, which for a
    // border texel is a bilinear footprint reaching past the edge. A wrapping sampler would fold
    // the opposite edge's texels into that fetch and report a difference that is an artefact of
    // the addressing rather than of the motion.
    std::unique_ptr<rhi::Sampler> m_sampler;
    // Ping-pong by declared temporal frame parity. Every temporal frame renders depth into
    // m_depth[slot], reads m_depth[1 - slot] as its previous depth, and writes m_color[slot] while
    // reading m_color[1 - slot] -- so this frame's colour slot always holds this frame's output.
    std::unique_ptr<rhi::Texture> m_depth[2];
    std::unique_ptr<rhi::Texture> m_color[2];
    // The 1x1 storage-write texture the resolve binds where a diagnostic is not declared. The
    // argument table entry must hold a valid writable texture even on the frames the kernel's
    // corresponding writeDiagnostics bit makes it write nothing.
    std::unique_ptr<rhi::Texture> m_diagnosticFallback;
    // The 1x1 sampled texture the debug view binds for whichever inputs its view does not read.
    // Every texel the shader loads lies outside it, and an out-of-bounds Load answers with zeroes.
    std::unique_ptr<rhi::Texture> m_viewFallback;
    // What the last frame to touch each colour slot left it as. CopyDestination is what M6.1's
    // commit left, and what an untouched slot is imported as before any frame has written it.
    rhi::TextureUse m_colorUse[2] = {rhi::TextureUse::CopyDestination,
                                     rhi::TextureUse::CopyDestination};
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
