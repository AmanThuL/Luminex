#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Asset/Asset.h"
#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Geometry/Mesh.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/View/Camera.h"
#include "Render/Gpu/Readback.h"
#include "Render/Renderer/Renderer.h"
#include "Support/OracleRoots.h"
#include "Support/TestGpu.h"
#include <rojoRHI/RHI.h>

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Vertex {
    float position[2];
    float color[3];
};
static_assert(sizeof(Vertex) == 20, "must match Slang's packed Vertex_natural_0 layout");

constexpr std::array<Vertex, 3> kTriangle = {{
    {{0.0f, 0.5f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
}};

// Small enough that the whole test is a few milliseconds, large enough that each probe below
// sits far enough inside its region to be deterministically interior without MSAA; see the
// per-probe margins noted at each REQUIRE below.
constexpr uint32_t kSize = 64;

// One BGRA8Unorm texel, in the channel order readback() produces.
struct Pixel {
    uint8_t b = 0, g = 0, r = 0, a = 0;
};

//======================================================================================================================
// [[maybe_unused]]: a header-defined helper is unused in whichever translation units declare no
// pixel probes of their own -- a buffer-only GPU test, for instance -- and that is not a warning.
[[maybe_unused]] Pixel pixelAt(const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * kSize + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

//======================================================================================================================
// Catch2 stringifies the comparison operands, but not the pixel they came from; this goes
// through INFO so a failure says which colour was actually there.
[[maybe_unused]] std::string describe(const char* what, uint32_t x, uint32_t y, const Pixel& p) {
    return std::string(what) + " (" + std::to_string(x) + "," + std::to_string(y) +
           "): B=" + std::to_string(p.b) + " G=" + std::to_string(p.g) +
           " R=" + std::to_string(p.r) + " A=" + std::to_string(p.a);
}

//======================================================================================================================
// For probes whose expected value is an arithmetic result rather than an on/off channel.
inline bool channelNear(uint8_t actual, int expected, int tolerance) {
    return std::abs(int{actual} - expected) <= tolerance;
}

inline lmx::Result<lmx::engine::GpuTexture> makeProbeTarget(lmx::engine::GpuContext& gpu,
                                                            const char* label) {
    return lmx::engine::GpuTexture::create(gpu.device(),
                                           {.format = rojoRHI::Format::BGRA8Unorm,
                                            .extent = {kSize, kSize, 1},
                                            .usage = rojoRHI::TextureUsage::RenderTarget,
                                            .label = label});
}

inline std::vector<uint8_t> renderSampledImage(lmx::engine::GpuContext& gpu,
                                               rojoRHI::Pipeline pipeline,
                                               const lmx::engine::GpuTexture& source,
                                               rojoRHI::SamplerIndex sampler,
                                               const lmx::engine::GpuTexture& destination,
                                               lmx::render::TextureStateTable& states) {
    using namespace rojoRHI;
    states.seedPhysical(source.rhi(), source.desc().mips, source.desc().layers,
                        source.initialLayout());
    states.seedPhysical(destination.rhi(), destination.desc().mips, destination.desc().layers,
                        destination.initialLayout());
    const SubresourceRange destinationRange{0, 1, 0, 1};
    std::vector<TextureBarrier> barriers;
    for (const auto& entry : states.physicalRanges(source.rhi(), {})) {
        barriers.push_back({.texture = source.rhi(),
                            .range = entry.range,
                            .srcStage = entry.state.stage,
                            .srcAccess = entry.state.access,
                            .dstStage = Stage::FragmentShader,
                            .dstAccess = Access::ShaderRead,
                            .oldLayout = entry.state.layout,
                            .newLayout = Layout::ShaderRead});
    }
    for (const auto& entry : states.physicalRanges(destination.rhi(), destinationRange)) {
        barriers.push_back({.texture = destination.rhi(),
                            .range = entry.range,
                            .srcStage = entry.state.stage,
                            .srcAccess = entry.state.access,
                            .dstStage = Stage::ColorAttachment,
                            .dstAccess = Access::ColorAttachmentWrite,
                            .oldLayout = entry.state.layout,
                            .newLayout = Layout::ColorAttachment});
    }
    auto frame = gpu.beginFrame();
    auto& commands = frame.commands;
    commands.barrier({}, {}, barriers);
    const rojoRHI::ColorAttachment color{.texture = destination.rhi(),
                                         .load = rojoRHI::LoadOp::Clear,
                                         .clear = {1.0f, 0.0f, 1.0f, 1.0f}};
    const auto extent = destination.ref().extent;
    commands.beginRenderPass({.colors = std::span(&color, 1),
                              .area = {0, 0, extent.width, extent.height},
                              .label = "rojorhi.test.textureUpload.probe"});
    commands.setPipeline(pipeline);
    commands.setViewport(
        {.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)});
    commands.setScissor({0, 0, extent.width, extent.height});
    commands.pushRoot(lmx::test::SamplerRoot{.source = static_cast<uint32_t>(source.view()),
                                             .sampler = static_cast<uint32_t>(sampler)});
#ifndef NDEBUG
    const ResourceAccess access{
        .texture = source.rhi(), .stage = Stage::FragmentShader, .access = Access::ShaderRead};
    commands.declareAccess(std::span(&access, 1));
#endif
    commands.draw(3);
    commands.endRenderPass();
    states.set(source.rhi(), {}, {Stage::FragmentShader, Access::ShaderRead, Layout::ShaderRead});
    states.set(destination.rhi(), destinationRange,
               {Stage::ColorAttachment, Access::ColorAttachmentWrite, Layout::ColorAttachment});
    gpu.submitAndEndFrame(frame);
    gpu.waitIdle();
    auto bytes = lmx::render::readbackNow(gpu, destination, states);
    INFO(errorOf(bytes));
    REQUIRE(bytes);
    return std::move(*bytes);
}

} // namespace
