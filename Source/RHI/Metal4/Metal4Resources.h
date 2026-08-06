#pragma once
#include "RHI/Metal4/Metal4Common.h"

#include <cstdint>
#include <utility>

namespace lmx::rhi::metal4 {

// Concrete RHI objects for the Metal 4 backend. Each is a thin owning wrapper: it holds the
// metal-cpp object in an NS::SharedPtr (same TransferPtr ownership convention as
// Metal4Device -- see the note on that class) and exposes handle() so sibling backend files
// can reach the native object. handle() is deliberately not on the RHI interfaces, so no
// Metal type escapes into RHI.h.
//
// Construction is always through Metal4Device::create*/loadShaderLibrary, which is where
// desc validation, labeling and residency registration happen; the wrappers themselves only
// own and report.

class Metal4Buffer final : public Buffer {
public:
    explicit Metal4Buffer(NS::SharedPtr<MTL::Buffer> buffer) : m_buffer(std::move(buffer)) {}

    uint64_t size() const override { return m_buffer->length(); }

    MTL::Buffer* handle() const { return m_buffer.get(); }

private:
    NS::SharedPtr<MTL::Buffer> m_buffer;
};

class Metal4Texture final : public Texture {
public:
    Metal4Texture(NS::SharedPtr<MTL::Texture> texture, uint32_t width, uint32_t height,
                  bool cpuReadback)
        : m_texture(std::move(texture)), m_width(width), m_height(height),
          m_cpuReadback(cpuReadback) {}

    uint32_t width() const override { return m_width; }
    uint32_t height() const override { return m_height; }
    void readback(void* out, uint64_t outSize) override;

    MTL::Texture* handle() const { return m_texture.get(); }

private:
    // Dimensions are cached from the desc rather than queried from MTL::Texture on every
    // call: they are immutable for the texture's lifetime and the getters are hot enough
    // (per-readback bounds math) that an objc_msgSend each is pure overhead.
    NS::SharedPtr<MTL::Texture> m_texture;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_cpuReadback = false;
};

class Metal4ShaderLibrary final : public ShaderLibrary {
public:
    explicit Metal4ShaderLibrary(NS::SharedPtr<MTL::Library> library)
        : m_library(std::move(library)) {}

    MTL::Library* handle() const { return m_library.get(); }

private:
    NS::SharedPtr<MTL::Library> m_library;
};

class Metal4Pipeline final : public GraphicsPipeline {
public:
    explicit Metal4Pipeline(NS::SharedPtr<MTL::RenderPipelineState> state)
        : m_state(std::move(state)) {}

    MTL::RenderPipelineState* handle() const { return m_state.get(); }

private:
    // MTL4::Compiler hands back a plain MTL::RenderPipelineState -- Metal 4 reuses the
    // Metal 3 pipeline-state type, only the descriptor and the compiler entry point are new.
    NS::SharedPtr<MTL::RenderPipelineState> m_state;
};

} // namespace lmx::rhi::metal4
