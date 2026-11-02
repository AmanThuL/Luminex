//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.h
/// @brief Declares frame-rendering orchestration and pass contracts.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Geometry/Mesh.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Engine/View/Camera.h"
#include "Render/Common/Formats.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/LocalLights/LightClusterStage.h"
#include "Render/Passes/LocalLights/LightDebugStage.h"
#include "Render/Passes/LocalLights/LightingStatus.h"
#include "Render/Passes/Occlusion/HzbStage.h"

#include "Render/Passes/Scene/DrawSubmission.h"

#include "Render/Passes/Scene/SceneStage.h"

#include "Render/Passes/Shadow/ShadowStage.h"
#include "Render/Passes/Temporal/Temporal.h"
#include "Render/Passes/Temporal/TemporalHistory.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Renderer/DisplayDomain.h"
#include "Render/Renderer/SceneView.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace lmx::render {

/// Owns the renderer's persistent exposure feedback and metering passes.
class ExposureStage;
/// Owns GPU visibility passes and frame-keyed retirement.
class GpuVisibility;
/// Owns independent direct ID visibility validation.
class OcclusionReference;
/// Exposes the renderer's resize candidates to the resize atomicity test.
struct RendererResizeAccess;
/// Owns the renderer's bloom pipelines and transient pass declarations.
class BloomStage;
/// Owns the renderer's display transform and disabled-bloom fallback.
class DisplayStage;

// Derived frame values shared only by the renderer implementation units.
struct RendererFrameState;
// Graph-local imports shared only by the renderer implementation units.
struct RendererFrameImports;
// Staged occlusion facts and graph bindings for one declaration.
struct RendererOcclusionFrame;
// Staged visibility status and draw buffers for one declaration.
struct RendererVisibilityFrame;
// Staged lighting status and light lists for one declaration.
struct RendererLightingFrame;

/// Capture tooling, not part of rendering: publishes the draw stages' uniform-block layouts
/// (including masked variants) (name, size, every field's offset and type) through
/// engine::registerUniformStruct, so the capture sidecar can name the bytes a .gputrace holds
/// instead of leaving them a hex dump.
///
/// Free-standing and idempotent -- re-registering a struct replaces it -- because the layouts
/// describe the *shaders*, not any one Renderer: Renderer::create calls it, and a caller with no
/// Renderer (a test, a tool) may call it directly.
void registerUniformLayoutsForCapture();

/// Floats in the persistent exposure buffer: `{ applied, previous }`. `applied` is what the scene
/// pass multiplies by this frame; `previous` is what it multiplied by on the previous declared
/// temporal frame. The buffer is the single source of both, in manual and auto mode alike, so a
/// temporal resolve correcting a history for the exposure it was recorded at never needs a
/// CPU-remembered value that could disagree with what the GPU actually applied across a mode
/// switch. Shaders/Passes/Exposure/ExposureSeed.slang and
/// Shaders/Passes/Exposure/ExposureResolve.slang write the pair;
/// Shaders/Passes/Exposure/HistogramAccumulate.slang and the auto scene/sky pipelines read index 0.
constexpr uint32_t kExposureBufferFloats = 2;

/// Owns frame targets and pipelines and declares the frame's render-graph passes.
class Renderer {
public:
    /// Releases the frame-owned stages after the caller has completed GPU work.
    ~Renderer();

    /// cpuReadback enables diagnostic results for GPU tests and the screenshot path.
    /// Target storage stays in GPU memory; readback uses explicit staging copies.
    static lmx::Result<std::unique_ptr<Renderer>> create(engine::GpuContext& gpu, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);

    /// Recreates the scene targets -- and the motion and history targets and, once a frame has
    /// enabled occlusion, both depth pyramids alongside them -- at the new size. The shadow map is
    /// fixed-size and untouched. Every replacement is allocated before any is adopted, so a failed
    /// allocation leaves the renderer entirely at its old extent.
    /// The caller guarantees the GPU is idle (Device::waitIdle) first: frames still in flight hold
    /// the old textures in their residency set and their encoders, and dropping them here would
    /// free memory the GPU is reading.
    lmx::Result<void> resize(uint32_t width, uint32_t height);

