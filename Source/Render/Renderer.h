//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.h
/// @brief Declares frame-rendering orchestration and pass contracts.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/DisplayDomain.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph.h"
#include "Render/SceneStage.h"
#include "Render/SceneView.h"
#include "Render/ShadowStage.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"
#include "Render/TemporalResolve.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace lmx::render {

/// Capture tooling, not part of rendering: publishes the draw stages' uniform-block layouts
/// (including masked variants) (name, slot, size, every field's offset and type) to
/// rhi::debug::CaptureSchema, so the capture sidecar can name the bytes a .gputrace holds instead
/// of leaving them a hex dump.
///
/// Free-standing and idempotent -- re-registering a struct replaces it -- because the layouts
/// describe the *shaders*, not any one Renderer: Renderer::create calls it, and a caller with no
/// Renderer (a test, a tool) may call it directly.
void registerUniformLayoutsForCapture();

/// The two colour formats the frame runs through, named because three places have to agree on
/// each: the texture the renderer creates, the pipeline compiled to render into it, and the format
/// declared when the graph imports it. A literal in any one of those is a trap -- the graph checks
/// attachment roles against what it was told, not against the texture.
///
/// The scene renders in half float because that is what holds radiance above 1.0; the display
/// target is the 8-bit surface the viewport, the swapchain, and the screenshot all expect.
constexpr rhi::Format kSceneColorFormat = rhi::Format::RGBA16Float;
/// Eight-bit BGRA storage of kSdrDisplayDomain; UNORM stores its already-encoded sRGB bytes.
constexpr rhi::Format kDisplayFormat = rhi::Format::BGRA8Unorm;
static_assert(kDisplayFormat == rhi::Format::BGRA8Unorm && kSdrDisplayDomain.bitsPerChannel == 8,
              "display storage must match the named domain's channel precision");

/// Floats in the persistent exposure buffer: `{ applied, previous }`. `applied` is what the scene
/// pass multiplies by this frame; `previous` is what it multiplied by on the previous declared
/// temporal frame. The buffer is the single source of both, in manual and auto mode alike, so a
/// temporal resolve correcting a history for the exposure it was recorded at never needs a
/// CPU-remembered value that could disagree with what the GPU actually applied across a mode
/// switch. Shaders/ExposureSeed.slang and Shaders/ExposureResolve.slang write the pair;
/// Shaders/HistogramAccumulate.slang and the auto scene/sky pipelines read index 0.
constexpr uint32_t kExposureBufferFloats = 2;

/// Owns frame targets and pipelines and declares the frame's render-graph passes.
class Renderer {
public:
    /// cpuReadback puts the color target in shared storage so Texture::readback() works. It exists
    /// for the GPU tests and the --screenshot path; the windowed App leaves it false.
    static rhi::Result<std::unique_ptr<Renderer>> create(rhi::Device& device, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);

    /// Recreates the scene targets -- and the motion and history targets alongside them -- at the
    /// new size. The shadow map is fixed-size and untouched.
    /// The caller guarantees the GPU is idle (Device::waitIdle) first: frames still in flight hold
    /// the old textures in their residency set and their encoders, and dropping them here would
    /// free memory the GPU is reading.
    rhi::Result<void> resize(uint32_t width, uint32_t height);

    /// Declares this frame's shadow, scene(+sky), and display passes into `graph`, importing the
    /// renderer's own targets, and answers with the display-target version the display pass
    /// produces -- the handle a caller declares its own pass against, so the editor's UI pass can
    /// read the finished image and the offscreen path can export it.
    ///
    /// Nothing is encoded here. The pass bodies run when the graph executes, and they encode into
    /// `commands` -- so the graph must be executed on that same command list, and `camera`, `view`,
    /// and everything `view` borrows must outlive that call.
    GraphTexture declarePasses(RenderGraph& graph, rhi::CommandList& commands, const Camera& camera,
                               const SceneView& view);

    /// Renders one frame into this renderer's own targets: declarePasses into a graph of nothing
    /// else, compiled and executed on the spot. It is what a caller with no passes of its own wants
    /// -- the offscreen capture path and the tests; a caller that has its own pass declares against
    /// declarePasses instead, so the graph orders the whole frame rather than half of it.
    ///
    /// barrierForSampling covers the one case the graph cannot see: a caller that samples
    /// colorTarget() from a pass it encodes by hand afterwards has declared nothing, so the
    /// transition to a shader read is emitted on its behalf.
    void render(rhi::CommandList& commands, const Camera& camera, const SceneView& view,
                bool barrierForSampling = true);

    /// The finished, display-encoded image: what the viewport shows and what a screenshot reads.
    /// Barriered to ShaderRead when render() returned with barrierForSampling == true.
    rhi::Texture& colorTarget();

