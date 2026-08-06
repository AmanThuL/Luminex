#include "RHI/Metal4/Metal4Resources.h"

#include "Core/Assert.h"

namespace lmx::rhi::metal4 {

ResidencyRegistration::ResidencyRegistration(NS::SharedPtr<MTL::ResidencySet> residency,
                                             const MTL::Allocation* allocation)
    : m_residency(std::move(residency)), m_allocation(allocation) {
    if (!m_residency) {
        return;
    }
    LMX_ASSERT(m_allocation != nullptr, "ResidencyRegistration: allocation must not be null");
    // Metal 4 makes nothing resident implicitly -- anything a command buffer may touch has
    // to be in a set attached to the queue. commit() republishes the whole allocation list;
    // the queue attachment itself is made once, when the device is created.
    m_residency->addAllocation(m_allocation);
    m_residency->commit();
}

ResidencyRegistration::~ResidencyRegistration() {
    if (!m_residency) {
        return;
    }
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    // Without this the set keeps its own reference to the allocation for the lifetime of the
    // process, so a resource the caller has released stays resident on the GPU. Harmless for
    // a handful of long-lived M1 resources; a leak-shaped growth once the swapchain starts
    // recreating textures on every resize.
    m_residency->removeAllocation(m_allocation);
    m_residency->commit();
}

void Metal4Texture::readback(void* out, uint64_t outSize) {
    LMX_ASSERT(out != nullptr, "Texture::readback: destination must not be null");
    LMX_ASSERT(m_cpuReadback,
               "Texture::readback: texture was not created with TextureDesc.cpuReadback");
    // Validate() already restricted cpuReadback to the 8-bit formats, so 4 bytes per pixel
    // is exact and the whole surface is one tightly packed block.
    const uint64_t expected = uint64_t{m_width} * m_height * 4;
    LMX_ASSERT(outSize == expected, "Texture::readback: outSize must be width*height*4");

    const MTL::Region region = MTL::Region::Make2D(0, 0, m_width, m_height);
    m_texture->getBytes(out, m_width * 4, region, 0);
}

} // namespace lmx::rhi::metal4