    /// Declares this frame's shadow, scene(+sky), and display passes into `graph`, importing the
    /// renderer's own targets, and answers with the display-target version the display pass
    /// produces -- the handle a caller declares its own pass against, so the editor's UI pass can
    /// read the finished image and the offscreen path can export it.
    ///
    /// Nothing is encoded here. The pass bodies run when the graph executes, and they encode into
    /// `commands` -- so the graph must be executed on that same command list, and `camera`, `view`,
    /// and everything `view` borrows must outlive that call. Creation or retirement failure returns
    /// its causal error; the caller must not execute or export a failed declaration.
    ///
    /// A failure is not rolled back. Visibility and lighting status, occlusion source facts,
    /// temporal status and the pyramid stage's slots advance only for a frame that declares, and
    /// unrecorded GPU visibility and light-list declarations are discarded; but a failure after
    /// draw submission has prepared leaves its rows, the prefix copies it already recorded on
    /// `commands` and its recorded read uses advanced. Every application caller treats the failure
    /// as terminal.
    lmx::Result<GraphTexture> declarePasses(RenderGraph& graph, rojoRHI::CommandList& commands,
                                            const engine::Camera& camera, const SceneView& view);

    /// Renders one frame into this renderer's own targets: declarePasses into a graph of nothing
    /// else, compiled and executed on the spot. It is what a caller with no passes of its own wants
    /// -- the offscreen capture path and the tests; a caller that has its own pass declares against
    /// declarePasses instead, so the graph orders the whole frame rather than half of it.
    /// Returns declaration errors without exporting or executing the graph. The caller still owns
    /// submission and completion of the acquired device frame on either outcome.
    lmx::Result<void> render(rojoRHI::CommandList& commands, const engine::Camera& camera,
                             const SceneView& view);

    /// The finished, display-encoded image: what the viewport shows and what a screenshot reads.
    const engine::GpuTexture& colorTarget();

    /// Colour domain of colorTarget(), shared by presentation, capture and diagnostics.
    constexpr DisplayDomain displayDomain() const { return kSdrDisplayDomain; }

    /// The scene-linear, pre-exposed image the display transform consumed, in kSceneColorFormat.
    /// Exposed for tests that need to read radiance rather than the picture made of it; the frame
    /// itself never touches it from outside declarePasses.
    ///
    /// Always allocated at the output extent, and rasterised into an origin-anchored rectangle of
    /// TemporalStatus::extents' render extent, which a frame below scale 1 leaves smaller than the
    /// allocation. The texels outside that rectangle are whatever an earlier frame left there, so a
    /// reader at a render scale below 1 has to crop to the active rectangle. The owner is borrowed
    /// until resize replaces it.
    const engine::GpuTexture& hdrColorTexture();

    /// The last declared frame's depth buffer, D32Float and reversed (near = 1, falling toward 0
    /// with distance). After a producing pass, a caller can invert a texel back to a view-space
    /// distance as z_view = -nearZ / d, pinning the projection's convention against real geometry.
    /// Consult textureStates() for each cell's actual layout and frontier before reading it.
    /// Graph-declared depth shader reads use DepthStencilRead; a slot no frame has rendered into
    /// yet may still be Undefined and cannot be read back preserving contents.
    ///
    /// Depth ping-pongs by declared temporal frame parity, so this is the slot the frame just
    /// declared rendered into; a frame with temporal off renders into slot 0. Like the scene
    /// colour, it is allocated at the output extent and written only inside the origin-anchored
    /// rectangle of TemporalStatus::extents' render extent. The owner is borrowed until resize.
    const engine::GpuTexture& depthTexture();

    /// What the last declared frame decided about its history. Advanced by declarePasses(), so it
    /// describes the frame just declared rather than the one about to be.
    TemporalStatus temporalStatus() const { return m_temporalStatus; }

    /// Owned camera/shadow visibility and command statistics for the last successfully declared
    /// frame. A failed declaration leaves the previous frame's status in place.
    const VisibilityStatus& visibilityStatus() const { return m_visibilityStatus; }
    /// Takes exact-frame GPU results retired by the most recent declaration or explicit drain, in
    /// frame order, each already joined with its independent occlusion reference.
    std::vector<VisibilityStatus> takeRetiredVisibility();
    /// Reads all submitted visibility results after the caller has completed Device::waitIdle.
    /// A readback or reference-join error publishes nothing and is sticky: the failing frames
    /// stay pending, so every later retirement -- this drain or the next declarePasses -- returns
    /// the same error. Callers treat it as terminal for the renderer's visibility work.
    lmx::Result<void> drainVisibilityAfterIdle();

    /// Local-light selection and context of the last successfully declared frame; a failed
    /// declaration leaves the previous frame's status in place.
    const LightingStatus& lightingStatus() const { return m_lightingStatus; }
    /// Moves completed lighting contexts to the caller, in frame order; consume regularly as for
    /// visibility. Every record reached it through a joined retirement, so taking cannot fail.
    std::vector<LightingStatus> takeRetiredLighting();
    /// Reads every submitted lighting result after the caller has completed Device::waitIdle.
    /// A light-list readback error publishes nothing for its frame or any later one and is
    /// sticky: those frames stay pending, so every later retirement -- this drain or the next
    /// declarePasses -- returns the same error. Callers treat it as terminal.
    lmx::Result<void> drainLightingAfterIdle();

