//----------------------------------------------------------------------------------------------------------------------
/// @file Validate.h
/// @brief Declares backend-neutral validation helpers for RHI descriptors.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "RHI/RHI.h"

namespace lmx::rhi {

/// Validates a buffer descriptor before backend object creation.
Result<void> validate(const BufferDesc& desc);
/// Validates a texture descriptor before backend object creation.
Result<void> validate(const TextureDesc& desc);
/// Validates a sampler descriptor before backend object creation.
Result<void> validate(const SamplerDesc& desc);
/// Validates a graphics pipeline descriptor before backend object creation.
Result<void> validate(const GraphicsPipelineDesc& desc);
/// Validates a compute pipeline descriptor before backend object creation.
Result<void> validate(const ComputePipelineDesc& desc);
/// Validates a swapchain descriptor before backend object creation.
Result<void> validate(const SwapchainDesc& desc);

/// Validates the attachment combination for a render pass.
Result<void> validateRenderPassTargets(const Texture* color, const Texture* depth);

/// Validates a subresource range against the texture it addresses, resolving the kAllMipLevels and
/// kAllArrayLayers sentinels against that texture's own extents.
Result<void> validateSubresourceRange(const Texture& texture, const TextureSubresourceRange& range);

/// Validates a texture view: its range against the texture, and its format against the texture's
/// format family.
Result<void> validateTextureView(const Texture& texture, const TextureViewDesc& view);

/// Returns true for formats a storage binding can read or write. Storage access is an unfiltered
/// read-write fetch, which the hardware supports for far fewer formats than sampling does.
bool isStorageFormat(Format format);

/// Returns the tightly packed byte size of a readable texel, or zero for unsupported formats.
uint32_t bytesPerPixel(Format format);

} // namespace lmx::rhi
