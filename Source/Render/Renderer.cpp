#include "Render/Renderer.h"

#include "Core/Assert.h"

#include <string>
#include <utility>

namespace lmx::render {

namespace {

// C++ mirror of Shaders/Mesh.slang's ObjectUniforms. Measured against the emitted MSL (Task 8,
// Amendment A4): Slang lowers each float4x4 to `struct { array<float4, 4> }` in *column-major*
// order and adds no padding, so a glm::mat4 -- itself an array of 4 columns -- uploads verbatim,
// and glm::vec4 lands on the trailing float4.
struct ObjectUniforms {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec4 baseColor;
};
static_assert(sizeof(ObjectUniforms) == 144, "must match the shader's cbuffer layout");

// MTL4ArgumentTable buffer slots, straight off the emitted MSL: gVertices [[buffer(0)]],
// gObject [[buffer(1)]].
constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;

} // namespace

rhi::Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device, uint32_t width,
                                                        uint32_t height, bool cpuReadback) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::create: width and height must be non-zero");

    // Not make_unique: the constructor is private so that a Renderer can only exist fully built.
    std::unique_ptr<Renderer> self(new Renderer(device, cpuReadback));

    auto library = device.loadShaderLibrary("Shaders/Mesh");
    if (!library) {
        return std::unexpected(library.error());
    }
    self->m_library = std::move(*library);

    auto pipeline = device.createGraphicsPipeline({.library = self->m_library.get(),
                                                   .vertexEntry = "vertexMain",
                                                   .fragmentEntry = "fragmentMain",
                                                   .colorFormat = rhi::Format::BGRA8Unorm,
                                                   .depthFormat = rhi::Format::D32Float,
                                                   .depthTestEnable = true,
                                                   .depthWriteEnable = true,
                                                   .label = "lmx.render.meshPipeline"});
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    self->m_pipeline = std::move(*pipeline);

    if (auto targets = self->resize(width, height); !targets) {
        return std::unexpected(targets.error());
    }
    return self;
}

rhi::Result<void> Renderer::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::resize: width and height must be non-zero");

    // sampled unconditionally: the whole point of the offscreen target is that something reads it
    // afterwards (the ImGui viewport image, the barrier test's copy pass).
    auto color = m_device.createTexture({.width = width,
                                         .height = height,
                                         .format = rhi::Format::BGRA8Unorm,
                                         .renderTarget = true,
                                         .sampled = true,
                                         .cpuReadback = m_cpuReadback,
                                         .label = "lmx.render.sceneColor"});
    if (!color) {
        return std::unexpected(color.error());
    }
    // renderTarget only -- nothing samples or reads back scene depth, and asking for more would
    // cost the driver's compressed depth layout for no consumer.
    auto depth = m_device.createTexture({.width = width,
                                         .height = height,
                                         .format = rhi::Format::D32Float,
                                         .renderTarget = true,
                                         .label = "lmx.render.sceneDepth"});
    if (!depth) {
        return std::unexpected(depth.error());
    }

    // Assigned only once both succeeded: a failed resize leaves the previous, still-valid pair in
    // place rather than a half-swapped one.
    m_color = std::move(*color);
    m_depth = std::move(*depth);
    m_width = width;
    m_height = height;
    return {};
}

void Renderer::render(rhi::CommandList& commands, const Camera& camera,
                      std::span<const DrawItem> items, bool barrierForSampling) {
    LMX_ASSERT(m_color && m_depth, "Renderer::render: targets are missing -- create() failed");

    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();

    commands.beginRenderPass(
        {.colorTarget = m_color.get(),
         .clearColor = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]},
         .clear = true,
         .depthTarget = m_depth.get(),
         .clearDepth = 1.0f});
    commands.bindPipeline(*m_pipeline);
    for (const DrawItem& item : items) {
        LMX_ASSERT(item.mesh != nullptr, "DrawItem.mesh must not be null");
        const ObjectUniforms uniforms{
            .mvp = viewProj * item.model, .model = item.model, .baseColor = item.baseColor};
        commands.bindVertexBuffer(kVertexBufferSlot, *item.mesh->vertexBuffer);
        // Per draw, into the frame's transient ring: setUniforms copies at call time, so each
        // draw keeps its own bytes even though every draw rebinds the same argument-table slot.
        commands.setUniforms(kObjectUniformsSlot, &uniforms, sizeof(uniforms));
        commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
    }
    commands.endRenderPass();

    if (barrierForSampling) {
        // Leaves the color target readable by a later pass this frame (UI viewport, blit test).
        commands.textureBarrier(*m_color, rhi::TextureUse::RenderTarget,
                                rhi::TextureUse::ShaderRead);
    }
}

rhi::Texture& Renderer::colorTarget() {
    LMX_ASSERT(m_color != nullptr, "Renderer::colorTarget: no color target -- create() failed");
    return *m_color;
}

} // namespace lmx::render
