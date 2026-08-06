#include "RHI/Metal4/Metal4Resources.h"

#include "Core/Assert.h"

namespace lmx::rhi::metal4 {

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