    /// The frame's motion target in kMotionFormat, allocated with the scene targets and so never
    /// null after a successful create(). Borrowed: the renderer owns it and replaces it on
    /// resize().
    /// Returns null when no motion target exists.
    const engine::GpuTexture* motionTexture() const { return m_motion ? &m_motion : nullptr; }
    /// Borrows the reactive owner on motionTexture() ownership terms.
    const engine::GpuTexture* reactiveTexture() const { return m_reactive ? &m_reactive : nullptr; }

    /// The colour history slot the frame just declared wrote, in kSceneColorFormat: the resolve's
    /// output under NativeTaa and the raw copy under Raw, so it always holds that frame's output.
    /// Borrowed on motionTexture()'s terms; a frame with temporal off leaves slot 0 untouched and
    /// this reports it anyway.
    const engine::GpuTexture* historyTexture();

    /// The use each persistent target was left in by the last frame that touched it. Bound to every
    /// graph declarePasses() is given, so a pass declared into that graph after the renderer's own
    /// reaches it through RenderGraph::textureStates(). create() and resize() seed every target
    /// they allocate.
    TextureStateTable& textureStates() { return m_textureStates; }

    /// Borrows the exposure stage's persistent kExposureBufferFloats GPU pair. Created with the
    /// renderer and retained across resize; diagnostic CPU reads use explicit staging copies.
    const engine::GpuBuffer& exposureBufferOwner();

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
    Renderer(engine::GpuContext& gpu, bool cpuReadback);
    RendererFrameState deriveFrameState(const engine::Camera& camera, const SceneView& view);
    RendererFrameImports importFrameResources(RenderGraph& graph, rojoRHI::CommandList& commands,
                                              const SceneView& view,
                                              const RendererFrameState& state);
    void recordFrameStatus(const RendererFrameState& state);
    lmx::Result<RendererVisibilityFrame>
    prepareVisibility(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                      const FrustumPlanes& planes, std::span<const GraphBuffer> sceneBuffers,
                      const RendererOcclusionFrame& occlusion);
    // Publishes a fully declared frame's visibility status and occlusion facts.
    void commitVisibility(RendererVisibilityFrame&& visibility, RendererOcclusionFrame&& occlusion);

    // Retires the frame three back, creates the cluster owner when first needed and declares this
    // frame's light lists; publishes nothing, so a failure leaves no status or pending record.
    lmx::Result<RendererLightingFrame>
    prepareLighting(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                    std::optional<GraphBuffer> lights, const FrameExtents& extents,
                    const CameraFrameState& cameraState);
    // Discards a prepared frame's unrecorded light-list declaration when the frame fails.
    void abandonLighting(const RendererLightingFrame& lighting);
    // Publishes a fully declared frame's lighting status and its pending retirement record.
    void commitLighting(RendererLightingFrame&& lighting);
    // Joins every pending status through `frame` with its retired light lists, in frame order. A
    // cluster retirement failure keeps that status and every later one pending; records already
    // taken from the cluster owner stay held until their status joins.
    lmx::Result<void> retireLightingThrough(uint64_t frame);

    // Creates every occlusion owner the frame needs and stages its history facts; declares no
    // pass, so a failure leaves no live declaration behind.
    lmx::Result<RendererOcclusionFrame>
    prepareOcclusion(RenderGraph& graph, rojoRHI::CommandList& commands,
                     const engine::Camera& camera, const SceneView& view,
                     const FrameExtents& extents, const CameraFrameState& cameraState);
    // Declares the independent reference, the pyramid build and the debug view. The reference's
    // readback allocation is the only failure, and it precedes the build.
    lmx::Result<void> declareOcclusion(RenderGraph& graph, rojoRHI::CommandList& commands,
                                       const SceneView& view, GraphTexture depth,
                                       GraphTexture& display, RendererOcclusionFrame& occlusion);

    // The motion and reactive attachments at one extent, allocated but not yet adopted.
    struct TemporalTargets {
        engine::GpuTexture motion;
        engine::GpuTexture reactive;
    };
    // Allocates the motion and reactive attachments at `width` x `height`. resize() -- and so
    // create(), which resizes once -- calls it, so both exist for every frame whether or not it
    // declares the temporal path. Its own function rather than resize()'s body because the two
    // allocations answer to kMotionFormat and kReactiveFormat rather than to the scene targets'.
    lmx::Result<TemporalTargets> createTemporalTargets(uint32_t width, uint32_t height) const;
    // Adopts the attachments and both temporal slot pairs that resize() allocated, forgetting the
    // outgoing identities' state entries and seeding the replacements'. It reports no error and
    // allocates no GPU object -- only the state table's own storage, which throws as any
    // allocation does -- which is what lets resize() allocate everything first and commit it all
    // at once.
    void adoptTemporalTargets(TemporalTargets targets, TemporalSlots slots);

