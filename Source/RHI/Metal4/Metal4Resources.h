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
// desc validation and labeling happen; the wrappers themselves own, report, and manage
// residency membership (below).

// Ties an allocation's membership of a residency set to the owning wrapper's lifetime:
// added on construction, removed on destruction, with a commit() on each side because the
// set only republishes its allocation list when told to.
//
// It holds the *set* rather than a back-pointer to Metal4Device deliberately: a resource
// outliving its device is then merely wasteful instead of a dangling write, and the class
// needs nothing from the device beyond the set.
//
// A null set means "not residency-managed", and that is a real case, not a defensive one:
// swapchain drawable textures (Task 10) are owned by CAMetalLayer, are handed out and
// reclaimed every frame, and must never be registered. Registration is therefore always an
// explicit argument at the construction site.
class ResidencyRegistration {
public:
    ResidencyRegistration() = default;
    ResidencyRegistration(NS::SharedPtr<MTL::ResidencySet> residency,
                          const MTL::Allocation* allocation);
    ~ResidencyRegistration();

    ResidencyRegistration(const ResidencyRegistration&) = delete;
    ResidencyRegistration& operator=(const ResidencyRegistration&) = delete;

private:
    NS::SharedPtr<MTL::ResidencySet> m_residency;
    const MTL::Allocation* m_allocation = nullptr;
};

class Metal4Buffer final : public Buffer {
public:
    Metal4Buffer(NS::SharedPtr<MTL::Buffer> buffer, NS::SharedPtr<MTL::ResidencySet> residency)
        : m_buffer(std::move(buffer)), m_residency(std::move(residency), m_buffer.get()) {}

    uint64_t size() const override { return m_buffer->length(); }

    MTL::Buffer* handle() const { return m_buffer.get(); }

private:
    // m_residency is declared last so it is destroyed *first* (reverse declaration order),
    // while m_buffer still holds the allocation it has to unregister.
    NS::SharedPtr<MTL::Buffer> m_buffer;
    ResidencyRegistration m_residency;
};

class Metal4Texture final : public Texture {
public:
    Metal4Texture(NS::SharedPtr<MTL::Texture> texture, uint32_t width, uint32_t height,
                  bool cpuReadback, NS::SharedPtr<MTL::ResidencySet> residency)
        : m_texture(std::move(texture)), m_width(width), m_height(height),
          m_cpuReadback(cpuReadback), m_residency(std::move(residency), m_texture.get()) {}

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
    // Declared last -- see the note in Metal4Buffer.
    ResidencyRegistration m_residency;
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
