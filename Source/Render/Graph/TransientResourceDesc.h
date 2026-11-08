//----------------------------------------------------------------------------------------------------------------------
/// @file TransientResourceDesc.h
/// @brief Declares the descriptors of the textures and buffers the render graph creates per frame.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <rojoRHI/RHI.h>

#include <cstdint>

namespace lmx::render {

/// Describes a texture the graph creates, owns, and destroys within one frame.
///
/// It mirrors rojoRHI::TextureDesc minus the two things a transient cannot have: initial contents,
/// and CPU-visible storage. A transient is device-private memory the graph places in a heap, so a
/// caller that needs to read a result back imports a texture of its own instead. The name is passed
/// beside the descriptor, exactly as importTexture takes one, and becomes the GPU object's label.
struct TransientTextureDesc {
    uint32_t width = 0;                                      ///< Extent in texels.
    uint32_t height = 0;                                     ///< Extent in texels.
    rojoRHI::Format format = rojoRHI::Format::Undefined;     ///< Pixel format.
    rojoRHI::TextureType kind = rojoRHI::TextureType::Tex2D; ///< Two-dimensional or cubemap.
    uint32_t mipLevels = 1;    ///< Mip levels allocated for each face.
    bool renderTarget = false; ///< Enables render-target use.
    bool sampled = false;      ///< Enables shader reads.
    bool storageRead = false;  ///< Enables storage-binding reads.
    bool storageWrite = false; ///< Enables storage-binding writes.
    /// Compares every creation field.
    bool operator==(const TransientTextureDesc&) const = default;
};

/// The buffer counterpart of TransientTextureDesc, on the same terms.
struct TransientBufferDesc {
    uint64_t size = 0;         ///< Allocation size in bytes.
    bool storageRead = false;  ///< Enables storage-binding reads.
    bool storageWrite = false; ///< Enables storage-binding writes.
    /// Compares every creation field.
    bool operator==(const TransientBufferDesc&) const = default;
};

} // namespace lmx::render
