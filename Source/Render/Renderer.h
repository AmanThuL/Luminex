#pragma once
#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace lmx::render {

// One object to draw this frame. Non-owning: `mesh` must outlive the render() call that consumes
// it, which is what lets a caller build the span on the stack every frame from meshes it keeps.
struct DrawItem {
    const Mesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    glm::vec4 baseColor{1.0f};
};

// Renders a list of DrawItems into its own offscreen color+depth pair with the Mesh.slang lambert
// pipeline. Owns the targets rather than rendering to a caller-supplied one because the whole point
// of the offscreen path is that the scene's size is the *viewport's* size, not the window's -- the
// caller resizes it and then displays colorTarget() however it likes (M2: an ImGui image).
class Renderer {
public:
    // cpuReadback puts the color target in shared storage so Texture::readback() works. It exists
    // for the GPU tests and the --screenshot path; the windowed App leaves it false.
    static rhi::Result<std::unique_ptr<Renderer>> create(rhi::Device& device, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);

    // Recreates both targets at the new size. The caller guarantees the GPU is idle
    // (Device::waitIdle) first: frames still in flight hold the old textures in their residency
    // set and their encoders, and dropping them here would free memory the GPU is reading.
    rhi::Result<void> resize(uint32_t width, uint32_t height);

    // Records one scene pass onto `commands`, which must be inside an open frame and outside any
    // render pass.
    //
    // barrierForSampling: true records the RenderTarget -> ShaderRead barrier after the pass, so a
    // *later pass in this same frame* may sample colorTarget(). Pass false when no later pass does
    // -- an offscreen-only frame -- because the RHI treats a barrier no pass consumed as a dropped
    // dependency edge and aborts at endFrame. See the plan's Amendment A4.
    void render(rhi::CommandList& commands, const Camera& camera, std::span<const DrawItem> items,
                bool barrierForSampling = true);

    // Barriered to ShaderRead when render() returned with barrierForSampling == true.
    rhi::Texture& colorTarget();

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    // Public data, not a setter pair: it is a plain per-frame knob the Inspector edits in place.
    float clearColor[4] = {0.05f, 0.07f, 0.10f, 1.0f};

private:
    Renderer(rhi::Device& device, bool cpuReadback)
        : m_device(device), m_cpuReadback(cpuReadback) {}

    rhi::Device& m_device;
    std::unique_ptr<rhi::ShaderLibrary> m_library;
    std::unique_ptr<rhi::GraphicsPipeline> m_pipeline;
    std::unique_ptr<rhi::Texture> m_color;
    std::unique_ptr<rhi::Texture> m_depth;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

} // namespace lmx::render