    /// Colour domain of colorTarget(), shared by presentation, capture and diagnostics.
    constexpr DisplayDomain displayDomain() const { return kSdrDisplayDomain; }

    /// The scene-linear, pre-exposed image the display transform consumed, in kSceneColorFormat.
    /// Exposed for tests that need to read radiance rather than the picture made of it; the frame
    /// itself never touches it from outside declarePasses.
    ///
    /// Always allocated at the output extent, and rasterised into an origin-anchored rectangle of
    /// TemporalStatus::extents' render extent, which a frame below scale 1 leaves smaller than the
    /// allocation. The texels outside that rectangle are whatever an earlier frame left there, so a
    /// reader at a render scale below 1 has to crop to the active rectangle.
    rhi::Texture& hdrColorTarget();

    /// The last declared frame's depth buffer, D32Float and reversed (near = 1, falling toward 0
    /// with distance). Held past the scene pass and sampled rather than discarded, so a caller can
    /// invert a texel back to a view-space distance as z_view = -nearZ / d -- which is what pins
    /// the projection's convention against real geometry. Barrier it to ShaderRead before sampling:
    /// render() declares no read of it, so the graph has emitted no transition.
    ///
    /// Depth ping-pongs by declared temporal frame parity, so this is the slot the frame just
    /// declared rendered into; a frame with temporal off renders into slot 0. Like the scene
    /// colour, it is allocated at the output extent and written only inside the origin-anchored
    /// rectangle of TemporalStatus::extents' render extent.
    rhi::Texture& depthTarget();

    /// What the last declared frame decided about its history. Advanced by declarePasses(), so it
    /// describes the frame just declared rather than the one about to be.
    TemporalStatus temporalStatus() const { return m_temporalStatus; }

    /// The frame's motion target in kMotionFormat, allocated with the scene targets and so never
    /// null after a successful create(). Borrowed: the renderer owns it and replaces it on
    /// resize().
    rhi::Texture* motionTarget() { return m_motion.get(); }

    /// The colour history slot the frame just declared wrote, in kSceneColorFormat: the resolve's
    /// output under NativeTaa and the raw copy under Raw, so it always holds that frame's output.
    /// Borrowed on motionTarget()'s terms; a frame with temporal off leaves slot 0 untouched and
    /// this reports it anyway.
    rhi::Texture* historyTarget();

    /// The persistent kExposureBufferFloats pair the frame's exposure passes keep. Created with
    /// the renderer and never replaced, so it is never null. Readable from the CPU only when
    /// create() was given cpuReadback -- the tests and the offscreen path; the windowed App never
    /// reads it back, which is the whole point of keeping the feedback GPU-resident.
    rhi::Buffer& exposureBuffer() { return *m_exposureBuffer; }

    /// Returns the current target width in pixels.
    uint32_t width() const { return m_width; }
    /// Returns the current target height in pixels.
    uint32_t height() const { return m_height; }

    /// Public data, not setter pairs: plain per-frame knobs the Inspector edits in place.
    ///
    /// clearColor is authored in display space -- it is what a colour picker hands over. The
    /// renderer decodes it once, when it declares the scene pass, so the hardware clear writes a
    /// scene-linear value into a scene-linear target and the clear reaches the display through the
    /// same transform every shaded pixel does.
    float clearColor[4] = {0.05f, 0.07f, 0.10f, 1.0f};
    /// Uploaded as PassUniforms.time. No shipped shader reads it yet; it is fed from the App's
    /// frame clock so the uniform block holds a real number rather than an unexplained zero.
    float timeSeconds = 0.0f;

private:
    Renderer(rhi::Device& device, bool cpuReadback)
        : m_device(device), m_transientPool(device), m_cpuReadback(cpuReadback) {}

    // Creates the motion and reactive attachments at the current extent, replacing any pair
    // already held. Called from resize() -- and so from create(), which resizes once -- so both
    // exist for every frame whether or not it declares the temporal path.
    //
    // Its own function rather than resize()'s body because the two allocations answer to
    // kMotionFormat and kReactiveFormat rather than to the scene targets'.
    rhi::Result<void> createTemporalTargets();

