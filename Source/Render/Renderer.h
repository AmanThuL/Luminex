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
    glm::vec4 albedo{1.0f};
    glm::vec3 fresnelR0{0.04f}; // dielectric default; metals get their own base colour
    float roughness = 0.5f;     // converted to shininess as 1 - roughness
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

    // Declares this frame's shadow and scene(+sky) passes into `graph`, importing the renderer's
    // own targets, and answers with the scene-colour version the scene pass produces -- the handle
    // a caller declares its own pass against, so the editor's UI pass can read what the scene pass
    // rendered and the offscreen path can export it.
    //
    // Nothing is encoded here. The pass bodies run when the graph executes, and they encode into
    // `commands` -- so the graph must be executed on that same command list, and `camera`, `view`,
    // and everything `view` borrows must outlive that call.
    GraphTexture declarePasses(RenderGraph& graph, rhi::CommandList& commands, const Camera& camera,
                               const SceneView& view);

    void render(rhi::CommandList& commands, const Camera& camera, const SceneView& view,
                bool barrierForSampling = true);

    // Barriered to ShaderRead when render() returned with barrierForSampling == true.
    rhi::Texture& colorTarget();

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    // Public data, not setter pairs: plain per-frame knobs the Inspector edits in place.
    //
    // clearColor is written by the hardware clear and therefore lands in the target *unencoded* --
    // it is a display-space colour, like every other value the target ends up holding, just one
    // that skips the shader that would have encoded it.
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
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_sceneWireframePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_skyPipeline;
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
