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

struct Material {
    // Null means the renderer's white 1x1 fallback -- so `albedo` alone applies, rather than the
    // draw silently reading an unbound texture slot.
    rhi::Texture* diffuse = nullptr;
    // Null means no normal mapping: the shader's flags bit 0 stays clear and its TBN path never
    // runs. The slot is still bound (to a flat-normal 1x1) so nothing dereferences an empty one.
    rhi::Texture* normalMap = nullptr;
    // glTF 2.0 channel convention: roughness = G, metallic = B (R and A unused). Null means the
    // shared white fallback, so `metallic`/`roughness` alone apply. Bound but unread by the
    // fragment until the GGX rewrite consumes it.
    rhi::Texture* metallicRoughness = nullptr;
    // glTF 2.0 channel convention: occlusion = R. Null means the shared white fallback (no
    // occlusion). Bound but unread by the fragment until the GGX rewrite consumes it.
    rhi::Texture* occlusion = nullptr;
    // Null means the shared white fallback, so `emissive` alone applies. Bound but unread by the
    // fragment until the GGX rewrite consumes it.
    rhi::Texture* emissiveMap = nullptr;
    glm::vec4 albedo{1.0f};
    glm::vec3 fresnelR0{0.04f}; // dielectric default; metals get their own base colour
    float roughness = 0.5f;     // converted to shininess as 1 - roughness
    float metallic = 1.0f;    // dielectric/metal mix; unread by the fragment until the GGX rewrite
    glm::vec3 emissive{0.0f}; // linear; unread by the fragment until the GGX rewrite
    glm::mat4 uvTransform{1.0f};
};

// One object to draw this frame. Non-owning: `mesh` and the material's textures must outlive the
// render() call that consumes them, which is what lets a caller build the span on the stack every
// frame from resources it keeps.
struct DrawItem {
    const Mesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    Material material;
};

// Mirrors Lighting.slang's DirLight. `strength` is linear radiance, `direction` is the way the
// rays travel (so a light overhead points down).
struct DirectionalLight {
    glm::vec3 strength{0.5f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
};

// Runtime-selectable, not a pipeline permutation: it is a uniform the shader branches on, so the
// editor's combo box costs one integer rather than a second set of pipelines.
enum class ShadowFilter { PCF, PCSS };

struct SceneView {
    std::span<const DrawItem> items;
    // Light 0 is the only caster: it drives the shadow map, and it is the light the shadow factor
    // multiplies. Lights 1 and 2 contribute without shadowing.
    DirectionalLight lights[3];
    // Linear, per this header's own doctrine (see Material above). Pre-decoded as a literal --
    // rather than a call to engine::srgbToLinear -- because Render sits below Engine in the
    // dependency chain and cannot call it; this is engine::attachSkyAndLights's authored
    // (0.25, 0.25, 0.35), decoded (Source/Engine/Scene.cpp) and rounded to 6 significant digits --
    // that decode is the only place the sRGB constant itself is written down.
    glm::vec3 ambient{0.050876f, 0.050876f, 0.100482f};
    // Both null or both set. A sky needs geometry to rasterise and a cubemap to sample; either
    // one alone would draw nothing or draw black, so the renderer skips the pass unless it has
    // the pair.
    const Mesh* skySphere = nullptr;
    rhi::Texture* skyCubemap = nullptr;
    // xyz centre, w radius. The shadow ortho frustum is fitted to exactly this, so a sphere that
    // does not contain the scene loses the geometry outside it from the shadow map.
    glm::vec4 boundingSphere{0.0f, 0.0f, 0.0f, 1.0f};
    ShadowFilter shadowFilter = ShadowFilter::PCF;
    bool wireframe = false;
    // Manual exposure, in stops. The renderer turns it into exp2(exposureEv) and every fragment
    // multiplies its linear output by that before the target sees it -- so the scene target holds
    // pre-exposed radiance and the display transform reads one already-exposed image. Zero is
    // unit exposure, which is what leaves a scene looking as it did before there was a slider.
    float exposureEv = 0.0f;
};

// The light's view-projection and the same matrix with the NDC -> texcoord map baked in, which is
// what ScenePass.slang hands to CalcShadowFactor.
struct ShadowMatrices {
    glm::mat4 viewProj;
    glm::mat4 shadowTransform;
};

// For a right-handed camera and Metal's [0,1] clip depth, put the light at -2r along its own
// direction, look at the sphere's centre, and fit an
// orthographic frustum to the sphere exactly (extents +/-r, near r, far 3r).
//
// Depth is reversed, like the camera's: the near plane maps to 1 and the far plane to 0, so the
// surface nearest the light holds the larger value. Everything downstream is built on that --
// the shadow pass clears to 0 and keeps what compares Greater, and the comparison sampler is
// GreaterEqual.
//
// A free function because it is pure arithmetic on the scene's bounds -- unit-testable without a
// device, which is where its coverage lives (Tests/RenderTests.cpp).
//
// `lightDir` is the direction the rays travel and need not be normalised. A direction parallel to
// world up is handled rather than producing NaNs because the editor can reach it.
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir);

// Capture tooling, not part of rendering: publishes the four uniform-block layouts this file
// uploads (name, slot, size, every field's offset and type) to rhi::debug::CaptureSchema, so the
// capture sidecar can name the bytes a .gputrace holds instead of leaving them a hex dump.
//
// Free-standing and idempotent -- re-registering a struct replaces it -- because the layouts
// describe the *shaders*, not any one Renderer: Renderer::create calls it, and a caller with no
// Renderer (a test, a tool) may call it directly.
void registerUniformLayoutsForCapture();

