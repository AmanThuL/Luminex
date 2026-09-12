//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.h
/// @brief Declares scene rendering data and frame-rendering orchestration.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"
#include "Render/AlphaMode.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"
#include "Render/TemporalResolve.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace lmx::render {

/// Physically based material factors and optional texture bindings.
struct Material {
    /// Null means the renderer's white 1x1 fallback -- so `albedo` alone applies, rather than the
    /// draw silently reading an unbound texture slot.
    rhi::Texture* diffuse = nullptr;
    /// Null means no normal mapping: the shader's flags bit 0 stays clear and its TBN path never
    /// runs. The slot is still bound (to a flat-normal 1x1) so nothing dereferences an empty one.
    rhi::Texture* normalMap = nullptr;
    /// glTF 2.0 channel convention: roughness = G, metallic = B (R and A unused). Null means the
    /// shared white fallback, so `metallic`/`roughness` alone apply.
    rhi::Texture* metallicRoughness = nullptr;
    /// glTF 2.0 channel convention: occlusion = R. Null means the shared white fallback (no
    /// occlusion). Attenuates the image-based terms only, never the analytic lights.
    rhi::Texture* occlusion = nullptr;
    /// Null means the shared white fallback, so `emissive` alone applies.
    rhi::Texture* emissiveMap = nullptr;
    /// The glTF base colour: linear, and the input the shader derives both the diffuse albedo and a
    /// metal's F0 from -- which is why there is no separate reflectance field to keep in step with
    /// it.
    glm::vec4 albedo{1.0f};
    /// Perceptual roughness, squared to the GGX alpha in the shader. The shader floors it at
    /// Lighting.slang's kMinRoughness, so a 0 here is a near-mirror rather than a singular lobe.
    float roughness = 0.5f;
    /// dielectric/metal mix. Deliberately diverges from glTF's material default of 1.0
    /// (GltfMaterial keeps that spec default, and Scene.cpp sets this explicitly for every glTF
    /// material): every non-glTF material here -- MaterialLab's sphere grid, known-color patches,
    /// ramp/probe materials, test materials -- relies on Material{} and never sets metallic, so a
    /// metallic default would render all of them as conductors.
    float metallic = 0.0f;
    /// glTF occlusion strength: 0 ignores the map and 1 applies it fully.
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};    ///< linear radiance the surface emits, added after all lighting
    glm::mat4 uvTransform{1.0f}; ///< Material UV transform applied before texture sampling.
    AlphaMode alphaMode = AlphaMode::Opaque; ///< Opaque or alpha-tested coverage.
    float alphaCutoff = 0.5f; ///< Nonnegative MASK threshold for texture alpha times albedo alpha.
    bool doubleSided = false; ///< MASK surfaces render both faces and reverse back-face normals.
};

/// One object to draw this frame. Non-owning: `mesh` and the material's textures must outlive the
/// render() call that consumes them, which is what lets a caller build the span on the stack every
/// frame from resources it keeps.
struct DrawItem {
    const Mesh* mesh = nullptr; ///< Borrowed mesh drawn by this item.
    glm::mat4 model{1.0f};      ///< Object-to-world transform.
    Material material;          ///< Material copied for this frame.
    /// The object-to-world transform this item was drawn with in the previous declared frame.
    /// Equal to `model` when the item has not moved, so a still object reprojects onto itself.
    glm::mat4 previousModel{1.0f};
    /// How this item's motion is produced; `Invalid` writes the motion sentinel instead of
    /// reprojecting through `previousModel`.
    MotionClass motionClass = MotionClass::Rigid;
};

/// Mirrors Lighting.slang's DirLight. `strength` is linear radiance, `direction` is the way the
/// rays travel (so a light overhead points down).
struct DirectionalLight {
    glm::vec3 strength{0.5f};               ///< Scene-linear RGB radiance.
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; ///< Direction rays travel in world space.
};

/// Runtime-selectable, not a pipeline permutation: it is a uniform the shader branches on, so the
/// editor's combo box costs one integer rather than a second set of pipelines.
enum class ShadowFilter {
    PCF,  ///< Fixed-kernel percentage-closer filtering.
    PCSS, ///< Contact-hardening percentage-closer soft shadows.
};

