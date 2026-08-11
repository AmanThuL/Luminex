//----------------------------------------------------------------------------------------------------------------------
/// @file Sampler.cpp
/// @brief Implements immutable sampler creation for bindless table slots.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <algorithm>
#include <format>

namespace lmx::noapi {
namespace {

//======================================================================================================================
MTL::SamplerMinMagFilter toMinMag(FilterMode filter) {
    return filter == FilterMode::Linear ? MTL::SamplerMinMagFilterLinear
                                        : MTL::SamplerMinMagFilterNearest;
}

//======================================================================================================================
MTL::SamplerMipFilter toMip(FilterMode filter) {
    return filter == FilterMode::Linear ? MTL::SamplerMipFilterLinear
                                        : MTL::SamplerMipFilterNearest;
}

//======================================================================================================================
MTL::SamplerAddressMode toAddress(AddressMode mode) {
    return mode == AddressMode::Repeat ? MTL::SamplerAddressModeRepeat
                                       : MTL::SamplerAddressModeClampToEdge;
}

} // namespace

//======================================================================================================================
Result<Sampler*> createSampler(Device* device, const SamplerDesc& desc) {
    LMX_ASSERT(device != nullptr, "createSampler: device must not be null");
    LMX_ASSERT(!desc.label.empty(), "createSampler: SamplerDesc.label must not be empty");
    LMX_ASSERT(desc.maxAnisotropy >= 1, "createSampler: SamplerDesc.maxAnisotropy must be at least "
                                        "one");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto samplerDesc = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
    samplerDesc->setMinFilter(toMinMag(desc.minFilter));
    samplerDesc->setMagFilter(toMinMag(desc.magFilter));
    samplerDesc->setMipFilter(toMip(desc.mipFilter));
    samplerDesc->setSAddressMode(toAddress(desc.addressU));
    samplerDesc->setTAddressMode(toAddress(desc.addressV));
    samplerDesc->setRAddressMode(toAddress(desc.addressW));
    samplerDesc->setMaxAnisotropy(desc.maxAnisotropy);
    samplerDesc->setCompareFunction(desc.compare ? toMTL(desc.compareOp)
                                                 : MTL::CompareFunctionNever);
    // gpuResourceID is valid only for samplers that opt into argument-buffer use, and a bindless
    // slot is nothing but that ID.
    samplerDesc->setSupportArgumentBuffers(true);
    samplerDesc->setLabel(makeString(desc.label).get());

    auto sampler = std::make_unique<Sampler>();
    sampler->handle = NS::TransferPtr(device->mtl->newSamplerState(samplerDesc.get()));
    if (!sampler->handle) {
        // newSamplerState reports failure only by returning null.
        return fail(ErrorCode::ResourceCreationFailed,
                    "createSampler: the device rejected sampler '" + std::string(desc.label) + "'");
    }

    device->liveSamplers += 1;
    return sampler.release();
}

//======================================================================================================================
void destroySampler(Device* device, Sampler* sampler) {
    LMX_ASSERT(device != nullptr, "destroySampler: device must not be null");
    LMX_ASSERT(sampler != nullptr, "destroySampler: sampler must not be null");
    assertNoWorkInFlight(device, "destroySampler");

    if (device->table != nullptr) {
        const std::vector<BindlessSlot>& slots = device->table->slots;
        const auto held = std::find_if(slots.begin(), slots.end(), [&](const BindlessSlot& slot) {
            return slot.sampler == sampler;
        });
        LMX_ASSERT(held == slots.end(),
                   std::format("destroySampler: bindless slot {} still holds this sampler -- clear "
                               "it before destroying the sampler",
                               std::distance(slots.begin(), held)));
    }

    LMX_ASSERT(device->liveSamplers > 0, "destroySampler: no sampler is live on this device");
    device->liveSamplers -= 1;
    delete sampler;
}

} // namespace lmx::noapi
