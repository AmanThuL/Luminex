//----------------------------------------------------------------------------------------------------------------------
/// @file GpuTexture.cpp
/// @brief Creates and retires texture views while preserving owner creation facts.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuTexture.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/FormatInfo.h"

#include <expected>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<GpuTexture> GpuTexture::create(rojoRHI::Device device, const rojoRHI::TextureDesc& desc,
                                      std::span<const rojoRHI::TextureData> initial,
                                      GpuTextureViews views) {
    auto texture = adoptGpu(device, device.createTexture(desc, initial));
    if (!texture) {
        return std::unexpected(std::move(texture.error()));
    }
    GpuTexture result;
    result.m_texture = std::move(*texture);
    result.m_desc = desc;
    result.m_label = desc.label;
    result.m_desc.label = result.m_label;
    result.m_initialLayout =
        initial.empty() ? rojoRHI::Layout::Undefined : rojoRHI::Layout::ShaderRead;
    const auto aspect =
        isDepthFormat(desc.format) ? rojoRHI::TextureAspect::Depth : rojoRHI::TextureAspect::Color;
    if (views.sampled &&
        (desc.usage & rojoRHI::TextureUsage::Sampled) != rojoRHI::TextureUsage::None) {
        auto sampled = adoptGpu(
            device, device.createView(result.rhi(), {.type = desc.type, .aspect = aspect}));
        if (!sampled) {
            return std::unexpected(std::move(sampled.error()));
        }
        result.m_sampled = std::move(*sampled);
    }
    if (views.storageMips) {
        LMX_ASSERT((desc.usage & rojoRHI::TextureUsage::Storage) != rojoRHI::TextureUsage::None,
                   "GpuTexture::create: storage views require Storage usage");
        result.m_storage.reserve(desc.mips);
        for (uint32_t mip = 0; mip < desc.mips; ++mip) {
            auto storage = adoptGpu(
                device, device.createView(result.rhi(), {.type = desc.type,
                                                         .range = {.baseMip = mip, .mipCount = 1},
                                                         .usage = rojoRHI::TextureUsage::Storage,
                                                         .aspect = aspect}));
            if (!storage) {
                return std::unexpected(std::move(storage.error()));
            }
            result.m_storage.push_back(std::move(*storage));
        }
    }
    registerCaptureResource(result.rhi(), result.m_desc);
    result.m_registered = true;
    return result;
}

//======================================================================================================================
GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : m_texture(std::move(other.m_texture)), m_sampled(std::move(other.m_sampled)),
      m_storage(std::move(other.m_storage)), m_desc(other.m_desc),
      m_label(std::move(other.m_label)),
      m_initialLayout(std::exchange(other.m_initialLayout, rojoRHI::Layout::Undefined)),
      m_registered(std::exchange(other.m_registered, false)) {
    m_desc.label = m_label;
    other.m_desc = {};
    other.m_desc.label = other.m_label;
}

//======================================================================================================================
GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        reset();
        m_texture = std::move(other.m_texture);
        m_sampled = std::move(other.m_sampled);
        m_storage = std::move(other.m_storage);
        m_desc = other.m_desc;
        m_label = std::move(other.m_label);
        m_desc.label = m_label;
        m_initialLayout = std::exchange(other.m_initialLayout, rojoRHI::Layout::Undefined);
        m_registered = std::exchange(other.m_registered, false);
        other.m_desc = {};
        other.m_desc.label = other.m_label;
    }
    return *this;
}

//======================================================================================================================
GpuTexture::~GpuTexture() {
    reset();
}

//======================================================================================================================
void GpuTexture::reset() {
    if (m_registered) {
        unregisterCaptureResource(m_texture.get());
        m_registered = false;
    }
    m_storage.clear();
    m_sampled.reset();
    m_texture.reset();
}

//======================================================================================================================
rojoRHI::Texture GpuTexture::rhi() const {
    LMX_ASSERT(m_texture, "GpuTexture::rhi: the owner is empty");
    return m_texture.get();
}

//======================================================================================================================
rojoRHI::ViewIndex GpuTexture::storageView(uint32_t mip) const {
    LMX_ASSERT(mip < m_storage.size(), "GpuTexture::storageView: no storage view for this mip");
    return m_storage[mip].get();
}

//======================================================================================================================
TextureRef GpuTexture::ref() const {
    return {rhi(),          view(),      {m_desc.extent.width, m_desc.extent.height},
            m_desc.format,  m_desc.mips, m_desc.layers,
            initialLayout()};
}

} // namespace lmx::engine