/// The frame's temporal opt-in, in the terms the caller owns: what is on, what changed, and which
/// diagnostic to draw. Everything the renderer derives from these -- the jitter sample, the history
/// reset reason, whether the reprojection pass may run -- is derived per frame and never set here.
struct TemporalSettings {
    /// Declares the motion attachment, the history commit and the diagnostic passes. False is the
    /// pre-temporal frame, declaration for declaration.
    bool enabled = false;
    /// Offsets rasterisation by the frame's Halton sample. Motion is built from the unjittered
    /// matrices either way, so this changes where the frame is sampled and nothing else.
    bool jitterEnabled = false;
    TemporalDebugView debugView = TemporalDebugView::Off; ///< Diagnostic drawn over the display.
    /// Raised for exactly one frame by the caller when the camera teleported. It is an event, not a
    /// state: no motion heuristic guesses a cut, so a caller that never raises it never gets one.
    bool cameraCut = false;
    /// Monotonic counter the scene bumps on any content change. A different generation resets the
    /// history, because the pixels it holds describe other geometry.
    uint64_t sceneGeneration = 0;
    /// Which reconstruction the frame runs. Both modes declare the same inputs and both leave a
    /// real frame in the colour slot, so switching between them is not a history reset.
    ReconstructionMode reconstruction = ReconstructionMode::Raw;
    /// Fraction of the output extent the scene rasterises at, within [kMinRenderScale,
    /// kMaxRenderScale]. 1 renders at the output extent, and anything below it upscales.
    float renderScale = 1.0f;
};

/// What the last declared frame decided about its history, for the editor to display and a test to
/// assert against.
struct TemporalStatus {
    HistoryResetReason lastReset = HistoryResetReason::None; ///< Reason derived for the last frame.
    uint64_t lastResetFrame = 0; ///< Count of declared frames when a non-None reason last applied.
    /// Position in the Halton sequence the last declared temporal frame stood at. The sequence
    /// advances on every temporal frame whether or not `jitterEnabled` was set -- turning jitter on
    /// resumes the sequence where it stands rather than restarting it -- so a frame with jitter off
    /// reports the index it would have used and rasterised unjittered. A frame with temporal off
    /// advances nothing and leaves this where the last temporal frame left it.
    uint32_t jitterIndex = 0;
    /// Whether the last declared frame could reproject the history it found: temporal on, and no
    /// reset reason. A frame with temporal off derives a reason like any other but reprojects
    /// nothing, so it reports false whatever that reason was.
    bool historyValid = false;
    /// Bytes both colour history slots hold; the allocation is permanent.
    uint64_t historyBytes = 0;
    /// Bytes both depth slots hold, on historyBytes' terms.
    uint64_t depthHistoryBytes = 0;
    /// Which reconstruction the last declared frame ran. A frame with temporal off reports the
    /// mode it was given, which nothing acted on.
    ReconstructionMode reconstruction = ReconstructionMode::Raw;
    /// Declared temporal frames since the last non-None reset reason: 1 on the reset frame itself,
    /// saturating at 65535. A frame with temporal off resets it to 0, because the history it would
    /// have counted is not the one the next temporal frame will find.
    uint32_t historyAge = 0;
    /// Whether historyAge has reached kTemporalWarmupFrames, so the accumulation is converged.
    bool warmupComplete = false;
    FrameExtents extents; ///< Render and output extents the last declared frame ran at.
    /// Render scale the last declared frame resolved, which the render extent's rounding may
    /// differ from by less than a pixel.
    float renderScale = 1.0f;
    /// Whether the last declared frame's render extent differed from its output extent, so the
    /// reconstruction upscaled rather than resolving at one to one.
    bool upscaled = false;
    /// Count of declared frames when the render extent last changed on a temporal frame without a
    /// history reset, 0 until it has. It is what shows that the history survived a scale change,
    /// so a frame with temporal off -- which has no history and rasterises at the output extent
    /// whatever the scale field says -- never advances it.
    uint64_t lastRenderExtentChangeFrame = 0;
    VendorFallback vendorFallback = VendorFallback::None; ///< Why a vendor request fell back.
    std::string_view vendorName; ///< Capability's algorithm name; empty when unavailable.
    bool vendorReset = false;    ///< Whether the last vendor frame discarded its private history.
    uint32_t vendorScalerGeneration = 0; ///< Successful scaler creations across output resizes.
};

