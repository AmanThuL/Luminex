//----------------------------------------------------------------------------------------------------------------------
/// @file GpuTexture.cpp
/// @brief Implements the texture owner over the RojoRHI v1 device.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuTexture.h"

#include "Core/Diagnostics/Assert.h"

#include <expected>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<GpuTexture> GpuTexture::create(rojoRHI::Device& device, const rojoRHI::TextureDesc& desc,
                                      std::span<const rojoRHI::TextureMip> initial) {
    auto texture = adoptGpu(device.createTexture(desc, initial));
    if (!texture) {
        return std::unexpected(std::move(texture.error()));
    }
    return GpuTexture{std::move(*texture), desc};
}

//======================================================================================================================
GpuTexture GpuTexture::adopt(std::unique_ptr<rojoRHI::Texture> texture,
                             const rojoRHI::TextureDesc& desc) {
    return GpuTexture{GpuOwned<rojoRHI::Texture>{std::move(texture)}, desc};
}

//======================================================================================================================
GpuTexture::GpuTexture(GpuOwned<rojoRHI::Texture> texture, const rojoRHI::TextureDesc& desc)
    : m_texture(std::move(texture)), m_desc(desc), m_label(desc.label) {
    m_desc.label = m_label;
}

//======================================================================================================================
GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : m_texture(std::move(other.m_texture)), m_desc(other.m_desc),
      m_label(std::move(other.m_label)) {
    m_desc.label = m_label;
    other.m_desc.label = other.m_label;
}

//======================================================================================================================
GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        m_texture = std::move(other.m_texture);
        m_desc = other.m_desc;
        m_label = std::move(other.m_label);
        m_desc.label = m_label;
        other.m_desc.label = other.m_label;
    }
    return *this;
}

//======================================================================================================================
rojoRHI::Texture& GpuTexture::rhi() const {
    LMX_ASSERT(m_texture, "GpuTexture::rhi: the owner is empty");
    return *m_texture;
}

} // namespace lmx::engine