// The two colour formats the frame runs through, named because three places have to agree on
// each: the texture the renderer creates, the pipeline compiled to render into it, and the format
// declared when the graph imports it. A literal in any one of those is a trap -- the graph checks
// attachment roles against what it was told, not against the texture.
//
// The scene renders in half float because that is what holds radiance above 1.0; the display
// target is the 8-bit surface the viewport, the swapchain, and the screenshot all expect.
constexpr rhi::Format kSceneColorFormat = rhi::Format::RGBA16Float;
constexpr rhi::Format kDisplayFormat = rhi::Format::BGRA8Unorm;

class Renderer {
public:
    // cpuReadback puts the color target in shared storage so Texture::readback() works. It exists
    // for the GPU tests and the --screenshot path; the windowed App leaves it false.
    static rhi::Result<std::unique_ptr<Renderer>> create(rhi::Device& device, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);

    // Recreates the scene targets at the new size. The shadow map is fixed-size and untouched.
    // The caller guarantees the GPU is idle (Device::waitIdle) first: frames still in flight hold
    // the old textures in their residency set and their encoders, and dropping them here would
    // free memory the GPU is reading.
    rhi::Result<void> resize(uint32_t width, uint32_t height);

    // Declares this frame's shadow, scene(+sky), and display passes into `graph`, importing the
    // renderer's own targets, and answers with the display-target version the display pass
    // produces -- the handle a caller declares its own pass against, so the editor's UI pass can
    // read the finished image and the offscreen path can export it.
    //
    // Nothing is encoded here. The pass bodies run when the graph executes, and they encode into
    // `commands` -- so the graph must be executed on that same command list, and `camera`, `view`,
    // and everything `view` borrows must outlive that call.
    GraphTexture declarePasses(RenderGraph& graph, rhi::CommandList& commands, const Camera& camera,
                               const SceneView& view);

    // Renders one frame into this renderer's own targets: declarePasses into a graph of nothing
    // else, compiled and executed on the spot. It is what a caller with no passes of its own wants
    // -- the offscreen capture path and the tests; a caller that has its own pass declares against
    // declarePasses instead, so the graph orders the whole frame rather than half of it.
    //
    // barrierForSampling covers the one case the graph cannot see: a caller that samples
    // colorTarget() from a pass it encodes by hand afterwards has declared nothing, so the
    // transition to a shader read is emitted on its behalf.
    void render(rhi::CommandList& commands, const Camera& camera, const SceneView& view,
                bool barrierForSampling = true);

    // The finished, display-encoded image: what the viewport shows and what a screenshot reads.
    // Barriered to ShaderRead when render() returned with barrierForSampling == true.
    rhi::Texture& colorTarget();

    // The scene-linear, pre-exposed image the display transform consumed, in kSceneColorFormat.
    // Exposed for tests that need to read radiance rather than the picture made of it; the frame
    // itself never touches it from outside declarePasses.
    rhi::Texture& hdrColorTarget();

    // The frame's depth buffer, D32Float and reversed (near = 1, falling toward 0 with distance).
    // Held past the scene pass and sampled rather than discarded, so a caller can invert a texel
    // back to a view-space distance as z_view = -nearZ / d -- which is what pins the projection's
    // convention against real geometry. Barrier it to ShaderRead before sampling: render()
    // declares no read of it, so the graph has emitted no transition.
    rhi::Texture& depthTarget();

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    // Public data, not setter pairs: plain per-frame knobs the Inspector edits in place.
    //
    // clearColor is authored in display space -- it is what a colour picker hands over. The
    // renderer decodes it once, when it declares the scene pass, so the hardware clear writes a
    // scene-linear value into a scene-linear target and the clear reaches the display through the
    // same transform every shaded pixel does.
    float clearColor[4] = {0.05f, 0.07f, 0.10f, 1.0f};
    // Uploaded as PassUniforms.time. No shipped shader reads it yet; it is fed from the App's
    // frame clock so the uniform block holds a real number rather than an unexplained zero.
    float timeSeconds = 0.0f;

private:
    Renderer(rhi::Device& device, bool cpuReadback)
        : m_device(device), m_cpuReadback(cpuReadback) {}

    rhi::Device& m_device;
    std::unique_ptr<rhi::ShaderLibrary> m_sceneLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_shadowLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_skyLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_displayPipeline;
    // The scene renders into m_hdrColor and the display transform resolves it into m_color, so
    // the two always share an extent and are replaced together by resize().
    std::unique_ptr<rhi::Texture> m_hdrColor;
    std::unique_ptr<rhi::Texture> m_color;
    std::unique_ptr<rhi::Texture> m_depth;
    std::unique_ptr<rhi::Texture> m_shadowMap;
    // The three "nothing here" textures every draw binds when a material or a scene leaves a slot
    // empty. They exist because the fragment shader reads all four texture slots unconditionally
    // (Slang gives every entry point the file's whole global set), so an empty slot has to hold
    // something that shades to the right answer rather than nothing at all.
    std::unique_ptr<rhi::Texture> m_whiteTexture;
    std::unique_ptr<rhi::Texture> m_flatNormalTexture;
    std::unique_ptr<rhi::Texture> m_blackCubeTexture;
    std::unique_ptr<rhi::Sampler> m_linearSampler;
    std::unique_ptr<rhi::Sampler> m_shadowSampler;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