    friend struct RendererResizeAccess;
    engine::GpuContext& m_gpu;
    // render()'s own pool for bloom's transients, since a caller without a graph of its own (the
    // --screenshot path, most GPU tests) has no pool to hand declarePasses(); a caller building a
    // graph of its own frame (the App) supplies its own instead and this member goes unused.
    // Non-copyable and non-movable (TransientPool's own contract), so it is constructed in place
    // above rather than assigned.
    TransientPool m_transientPool;
    // What the last frame to touch each persistent target left it as, written back by every graph
    // declarePasses() binds it to.
    TextureStateTable m_textureStates;
    std::unique_ptr<ShadowStage> m_shadowStage;
    std::unique_ptr<SceneStage> m_sceneStage;

    // Stage owners retain callback inputs and persistent feedback through graph execution.
    std::unique_ptr<ExposureStage> m_exposureStage;
    std::unique_ptr<BloomStage> m_bloomStage;
    std::unique_ptr<DisplayStage> m_displayStage;
    // The scene renders into m_hdrColor and the display transform resolves it into m_color, so
    // the two always share an extent and are replaced together by resize().
    engine::GpuTexture m_hdrColor;
    engine::GpuTexture m_color;
    engine::GpuTexture m_shadowMap;

    // Allocated with the scene targets and replaced by resize() like them, whether or not any
    // frame declares the temporal path: the allocation is permanent and reported through
    // TemporalStatus::historyBytes. Freeing on disable would drop memory the frames still in
    // flight hold in their residency sets, and the caller's idle guarantee covers resize() alone.
    engine::GpuTexture m_motion;
    engine::GpuTexture m_reactive;
    // The reconstruction stage, which owns both history pairs -- the depth the scene pass renders
    // into included -- and every pass that reads or writes them. Created with the renderer and
    // resized alongside the scene targets, so its slots always share their extent.
    std::unique_ptr<TemporalResolve> m_temporalResolve;

    // Roots select these owned views whenever a material or environment has no texture. White
    // preserves material factors, the flat normal preserves the tangent frame when sampled, and
    // a black cube plus zero DFG makes both image-based terms vanish. Owners outlive submitted
    // readers, including views selected independently by each draw.
    engine::GpuTexture m_whiteTexture;
    engine::GpuTexture m_flatNormalTexture;
    engine::GpuTexture m_blackCubeTexture;
    engine::GpuTexture m_zeroDfgTexture;
    engine::GpuOwned<rojoRHI::SamplerIndex> m_linearSampler;
    engine::GpuOwned<rojoRHI::SamplerIndex> m_shadowSampler;
    engine::GpuOwned<rojoRHI::SamplerIndex> m_iblSampler;

    // What the previous declared frame was, recorded every frame -- temporal on or off -- because
    // that is what makes re-enabling distinguishable from the first frame ever declared.
    std::optional<FrameSignature> m_previousSignature;
    std::optional<CameraFrameState> m_previousCamera;
    uint32_t m_temporalFrame = 0; // free-running; the jitter sequence wraps it itself
    // The history slot the frame just declared used, which is what depthTexture() and
    // historyTexture() report: m_temporalFrame % 2 on a temporal frame and 0 otherwise. Recorded
    // rather than recomputed because m_temporalFrame advances past it before declarePasses returns.
    uint32_t m_currentSlot = 0;
    uint64_t m_declaredFrames = 0;
    TemporalStatus m_temporalStatus;
    DrawSubmission m_drawSubmission;

    // Retirement joins m_gpuVisibility and m_occlusionReference in one transaction, so the
    // reference exists only beside the production owner it checks.
    std::unique_ptr<GpuVisibility> m_gpuVisibility;
    std::unique_ptr<HzbStage> m_hzbStage;
    std::unique_ptr<OcclusionReference> m_occlusionReference;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_hzbDebugLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_hzbDebugPipeline;
    // Facts of the pyramid the last successfully declared frame built; a failed declaration
    // leaves them, and the two values below, at the previous frame's.
    HzbSource m_occlusionSource;
    bool m_occlusionPreviouslyEnabled = false;
    std::optional<glm::mat4> m_previousOcclusionViewProjection;
    VisibilityStatus m_visibilityStatus;
    std::unique_ptr<LightClusterStage> m_lightClusters;
    std::unique_ptr<LightDebugStage> m_lightDebug;
    LightingStatus m_lightingStatus;
    std::vector<LightingStatus> m_pendingLighting;
    std::vector<LightingStatus> m_retiredLighting;
    // Taken from m_lightClusters but not yet joined with their declared status.
    std::vector<RetiredLightClusters> m_retiredClusters;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