/// Non-owning, frame-local view of all scene data consumed by the renderer.
struct SceneView {
    std::span<const DrawItem> items; ///< Borrowed draw list for the current render call.
    /// Light 0 is the only caster: it drives the shadow map, and it is the light the shadow factor
    /// multiplies. Lights 1 and 2 contribute without shadowing.
    DirectionalLight lights[3];
    /// Both null or both set. A sky needs geometry to rasterise and a cubemap to sample; either
    /// one alone would draw nothing or draw black, so the renderer skips the pass unless it has
    /// the pair.
    const Mesh* skySphere = nullptr;
    rhi::Texture* skyCubemap = nullptr; ///< Borrowed sky radiance cubemap.
    /// The scene's image-based lighting, generated from the same environment `skyCubemap` shows
    /// (Source/Engine/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance chain,
    /// and the split-sum DFG table. This is what replaced the flat ambient term -- an environment
    /// the surface actually samples per normal and per reflection vector, rather than one constant
    /// added to every pixel.
    ///
    /// Independently nullable, and null is a supported state rather than an incomplete one: the
    /// renderer substitutes its black-cube and zero-DFG fallbacks, which make both image-based
    /// terms evaluate to zero. A caller that builds a SceneView by hand -- every test that probes
    /// direct lighting on its own -- therefore renders without constructing an IBL set.
    rhi::Texture* irradiance = nullptr;     ///< Borrowed diffuse IBL cubemap.
    rhi::Texture* prefilteredEnv = nullptr; ///< Borrowed specular IBL cubemap chain.
    rhi::Texture* dfgLut = nullptr;         ///< Borrowed split-sum lookup table.
    /// xyz centre, w radius. The shadow ortho frustum is fitted to exactly this, so a sphere that
    /// does not contain the scene loses the geometry outside it from the shadow map.
    glm::vec4 boundingSphere{0.0f, 0.0f, 0.0f, 1.0f};
    ShadowFilter shadowFilter = ShadowFilter::PCF; ///< Runtime shadow sampling mode.
    bool wireframe = false;                        ///< Selects the wireframe scene pipeline.
    /// Manual exposure, in stops. Every fragment multiplies its linear output by exp2(exposureEv)
    /// before the target sees it -- so the scene target holds pre-exposed radiance and the display
    /// transform reads one already-exposed image. Zero is unit exposure, which is what leaves a
    /// scene looking as it did before there was a slider. This is what shading applies whenever
    /// autoExposureEnabled is false, unchanged from before auto-exposure existed; it is also the
    /// value a reset frame seeds the feedback buffer with when auto-exposure is true.
    float exposureEv = 0.0f;

    /// Histogram auto-exposure (spec 9), a Render Settings opt-in; manual exposure (above) stays
    /// the default. When true, the scene and sky passes switch to `ScenePassAuto.slang`/
    /// `SkyAuto.slang`'s compiled pipelines, which read their applied exposure from the persistent
    /// exposure buffer (declared as a graph read only in this mode) instead of `exposureEv` -- a
    /// GPU-persistent value with a one-frame lag, never a CPU readback. The manual pipelines'
    /// shader source is untouched by any of this: the exposure histogram and resolve passes still
    /// declare every frame (so dead-pass culling has something to remove when this is false), but
    /// nothing exports their result.
    bool autoExposureEnabled = false;
    /// True on the frame the feedback loop must (re)seed the exposure buffer with exp2(exposureEv)
    /// (spec 9's four reset triggers: first frame, scene switch, auto-exposure enable, resize).
    /// Only meaningful when autoExposureEnabled is true; EditorShell/main.cpp track the triggers
    /// and forward the result here every frame.
    bool exposureReset = false;
    /// Lower bound of the retained histogram population; 50 trims the darkest half.
    float exposureLowPercentile = 50.0f;
    /// Upper bound of the retained histogram population; 95 trims the brightest 5 percent.
    float exposureHighPercentile = 95.0f;
    /// Scene-referred average luminance is exposed to this pre-exposed output value.
    float exposureTargetGrey = 0.18f;
    float exposureEvMin = -8.0f;         ///< Clamp on the resolved exposure, in stops.
    float exposureEvMax = 8.0f;          ///< Clamp on the resolved exposure, in stops.
    float exposureCompensationEv = 0.0f; ///< Extra stops applied by metering, before the clamp.
    /// How fast auto exposure may brighten, in stops per second. The resolve steps the applied
    /// exposure toward the metered target by at most this much per frame rather than snapping to
    /// it, which is what turns a lighting change into a settle a temporal history can follow. Zero
    /// disables adaptation in this direction and reproduces the instantaneous behaviour bit for
    /// bit. Only meaningful when autoExposureEnabled is true; manual exposure applies exposureEv
    /// directly.
    float exposureAdaptUpStopsPerSecond = 3.0f;
    /// How fast auto exposure may darken, in stops per second, on exposureAdaptUpStopsPerSecond's
    /// terms. Slower than brightening by default, which is the asymmetry the eye expects.
    float exposureAdaptDownStopsPerSecond = 1.5f;

