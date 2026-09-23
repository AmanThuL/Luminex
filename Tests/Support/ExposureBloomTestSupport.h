#pragma once

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace lmx::test {

//======================================================================================================================
template <typename T>
std::string errorOf(const rojoRHI::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

//======================================================================================================================
// A half-float bit pattern for 2^exponent, exact because every power of two in the normal range
// has a zero mantissa -- so uploads need no float-to-half rounding logic to reason about.
constexpr uint16_t halfPow2(int exponent) {
    return static_cast<uint16_t>((exponent + 15) << 10);
}

//======================================================================================================================
inline rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
makeSceneColorTexture(rojoRHI::Device& device, uint32_t width, uint32_t height,
                      std::span<const uint16_t> rgbaHalf, const char* label) {
    const rojoRHI::TextureMip mip{.data = rgbaHalf.data(),
                                  .bytesPerRow = uint64_t{width} * 4 * sizeof(uint16_t)};
    return device.createTexture({.width = width,
                                 .height = height,
                                 .format = rojoRHI::Format::RGBA16Float,
                                 .sampled = true,
                                 .label = label},
                                std::span{&mip, 1});
}

} // namespace lmx::test
