//----------------------------------------------------------------------------------------------------------------------
/// @file SceneView.h
/// @brief Declares borrowed scene inputs and temporal status for one rendered frame.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/AlphaMode.h"
#include "Render/SceneTables.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"
#include "Render/Visibility.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace lmx::rhi {
class Texture;
}

namespace lmx::render {

/// Frame-local draw order and texture bindings; all borrowed buffers and textures outlive
/// execution.
struct DrawItem {
    uint32_t instanceRow = 0;          ///< Stable instance slot; independent of draw-list position.
    MeshRow mesh;                      ///< Range in the scene geometry pool.
    rhi::Texture* diffuse = nullptr;   ///< Null selects the white fallback.
    rhi::Texture* normalMap = nullptr; ///< Null selects the flat-normal fallback.
    rhi::Texture* metallicRoughness = nullptr; ///< Null selects the white fallback.
    rhi::Texture* occlusion = nullptr;         ///< Null selects the white fallback.
    rhi::Texture* emissiveMap = nullptr;       ///< Null selects the white fallback.
    AlphaMode alphaMode = AlphaMode::Opaque;   ///< Coverage pipeline selection.
    bool doubleSided = false;                  ///< Masked culling pipeline selection.
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
    ClassifyMode classifyMode = ClassifyMode::Cpu; ///< CPU default or fixed-slot GPU classifier.
    bool classifyCheck = false;    ///< Compare retired GPU output against the CPU oracle.
    bool visibilityEnabled = true; ///< Conservatively cull camera candidates.
    SubmissionMode submission = SubmissionMode::Indirect; ///< CPU command preparation mode.
    SceneTables tables;              ///< Borrowed geometry and paced row buffers for this frame.
    std::span<const DrawItem> items; ///< Borrowed draw list for the current render call.
    /// Light 0 is the only caster: it drives the shadow map, and it is the light the shadow factor
    /// multiplies. Lights 1 and 2 contribute without shadowing.
    DirectionalLight lights[3];
    /// Both absent or both set. A sky needs geometry to rasterise and a cubemap to sample; either
    /// one alone would draw nothing or draw black, so the renderer skips the pass unless it has
    /// the pair.
    std::optional<MeshRow> skySphere;
    rhi::Texture* skyCubemap = nullptr; ///< Borrowed sky radiance cubemap.
    /// The scene's image-based lighting, generated from the same environment `skyCubemap` shows
    /// (Source/Asset/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance chain,
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

} // namespace lmx::render
