//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.h
/// @brief Declares scene rendering data and frame-rendering orchestration.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
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
};

/// One object to draw this frame. Non-owning: `mesh` and the material's textures must outlive the
/// render() call that consumes them, which is what lets a caller build the span on the stack every
/// frame from resources it keeps.
struct DrawItem {
    const Mesh* mesh = nullptr; ///< Borrowed mesh drawn by this item.
    glm::mat4 model{1.0f};      ///< Object-to-world transform.
    Material material;          ///< Material copied for this frame.
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
    /// Manual exposure, in stops. The renderer turns it into exp2(exposureEv) and every fragment
    /// multiplies its linear output by that before the target sees it -- so the scene target holds
    /// pre-exposed radiance and the display transform reads one already-exposed image. Zero is
    /// unit exposure, which is what leaves a scene looking as it did before there was a slider.
    /// Ignored when autoExposureEnabled is true, except as the value a reset frame forces.
    float exposureEv = 0.0f;

    /// Histogram auto-exposure (spec 9), a Render Settings opt-in; manual exposure (above) stays
    /// the default. When true, `autoExposureOverride` replaces exp2(exposureEv) as this frame's
    /// applied preExposure. Manual mode never reads the feedback buffer: the exposure histogram
    /// and resolve passes still declare every frame (so dead-pass culling has something to remove
    /// when this is false), but nothing exports their result and nothing feeds it back.
    bool autoExposureEnabled = false;
    /// The linear exposure multiplier applied this frame when autoExposureEnabled is true. The App
    /// is the only writer: a blocking readback of the exposure buffer, taken after the frame whose
    /// resolve pass produced it, so the one-frame lag the spec describes is realised as a CPU/GPU
    /// sync boundary rather than a same-timeline GPU buffer read. On the frame any of spec 9's four
    /// reset triggers fires (first frame, scene switch, auto-exposure enable, resize), the App sets
    /// this to resolveAutoExposureOverride's reset branch (exp2(exposureEv)) instead of the
    /// readback, which is what the reset rule *is* -- there is no separate flag to track here
    /// because the reset and the value it forces are the same decision. Ignored in manual mode.
    float autoExposureOverride = 1.0f;
    float exposureLowPercentile = 50.0f; ///< Percent of pixel count trimmed from the shadow end.
    float exposureHighPercentile =
        95.0f;                           ///< Percent of pixel count trimmed from the highlight end.
    float exposureTargetGrey = 0.18f;    ///< Scene-referred luminance metering maps to unit output.
    float exposureEvMin = -8.0f;         ///< Clamp on the resolved exposure, in stops.
    float exposureEvMax = 8.0f;          ///< Clamp on the resolved exposure, in stops.
    float exposureCompensationEv = 0.0f; ///< Extra stops applied by metering, before the clamp.

    /// Bloom (spec 10): threshold/prefilter on pre-exposed luminance, a downsample/upsample chain,
    /// composited before the display transform. Enabled by default, identically in the editor and
    /// --screenshot.
    bool bloomEnabled = true;
    float bloomThreshold = 1.0f; ///< Pre-exposed luminance below this contributes nothing.
    float bloomIntensity = 0.2f; ///< Multiplier applied to the composited bloom result.
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

/// The value SceneView::autoExposureOverride takes this frame (spec 9's four reset rules -- first
/// frame, scene switch, auto-exposure enable, and resize -- collapse to one decision: `reset` true
/// forces the manual EV exactly as if auto-exposure were off, and false carries the App's cached
/// readback of the exposure buffer forward unchanged). A free function because it is pure
/// arithmetic on the App's own state, unit-testable without a device -- Tests/RenderTests.cpp is
/// where its coverage lives, one case per named trigger. main.cpp calls it once per frame with
/// `reset` from EditorShell::consumeExposureReset().
float resolveAutoExposureOverride(bool reset, float manualExposureEv,
                                  float previousResolvedExposure);

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
constexpr rhi::Format kDisplayFormat = rhi::Format::BGRA8Unorm; ///< Display-encoded target format.

/// Owns frame targets and pipelines and declares the frame's render-graph passes.
class Renderer {
public:
    /// cpuReadback puts the color target in shared storage so Texture::readback() works. It exists
    /// for the GPU tests and the --screenshot path; the windowed App leaves it false.
    static rhi::Result<std::unique_ptr<Renderer>> create(rhi::Device& device, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);

    /// Recreates the scene targets at the new size. The shadow map is fixed-size and untouched.
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
    rhi::Texture& hdrColorTarget();

    /// The frame's depth buffer, D32Float and reversed (near = 1, falling toward 0 with distance).
    /// Held past the scene pass and sampled rather than discarded, so a caller can invert a texel
    /// back to a view-space distance as z_view = -nearZ / d -- which is what pins the projection's
    /// convention against real geometry. Barrier it to ShaderRead before sampling: render()
    /// declares no read of it, so the graph has emitted no transition.
    rhi::Texture& depthTarget();

    /// The persistent exposure buffer the resolve pass writes (spec 9): one float, the linear
    /// exposure multiplier to apply next frame. The App reads it back (after waitIdle, once the
    /// frame whose resolve pass wrote it has retired) to feed SceneView::autoExposureOverride the
    /// following frame -- the one-frame feedback loop's CPU/GPU sync boundary. Valid content only
    /// once a frame with autoExposureEnabled true has actually scheduled the resolve pass.
    rhi::Buffer& exposureBuffer();

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

    rhi::Device& m_device;
    // render()'s own pool for bloom's transients, since a caller without a graph of its own (the
    // --screenshot path, most GPU tests) has no pool to hand declarePasses(); a caller building a
    // graph of its own frame (the App) supplies its own instead and this member goes unused.
    // Non-copyable and non-movable (TransientPool's own contract), so it is constructed in place
    // above rather than assigned.
    TransientPool m_transientPool;
    std::unique_ptr<rhi::ShaderLibrary> m_sceneLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_shadowLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_skyLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_histogramLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureResolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomDownsampleLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomUpsampleLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_displayPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_histogramPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_exposureResolvePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomThresholdPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomUpsamplePipeline;
    // The scene renders into m_hdrColor and the display transform resolves it into m_color, so
    // the two always share an extent and are replaced together by resize().
    std::unique_ptr<rhi::Texture> m_hdrColor;
    std::unique_ptr<rhi::Texture> m_color;
    std::unique_ptr<rhi::Texture> m_depth;
    std::unique_ptr<rhi::Texture> m_shadowMap;
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
    // 1x1 RGBA16Float zero, bound as the display pass's bloom input when bloom is disabled -- the
    // fallback that keeps "composite exactly x + 0" true even if the intensity uniform did not.
    std::unique_ptr<rhi::Texture> m_blackBloomFallback;
    // Persistent, imported every frame rather than pooled: a 256-bin uint histogram, cleared and
    // refilled every frame, and a one-float exposure result that survives across frames (spec 9's
    // feedback buffer). Neither is a graph transient because both must outlive the frame that
    // wrote them -- the histogram to be read by the same frame's resolve pass, the exposure result
    // to be read back by the App for the *next* frame.
    std::unique_ptr<rhi::Buffer> m_histogramBuffer;
    std::unique_ptr<rhi::Buffer> m_exposureBuffer;
    std::unique_ptr<rhi::Sampler> m_linearSampler;
    std::unique_ptr<rhi::Sampler> m_shadowSampler;
    std::unique_ptr<rhi::Sampler> m_iblSampler;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
