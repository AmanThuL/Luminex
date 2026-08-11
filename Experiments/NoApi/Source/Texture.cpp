//----------------------------------------------------------------------------------------------------------------------
/// @file Texture.cpp
/// @brief Implements texture sizing and creation at a caller-chosen address inside a heap.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <algorithm>
#include <format>

namespace lmx::noapi {
namespace {

constexpr uint32_t kCubeFaceCount = 6;

//======================================================================================================================
MTL::TextureType toMTL(TextureKind kind) {
    switch (kind) {
    case TextureKind::Texture2DArray:
        return MTL::TextureType2DArray;
    case TextureKind::TextureCube:
        return MTL::TextureTypeCube;
    case TextureKind::Texture3D:
        return MTL::TextureType3D;
    case TextureKind::Texture2D:
        break;
    }
    return MTL::TextureType2D;
}

//======================================================================================================================
MTL::TextureUsage toMTL(TextureUsage usage) {
    MTL::TextureUsage result = MTL::TextureUsageUnknown;
    if (hasUsage(usage, TextureUsage::Sampled)) {
        result |= MTL::TextureUsageShaderRead;
    }
    if (hasUsage(usage, TextureUsage::Storage)) {
        result |= MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite;
    }
    if (hasUsage(usage, TextureUsage::ColorAttachment) ||
        hasUsage(usage, TextureUsage::DepthStencilAttachment)) {
        result |= MTL::TextureUsageRenderTarget;
    }
    // Metal requires the parent texture to opt into subresource and format views at creation, and
    // every bindless slot the prototype writes may name one.
    if (hasUsage(usage, TextureUsage::Sampled) || hasUsage(usage, TextureUsage::Storage)) {
        result |= MTL::TextureUsagePixelFormatView;
    }
    return result;
}

//======================================================================================================================
NS::SharedPtr<MTL::TextureDescriptor> makeDescriptor(const TextureDesc& desc) {
    auto textureDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    textureDesc->setTextureType(toMTL(desc.kind));
    textureDesc->setPixelFormat(toMTL(desc.format));
    textureDesc->setWidth(desc.extent.width);
    textureDesc->setHeight(desc.extent.height);
    textureDesc->setDepth(desc.kind == TextureKind::Texture3D ? desc.extent.depth : 1);
    textureDesc->setMipmapLevelCount(desc.mipCount);
    // A cube's array length counts cubes rather than faces, so six faces stay one array element.
    textureDesc->setArrayLength(
        desc.kind == TextureKind::TextureCube
            ? 1
            : (desc.kind == TextureKind::Texture2DArray ? desc.arrayLayers : 1));
    textureDesc->setSampleCount(desc.sampleCount);
    textureDesc->setUsage(toMTL(desc.usage));
    // Every prototype texture is placed into a private placement heap, and a placed resource must
    // match the heap's storage and hazard-tracking modes.
    textureDesc->setStorageMode(MTL::StorageModePrivate);
    textureDesc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
    return textureDesc;
}

//======================================================================================================================
void validateDesc(const TextureDesc& desc) {
    LMX_ASSERT(!desc.label.empty(), "TextureDesc.label must not be empty");
    LMX_ASSERT(desc.format != Format::Undefined, "TextureDesc.format must not be Undefined");
    LMX_ASSERT(desc.usage != TextureUsage::None, "TextureDesc.usage must declare at least one use");
    LMX_ASSERT(desc.mipCount >= 1, "TextureDesc.mipCount must be at least one");
    LMX_ASSERT(desc.arrayLayers >= 1, "TextureDesc.arrayLayers must be at least one");
    LMX_ASSERT(desc.sampleCount >= 1, "TextureDesc.sampleCount must be at least one");
    LMX_ASSERT(desc.extent.width >= 1 && desc.extent.height >= 1 && desc.extent.depth >= 1,
               "TextureDesc.extent must be non-zero in every dimension");
    LMX_ASSERT(desc.kind != TextureKind::TextureCube || desc.arrayLayers == kCubeFaceCount,
               "TextureDesc: a cube map must declare six array layers");
}

} // namespace

//======================================================================================================================
SizeAlign textureSizeAlign(const Device* device, const TextureDesc& desc) {
    LMX_ASSERT(device != nullptr, "textureSizeAlign: device must not be null");
    validateDesc(desc);
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Sizing runs against exactly the descriptor creation will use, because a swizzled layout's
    // footprint depends on every field of it.
    const MTL::SizeAndAlign sizeAlign =
        device->mtl->heapTextureSizeAndAlign(makeDescriptor(desc).get());
    return {.size = sizeAlign.size, .alignment = sizeAlign.align};
}

//======================================================================================================================
Result<Texture*> createTexture(Device* device, const TextureDesc& desc, GpuAddress placement) {
    LMX_ASSERT(device != nullptr, "createTexture: device must not be null");
    validateDesc(desc);
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    if (toMTL(desc.format) == MTL::PixelFormatInvalid) {
        return fail(ErrorCode::InvalidDesc,
                    "createTexture: texture '" + std::string(desc.label) +
                        "' names a format this prototype does not map to Metal");
    }

    AllocationRecord* record = findAllocation(device, placement);
    LMX_ASSERT(record != nullptr,
               std::format("createTexture: placement address {:#x} for '{}' lies outside every "
                           "live allocation",
                           placement, desc.label));
    LMX_ASSERT(record->kind == MemoryKind::Private,
               std::format("createTexture: '{}' must be placed in MemoryKind::Private memory",
                           desc.label));

    const SizeAlign required = textureSizeAlign(device, desc);
    LMX_ASSERT(required.alignment == 0 || placement % required.alignment == 0,
               std::format("createTexture: placement address {:#x} for '{}' violates the {}-byte "
                           "alignment the device requires",
                           placement, desc.label, required.alignment));
    const uint64_t offsetInAllocation = placement - record->base;
    LMX_ASSERT(offsetInAllocation + required.size <= record->size,
               std::format("createTexture: '{}' needs {} bytes at offset {} of a {}-byte "
                           "allocation",
                           desc.label, required.size, offsetInAllocation, record->size));

    auto texture = std::make_unique<Texture>();
    texture->handle = NS::TransferPtr(record->heap->newTexture(
        makeDescriptor(desc).get(), offsetInAllocation + record->bufferOffset));
    if (!texture->handle) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "createTexture: the device rejected texture '" + std::string(desc.label) +
                        "' at the requested placement");
    }
    texture->label = std::string(desc.label);
    texture->desc = desc;
    texture->desc.label = texture->label;
    texture->placement = placement;
    texture->handle->setLabel(makeString(texture->label).get());

    // The placement count is what makes freeing memory a texture still lives in diagnosable.
    record->placedTextures += 1;
    device->liveTextures += 1;
    return texture.release();
}