    rhi::Device& m_device;
    // render()'s own pool for bloom's transients, since a caller without a graph of its own (the
    // --screenshot path, most GPU tests) has no pool to hand declarePasses(); a caller building a
    // graph of its own frame (the App) supplies its own instead and this member goes unused.
    // Non-copyable and non-movable (TransientPool's own contract), so it is constructed in place
    // above rather than assigned.
    TransientPool m_transientPool;
    std::unique_ptr<ShadowStage> m_shadowStage;
    std::unique_ptr<SceneStage> m_sceneStage;
    std::unique_ptr<rhi::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_histogramLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureResolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureSeedLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomDownsampleLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomUpsampleLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_displayPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_histogramPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_exposureResolvePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_exposureSeedPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomThresholdPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomUpsamplePipeline;
    // The scene renders into m_hdrColor and the display transform resolves it into m_color, so
    // the two always share an extent and are replaced together by resize().
    std::unique_ptr<rhi::Texture> m_hdrColor;
    std::unique_ptr<rhi::Texture> m_color;
    std::unique_ptr<rhi::Texture> m_shadowMap;
    // Allocated with the scene targets and replaced by resize() like them, whether or not any
    // frame declares the temporal path: the allocation is permanent and reported through
    // TemporalStatus::historyBytes. Freeing on disable would drop memory the frames still in
    // flight hold in their residency sets, and the caller's idle guarantee covers resize() alone.
    std::unique_ptr<rhi::Texture> m_motion;
    std::unique_ptr<rhi::Texture> m_reactive;
    // The reconstruction stage, which owns both history pairs -- the depth the scene pass renders
    // into included -- and every pass that reads or writes them. Created with the renderer and
    // resized alongside the scene targets, so its slots always share their extent.
    std::unique_ptr<TemporalResolve> m_temporalResolve;
    // The "nothing here" textures every draw binds when a material or a scene leaves a slot empty.
    // They exist because the fragment shader reads every texture slot unconditionally (Slang gives
    // every entry point the file's whole global set), so an empty slot has to hold something that
    // shades to the right answer rather than nothing at all: white is the identity for the
    // material factors, a flat normal leaves the tangent frame alone, and a black cube plus a zero
    // DFG table make both image-based terms vanish.
    std::unique_ptr<rhi::Texture> m_whiteTexture;
    std::unique_ptr<rhi::Texture> m_flatNormalTexture;
    std::unique_ptr<rhi::Texture> m_blackCubeTexture;
    std::unique_ptr<rhi::Texture> m_zeroDfgTexture;
    // 1x1 RGBA16Float zero, bound to keep the display pass's bloom slot valid when bloom is
    // disabled; the zero intensity makes the shader skip reading it.
    std::unique_ptr<rhi::Texture> m_blackBloomFallback;
    // Persistent, imported every frame rather than pooled: a 256-bin uint histogram, cleared and
    // refilled every frame, and a one-float exposure result that survives across frames (spec 9's
    // feedback buffer). Neither is a graph transient because both must outlive the frame that
    // wrote them -- the histogram to be read by the same frame's resolve pass, the exposure result
    // to be read directly, GPU-side, by the *next* frame's scene and sky passes (never a CPU
    // readback -- that would stall the three-frames-in-flight pipeline every auto-exposure frame).
    std::unique_ptr<rhi::Buffer> m_histogramBuffer;
    std::unique_ptr<rhi::Buffer> m_exposureBuffer;
    std::unique_ptr<rhi::Sampler> m_linearSampler;
    std::unique_ptr<rhi::Sampler> m_shadowSampler;
    std::unique_ptr<rhi::Sampler> m_iblSampler;
    // What the previous declared frame was, recorded every frame -- temporal on or off -- because
    // that is what makes re-enabling distinguishable from the first frame ever declared.
    std::optional<FrameSignature> m_previousSignature;
    std::optional<CameraFrameState> m_previousCamera;
    // What the previous declared frame last did with the two targets whose terminal use depends on
    // the path it took. Barriers are derived from the passes in one graph, so a cross-frame edge
    // exists only where the import states it: a temporal frame ends by reading motion (when it
    // draws a debug view) and by copying out of the scene colour, and the next frame's first
    // access has to be ordered behind whichever of those actually happened.
    rhi::TextureUse m_previousMotionUse = rhi::TextureUse::RenderTarget;
    // The reactive attachment is written by every temporal frame and read only by the resolve, so
    // its terminal use is what the frame's mode decided rather than a constant.
    rhi::TextureUse m_previousReactiveUse = rhi::TextureUse::RenderTarget;
    rhi::TextureUse m_previousSceneColorUse = rhi::TextureUse::ShaderRead;
    uint32_t m_temporalFrame = 0; // free-running; the jitter sequence wraps it itself
    // The history slot the frame just declared used, which is what depthTarget() and
    // historyTarget() report: m_temporalFrame % 2 on a temporal frame and 0 otherwise. Recorded
    // rather than recomputed because m_temporalFrame advances past it before declarePasses returns.
    uint32_t m_currentSlot = 0;
    uint64_t m_declaredFrames = 0;
    TemporalStatus m_temporalStatus;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
