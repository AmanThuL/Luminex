#pragma once

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace lmx::test {

//======================================================================================================================
template <typename T, typename Error>
std::string errorOf(const std::expected<T, Error>& result) {
    return result ? std::string{} : result.error().message;
}

//======================================================================================================================
inline std::unique_ptr<engine::GpuContext> createTestGpu(engine::GpuContextDesc desc = {}) {
    auto result = engine::GpuContext::create(desc);
    INFO(errorOf(result));
    REQUIRE(result.has_value());
    return std::move(*result);
}

//======================================================================================================================
inline engine::GpuTexture createTestTexture(engine::GpuContext& gpu, rojoRHI::Format format,
                                            rojoRHI::TextureUsage usage, uint32_t width = 64,
                                            uint32_t height = 64, uint32_t mips = 1) {
    auto result = engine::GpuTexture::create(gpu.device(), {.format = format,
                                                            .extent = {width, height, 1},
                                                            .mips = mips,
                                                            .usage = usage,
                                                            .label = "lmx.test.texture"});
    INFO(errorOf(result));
    REQUIRE(result.has_value());
    return std::move(*result);
}

//======================================================================================================================
inline engine::GpuBuffer createTestBuffer(engine::GpuContext& gpu, uint64_t bytes,
                                          rojoRHI::BufferUsage usage) {
    auto result = engine::GpuBuffer::create(
        gpu.device(), {.size = bytes, .usage = usage, .label = "lmx.test.buffer"});
    INFO(errorOf(result));
    REQUIRE(result.has_value());
    return std::move(*result);
}

//======================================================================================================================
inline constexpr bool trackerAvailable() {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

} // namespace lmx::test

using lmx::test::createTestGpu;
using lmx::test::errorOf;
using lmx::test::trackerAvailable;

using lmx::test::createTestBuffer;
using lmx::test::createTestTexture;
