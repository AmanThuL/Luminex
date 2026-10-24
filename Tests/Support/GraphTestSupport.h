#pragma once

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/LoweredFrame.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace {

//======================================================================================================================
inline rojoRHI::Texture testTexture(uint64_t bits) {
    LMX_ASSERT(bits != 0, "a synthetic texture needs a nonzero identity");
    return {bits};
}

//======================================================================================================================
inline rojoRHI::Buffer testBuffer(uint64_t bits) {
    LMX_ASSERT(bits != 0, "a synthetic buffer needs a nonzero identity");
    return {bits};
}

//======================================================================================================================
inline lmx::engine::TextureRef
testTextureRef(uint64_t bits, rojoRHI::Format format, uint32_t width, uint32_t height,
               uint32_t mips = 1, uint32_t layers = 1,
               rojoRHI::Layout initialLayout = rojoRHI::Layout::Undefined) {
    return {testTexture(bits), lmx::engine::kNoView, {width, height}, format, mips, layers,
            initialLayout};
}

//======================================================================================================================
inline uint64_t nextSyntheticIdentity() {
    static uint64_t next = 1;
    return next++;
}

// Values here describe fixture identity and immutable creation facts; none reaches a device.
//======================================================================================================================
inline lmx::engine::TextureRef textureFixture(uint32_t width, uint32_t height = 0,
                                              std::string_view = {}, uint32_t mips = 1,
                                              uint32_t layers = 1) {
    return testTextureRef(nextSyntheticIdentity(), rojoRHI::Format::Undefined, width,
                          height == 0 ? width : height, mips, layers);
}

//======================================================================================================================
inline lmx::engine::TextureRef formatted(lmx::engine::TextureRef ref, rojoRHI::Format format) {
    ref.format = format;
    return ref;
}

//======================================================================================================================
inline lmx::render::BufferRef bufferFixture(uint64_t size, std::string_view = {}) {
    return {testBuffer(nextSyntheticIdentity()), size, std::nullopt};
}

} // namespace
