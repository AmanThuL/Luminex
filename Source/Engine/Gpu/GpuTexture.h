//----------------------------------------------------------------------------------------------------------------------
/// @file GpuTexture.h
/// @brief Declares texture ownership, shader views and immutable creation metadata.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuOwned.h"

#include <rojoRHI/RHI.h>

#include <span>
#include <string>
#include <vector>

namespace lmx::engine {

/// Sentinel for an absent shader view; index zero is a valid view.
inline constexpr rojoRHI::ViewIndex kNoView{~0u};

/// Borrows a texture and its default sampled view; the owner and device must outlive its use.
/// Dimensions and initialLayout describe creation of the underlying resource, never current state.
struct TextureRef {
    rojoRHI::Texture texture;                            ///< Underlying texture identity.
    rojoRHI::ViewIndex view = kNoView;                   ///< Sampled view, or kNoView.
    rojoRHI::Extent2D extent;                            ///< Mip-zero extent in texels.
    rojoRHI::Format format = rojoRHI::Format::Undefined; ///< Storage format.
    uint32_t mips = 1;                                   ///< Underlying resource mip count.
    uint32_t layers = 1; ///< Underlying resource layer count, including cube faces.
    rojoRHI::Layout initialLayout = rojoRHI::Layout::Undefined; ///< Immutable creation layout.
};

/// Selects the views the texture owner creates and retires with its texture.
struct GpuTextureViews {
    bool sampled = true;      ///< Creates a sampled view when the texture has Sampled usage.
    bool storageMips = false; ///< Creates one storage view per mip; requires Storage usage.
};

/// Owns a texture, its shader views and a descriptor whose label remains valid across moves.
/// Views are destroyed before the texture. The creating context must outlive the owner.
class GpuTexture {
public:
    /// Constructs an empty owner.
    GpuTexture() = default;
    /// Creates a texture and the requested views; provided initial data establishes ShaderRead.
    /// Returns the original creation error if any resource or view cannot be created.
    static Result<GpuTexture> create(rojoRHI::Device device, const rojoRHI::TextureDesc& desc,
                                     std::span<const rojoRHI::TextureData> initial = {},
                                     GpuTextureViews views = {});
    /// A texture has exactly one owner.
    GpuTexture(const GpuTexture&) = delete;
    /// A texture cannot acquire a second owner by assignment.
    GpuTexture& operator=(const GpuTexture&) = delete;
    /// Transfers the texture, views and creation facts, preserving the label's lifetime.
    GpuTexture(GpuTexture&& other) noexcept;
    /// Releases this texture and views before transferring other's ownership.
    GpuTexture& operator=(GpuTexture&& other) noexcept;
    /// Unregisters the resource and defers release of views before its texture.
    ~GpuTexture();

    /// Returns the creation descriptor; label borrows this owner's string.
    const rojoRHI::TextureDesc& desc() const { return m_desc; }
    /// Returns the texture handle; the owner must not be empty.
    rojoRHI::Texture rhi() const;
    /// Returns the sampled view or kNoView when none was requested or supported by usage.
    rojoRHI::ViewIndex view() const { return m_sampled.get(); }
    /// Returns the actual number of per-mip storage views, zero when none were requested.
    uint32_t storageViewCount() const { return static_cast<uint32_t>(m_storage.size()); }
    /// Returns a created storage view; mip must be below storageViewCount().
    rojoRHI::ViewIndex storageView(uint32_t mip) const;
    /// Returns ShaderRead when created with initial data, otherwise Undefined; never current state.
    rojoRHI::Layout initialLayout() const { return m_initialLayout; }
    /// Borrows the texture, sampled view and underlying-resource creation metadata.
    TextureRef ref() const;
    /// Returns whether this owner holds a texture.
    explicit operator bool() const { return static_cast<bool>(m_texture); }

private:
    void reset();

    GpuOwned<rojoRHI::Texture> m_texture;
    GpuOwned<rojoRHI::ViewIndex> m_sampled;
    std::vector<GpuOwned<rojoRHI::ViewIndex>> m_storage;
    rojoRHI::TextureDesc m_desc;
    std::string m_label;
    rojoRHI::Layout m_initialLayout = rojoRHI::Layout::Undefined;
    bool m_registered = false;
};

} // namespace lmx::engine
