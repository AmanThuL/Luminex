//----------------------------------------------------------------------------------------------------------------------
/// @file GpuTexture.h
/// @brief Declares the owner of one GPU texture and the descriptor it was created with.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuOwned.h"

#include <rojoRHI/Device.h>
#include <rojoRHI/Texture.h>
#include <rojoRHI/TextureDesc.h>

#include <memory>
#include <span>
#include <string>

namespace lmx::engine {

/// Owns one GPU texture together with a copy of the descriptor it was created with.
///
/// The owner exposes no width, height, format or mip getters by design: v2 textures are handles
/// without getters, so callers read `desc()` instead. The descriptor's `label` views a string the
/// owner holds, so it stays valid after the caller's string dies and across moves. Phase B adds the
/// texture's views here. The owner must not outlive the device that created the texture.
class GpuTexture {
public:
    /// Constructs an empty owner.
    GpuTexture() = default;

    /// Creates a texture on `device` from `desc`, uploading `initial` mip levels when given.
    static Result<GpuTexture> create(rojoRHI::Device& device, const rojoRHI::TextureDesc& desc,
                                     std::span<const rojoRHI::TextureMip> initial = {});
    /// Takes ownership of a texture created elsewhere from `desc`.
    static GpuTexture adopt(std::unique_ptr<rojoRHI::Texture> texture,
                            const rojoRHI::TextureDesc& desc);

    /// Non-copyable: a GPU texture has exactly one owner.
    GpuTexture(const GpuTexture&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    GpuTexture& operator=(const GpuTexture&) = delete;
    /// Moves the texture and its descriptor, re-pointing the label at this owner's string.
    GpuTexture(GpuTexture&& other) noexcept;
    /// Releases the held texture, then moves `other`'s, re-pointing the label at this owner.
    GpuTexture& operator=(GpuTexture&& other) noexcept;
    /// Releases the owned texture.
    ~GpuTexture() = default;

    /// Returns the descriptor the texture was created with; its label views this owner's string.
    const rojoRHI::TextureDesc& desc() const { return m_desc; }
    /// Returns the owned texture; the owner must not be empty.
    rojoRHI::Texture& rhi() const;
    /// Returns whether the owner holds a texture.
    explicit operator bool() const { return static_cast<bool>(m_texture); }

private:
    GpuTexture(GpuOwned<rojoRHI::Texture> texture, const rojoRHI::TextureDesc& desc);

    GpuOwned<rojoRHI::Texture> m_texture;
    rojoRHI::TextureDesc m_desc;
    std::string m_label;
};

} // namespace lmx::engine