//======================================================================================================================
void destroyTexture(Device* device, Texture* texture) {
    LMX_ASSERT(device != nullptr, "destroyTexture: device must not be null");
    LMX_ASSERT(texture != nullptr, "destroyTexture: texture must not be null");
    assertNoWorkInFlight(device, "destroyTexture");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    if (device->table != nullptr) {
        const std::vector<BindlessSlot>& slots = device->table->slots;
        const auto held = std::find_if(slots.begin(), slots.end(), [&](const BindlessSlot& slot) {
            return slot.texture == texture;
        });
        LMX_ASSERT(held == slots.end(),
                   std::format("destroyTexture: bindless slot {} still holds a view of '{}' -- "
                               "clear it before destroying the texture",
                               std::distance(slots.begin(), held), texture->label));
    }

    AllocationRecord* record = findAllocation(device, texture->placement);
    LMX_ASSERT(record != nullptr, "destroyTexture: the texture's allocation was already freed");
    record->placedTextures -= 1;

    LMX_ASSERT(device->liveTextures > 0, "destroyTexture: no texture is live on this device");
    device->liveTextures -= 1;
    delete texture;
}

//======================================================================================================================
const TextureDesc& textureDesc(const Texture* texture) {
    LMX_ASSERT(texture != nullptr, "textureDesc: texture must not be null");
    return texture->desc;
}

} // namespace lmx::noapi