    /// Bloom (spec 10): threshold/prefilter on pre-exposed luminance, a downsample/upsample chain,
    /// composited before the display transform. Enabled by default, identically in the editor and
    /// --screenshot.
    bool bloomEnabled = true;
    float bloomThreshold = 1.0f; ///< Pre-exposed luminance below this contributes nothing.
    float bloomIntensity = 0.2f; ///< Multiplier applied to the composited bloom result.

    /// Temporal state and motion (spec sections 4-6). Default-constructed means off, and a frame
    /// with it off declares, imports and uploads exactly what the renderer declared before there
    /// was a temporal path at all.
    TemporalSettings temporal;
};

/// The light's view-projection and the same matrix with the NDC -> texcoord map baked in, which is
/// what ScenePass.slang hands to CalcShadowFactor.
struct ShadowMatrices {
    glm::mat4 viewProj;        ///< World-to-light clip transform.
    glm::mat4 shadowTransform; ///< World-to-shadow-texture transform.
};

/// For a right-handed camera and Metal's [0,1] clip depth, put the light at -2r along its own
/// direction, look at the sphere's centre, and fit an
/// orthographic frustum to the sphere exactly (extents +/-r, near r, far 3r).
///
/// Depth is reversed, like the camera's: the near plane maps to 1 and the far plane to 0, so the
/// surface nearest the light holds the larger value. Everything downstream is built on that --
/// the shadow pass clears to 0 and keeps what compares Greater, and the comparison sampler is
/// GreaterEqual.
///
/// A free function because it is pure arithmetic on the scene's bounds -- unit-testable without a
/// device, which is where its coverage lives (Tests/RenderTests.cpp).
///
/// `lightDir` is the direction the rays travel and need not be normalised. A direction parallel to
/// world up is handled rather than producing NaNs because the editor can reach it.
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir);

/// Capture tooling, not part of rendering: publishes the four uniform-block layouts this file
/// uploads (name, slot, size, every field's offset and type) to rhi::debug::CaptureSchema, so the
/// capture sidecar can name the bytes a .gputrace holds instead of leaving them a hex dump.
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
constexpr rhi::Format kDisplayFormat = rhi::Format::RGBA16Float; ///< Display-encoded target format.

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
    float experimentalDisplayPeak = 1.0f;   ///< Display-only headroom in reference whites.
    bool experimentalDisplayLinear = false; ///< Extended-linear presentation output.
    bool experimentalCalibration = false;   ///< Display-only synthetic probe patches.

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
    std::unique_ptr<rhi::ShaderLibrary> m_sceneLibrary;
    // ScenePassAuto.slang: byte-for-byte ScenePass.slang except the final multiply reads the
    // persistent exposure buffer instead of PassUniforms.preExposure (spec 9). A separate library
    // and pipeline -- not a runtime branch in one shader -- because a branch that is never taken
    // still gives the compiler a different fragment to schedule around: an M5 parity check failed
    // by one rounding bit in one pixel the one time this shared a file with the manual path. See
    // ScenePassAuto.slang's header for the full reasoning.
    std::unique_ptr<rhi::ShaderLibrary> m_sceneAutoLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_shadowLibrary;
    std::array<std::unique_ptr<rhi::ShaderLibrary>, 2> m_maskSceneLibraries;
    std::unique_ptr<rhi::ShaderLibrary> m_maskShadowLibrary;
    std::array<std::unique_ptr<rhi::GraphicsPipeline>, 16> m_maskScenePipelines;
    std::array<std::unique_ptr<rhi::GraphicsPipeline>, 2> m_maskShadowPipelines;
    std::unique_ptr<rhi::ShaderLibrary> m_skyLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_skyAutoLibrary; // SkyAuto.slang; same reasoning as above
    std::unique_ptr<rhi::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_histogramLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureResolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureSeedLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomDownsampleLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomUpsampleLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineAuto;
    std::unique_ptr<rhi::GraphicsPipeline> m_displayPipeline;
    // The motion twins of the six pipelines above: the same entry points' motion variants, compiled
    // against a second colour attachment in kMotionFormat. A temporal frame binds these instead,
    // which is what leaves the pipelines above -- and so the picture a temporal-off frame produces
    // -- exactly as they were.
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipelineAutoMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipelineAutoMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineMotion;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipelineAutoMotion;
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
