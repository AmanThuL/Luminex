#include "Engine/Asset/Image/PngImage.h"
#include "Render/Passes/Display/DisplayRoots.h"
#include "Render/Renderer/DisplayDomain.h"
#include "Render/Renderer/Renderer.h"
#include "Support/DisplayTransformOracle.h"
#include "Support/GpuTestSupport.h"
#include <cstring>
#include <rojoRHI/RHI.h>

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace lmx;

struct Fixture {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bloomWidth = 1;
    uint32_t bloomHeight = 1;
    float bloomIntensity = 0.0f;
    std::vector<uint16_t> scene;
    std::vector<uint16_t> bloom{0, 0, 0, 0};
};

//======================================================================================================================
void appendHalf(std::vector<uint16_t>& pixels, const glm::vec3& rgb) {
    for (int channel = 0; channel < 3; ++channel) {
        pixels.push_back(glm::packHalf1x16(rgb[channel]));
    }
    pixels.push_back(0); // Input coverage must not leak into the opaque display target.
}

//======================================================================================================================
glm::vec3 halfTexel(std::span<const uint16_t> pixels, uint32_t width, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * width + x) * 4;
    return {glm::unpackHalf1x16(pixels[offset]), glm::unpackHalf1x16(pixels[offset + 1]),
            glm::unpackHalf1x16(pixels[offset + 2])};
}

//======================================================================================================================
glm::vec3 hueAt(float t) {
    const float phase = t * 6.0f;
    const auto component = [phase](float offset) {
        const float wrapped = std::fmod(phase + offset, 6.0f);
        return std::clamp(std::abs(wrapped - 3.0f) - 1.0f, 0.0f, 1.0f);
    };
    const glm::vec3 hue{component(0.0f), component(4.0f), component(2.0f)};
    return hue * (1.5f / glm::dot(hue, glm::vec3{0.2126f, 0.7152f, 0.0722f}));
}

//======================================================================================================================
Fixture makeRamps() {
    Fixture fixture{.width = 256, .height = 64};
    const float grey = test::srgbDecode(0.46f);
    const std::array<glm::vec3, 6> patches{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {grey, grey, grey}, {1, 1, 1}, {0, 0, 0}}};
    for (uint32_t y = 0; y < fixture.height; ++y) {
        for (uint32_t x = 0; x < fixture.width; ++x) {
            const float t = static_cast<float>(x) / 255.0f;
            glm::vec3 color{};
            switch (y / 8) {
            case 0:
                color = glm::vec3{t};
                break;
            case 1:
                color = glm::vec3{4.0f * t};
                break;
            case 2:
                color = glm::vec3{0.18f * std::exp2(-8.0f + 12.0f * t)};
                break;
            case 3:
                color.r = 2.0f * t;
                break;
            case 4:
                color.g = 2.0f * t;
                break;
            case 5:
                color.b = 2.0f * t;
                break;
            case 6:
                color = patches[x * 6 / fixture.width];
                break;
            case 7:
                color = hueAt(t);
                break;
            }
            appendHalf(fixture.scene, color);
        }
    }
    return fixture;
}

//======================================================================================================================
Fixture makeComposite() {
    Fixture fixture{
        .width = 64, .height = 64, .bloomWidth = 32, .bloomHeight = 32, .bloomIntensity = 0.2f};
    fixture.bloom.clear();
    for (uint32_t y = 0; y < fixture.height; ++y) {
        for (uint32_t x = 0; x < fixture.width; ++x) {
            appendHalf(fixture.scene, glm::vec3{static_cast<float>(x) / 63.0f});
        }
    }
    for (uint32_t y = 0; y < fixture.bloomHeight; ++y) {
        for (uint32_t x = 0; x < fixture.bloomWidth; ++x) {
            appendHalf(fixture.bloom, {static_cast<float>(x) / 31.0f, static_cast<float>(y) / 31.0f,
                                       (x + y) % 2 == 0 ? 0.125f : 0.5f});
        }
    }
    return fixture;
}

//======================================================================================================================
glm::vec3 bloomAt(const Fixture& fixture, uint32_t x, uint32_t y) {
    const float px =
        std::clamp((static_cast<float>(x) + 0.5f) * fixture.bloomWidth / fixture.width - 0.5f, 0.0f,
                   static_cast<float>(fixture.bloomWidth - 1));
    const float py =
        std::clamp((static_cast<float>(y) + 0.5f) * fixture.bloomHeight / fixture.height - 0.5f,
                   0.0f, static_cast<float>(fixture.bloomHeight - 1));
    const auto x0 = static_cast<uint32_t>(px);
    const auto y0 = static_cast<uint32_t>(py);
    const uint32_t x1 = std::min(x0 + 1, fixture.bloomWidth - 1);
    const uint32_t y1 = std::min(y0 + 1, fixture.bloomHeight - 1);
    const auto at = [&fixture](uint32_t bx, uint32_t by) {
        return halfTexel(fixture.bloom, fixture.bloomWidth, bx, by);
    };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), px - x0),
                    glm::mix(at(x0, y1), at(x1, y1), px - x0), py - y0);
}

//======================================================================================================================
engine::GpuTexture upload(rojoRHI::Device device, uint32_t width, uint32_t height,
                          std::span<const uint16_t> pixels, const char* label) {
    const rojoRHI::TextureData mip{.bytes = std::as_bytes(pixels),
                                   .bytesPerRow = uint64_t{width} * 8};
    auto texture = engine::GpuTexture::create(device,
                                              {.format = render::kSceneColorFormat,
                                               .extent = {width, height, 1},
                                               .usage = rojoRHI::TextureUsage::Sampled,
                                               .label = label},
                                              std::span{&mip, 1});
    INFO((texture ? "" : texture.error().message));
    REQUIRE(texture.has_value());
    return std::move(*texture);
}

//======================================================================================================================
std::vector<uint8_t> renderFixture(const Fixture& fixture) {
    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);
    auto library = gpu->loadShaderLibrary("DisplayTransform");
    INFO((library ? "" : library.error().message));
    REQUIRE(library.has_value());
    const rojoRHI::ColorTarget colorFormat{.format = render::kDisplayFormat};
    auto pipeline = engine::adoptGpu(gpu->device(),
                                     gpu->device().createPipeline(rojoRHI::GraphicsPipelineDesc{
                                         .vertex = {library->get(), "vertexMain"},
                                         .fragment = {library->get(), "fragmentMain"},
                                         .raster = {.cull = rojoRHI::CullMode::Back},
                                         .depthStencil = {.depthCompare = rojoRHI::CompareOp::Less},
                                         .colors = std::span(&colorFormat, 1),
                                         .label = "lmx.test.displayGoldenPipeline"}));
    INFO((pipeline ? "" : pipeline.error().message));
    REQUIRE(pipeline.has_value());
    auto scene = upload(gpu->device(), fixture.width, fixture.height, fixture.scene,
                        "lmx.test.displayGoldenScene");
    auto bloom = upload(gpu->device(), fixture.bloomWidth, fixture.bloomHeight, fixture.bloom,
                        "lmx.test.displayGoldenBloom");
    auto target = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.format = render::kDisplayFormat,
                                                   .extent = {fixture.width, fixture.height, 1},
                                                   .usage = rojoRHI::TextureUsage::RenderTarget,
                                                   .label = "lmx.test.displayGoldenTarget"});
    INFO((target ? "" : target.error().message));
    REQUIRE(target.has_value());
    auto gpuFrame1 = gpu->beginFrame();
    auto& commands = gpuFrame1.commands;
    const std::array barriers{
        rojoRHI::TextureBarrier{.texture = scene.rhi(),
                                .dstStage = rojoRHI::Stage::FragmentShader,
                                .dstAccess = rojoRHI::Access::ShaderRead,
                                .oldLayout = rojoRHI::Layout::ShaderRead,
                                .newLayout = rojoRHI::Layout::ShaderRead},
        rojoRHI::TextureBarrier{.texture = bloom.rhi(),
                                .dstStage = rojoRHI::Stage::FragmentShader,
                                .dstAccess = rojoRHI::Access::ShaderRead,
                                .oldLayout = rojoRHI::Layout::ShaderRead,
                                .newLayout = rojoRHI::Layout::ShaderRead},
        rojoRHI::TextureBarrier{.texture = target->rhi(),
                                .dstStage = rojoRHI::Stage::ColorAttachment,
                                .dstAccess = rojoRHI::Access::ColorAttachmentWrite,
                                .newLayout = rojoRHI::Layout::ColorAttachment}};
    commands.barrier({}, {}, barriers);
    const rojoRHI::ColorAttachment color{.texture = target->rhi(), .load = rojoRHI::LoadOp::Clear};
    commands.beginRenderPass({.colors = std::span(&color, 1),
                              .area = {0, 0, fixture.width, fixture.height},
                              .label = "lmx.test.displayGolden"});
    commands.setPipeline(pipeline->get());
    commands.setViewport(
        {.width = static_cast<float>(fixture.width), .height = static_cast<float>(fixture.height)});
    commands.setScissor({0, 0, fixture.width, fixture.height});
    commands.pushRoot(render::DisplayRoot{.sceneColor = static_cast<uint32_t>(scene.view()),
                                          .bloom = static_cast<uint32_t>(bloom.view()),
                                          .bloomIntensity = fixture.bloomIntensity});
#ifndef NDEBUG
    const std::array accesses{rojoRHI::ResourceAccess{.texture = scene.rhi(),
                                                      .stage = rojoRHI::Stage::FragmentShader,
                                                      .access = rojoRHI::Access::ShaderRead},
                              rojoRHI::ResourceAccess{.texture = bloom.rhi(),
                                                      .stage = rojoRHI::Stage::FragmentShader,
                                                      .access = rojoRHI::Access::ShaderRead}};
    commands.declareAccess(std::span(accesses).first(fixture.bloomIntensity == 0.0f ? 1 : 2));
#endif
    commands.draw(3);
    commands.endRenderPass();
    gpu->submitAndEndFrame(gpuFrame1);
    gpu->waitIdle();
    std::vector<uint8_t> rgba(size_t{fixture.width} * fixture.height * 4);
    {
        const std::array current{render::TextureRangeState{
            .range = {0, 1, 0, 1},
            .state = {rojoRHI::Stage::ColorAttachment, rojoRHI::Access::ColorAttachmentWrite,
                      rojoRHI::Layout::ColorAttachment}}};
        const auto readbackBytes = lmx::render::readbackNow(*gpu, *target, current);
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        std::memcpy(rgba.data(), readbackBytes->data(), rgba.size());
    }
    for (size_t offset = 0; offset < rgba.size(); offset += 4) {
        std::swap(rgba[offset], rgba[offset + 2]);
    }
    return rgba;
}

//======================================================================================================================
void verifyFixture(const Fixture& fixture, const char* filename) {
    const auto actual = renderFixture(fixture);
    size_t oracleFailures = 0;
    int oracleMax = 0;
    for (uint32_t y = 0; y < fixture.height; ++y) {
        for (uint32_t x = 0; x < fixture.width; ++x) {
            const auto expected =
                test::displayBytes(halfTexel(fixture.scene, fixture.width, x, y) +
                                   bloomAt(fixture, x, y) * fixture.bloomIntensity);
            const size_t offset = (size_t{y} * fixture.width + x) * 4;
            for (size_t channel = 0; channel < 4; ++channel) {
                const int delta = std::abs(int{actual[offset + channel]} -
                                           (channel == 3 ? 255 : expected[channel]));
                oracleMax = std::max(oracleMax, delta);
                oracleFailures += delta > (channel == 3 ? 0 : 1);
            }
        }
    }
    INFO("oracle failing channels=" << oracleFailures << " maximum byte error=" << oracleMax);
    CHECK(oracleFailures == 0);

    const auto path = std::filesystem::path(LMX_REPO_ROOT) / "Tests/Golden" / filename;
    const std::array text{
        asset::PngTextChunk{"lmx:display", render::toJson(render::kSdrDisplayDomain)}};
    const auto write = [&](const std::filesystem::path& output) {
        auto result = asset::writePng(output, actual, fixture.width, fixture.height, text);
        INFO((result ? "" : result.error().message));
        REQUIRE(result.has_value());
    };
    const char* update = std::getenv("LMX_UPDATE_GOLDENS");
    if (update && std::string_view(update) == "1") {
        REQUIRE(oracleFailures == 0);
        write(path);
    }
    auto golden = asset::readPng(path);
    INFO((golden ? "" : golden.error().message));
    if (!golden) {
        write(path.string() + ".actual.png");
    }
    REQUIRE(golden.has_value());
    REQUIRE(golden->width == fixture.width);
    REQUIRE(golden->height == fixture.height);
    REQUIRE(golden->rgba.size() == actual.size());
    size_t failed = 0;
    size_t differing = 0;
    int maximum = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        const int delta = std::abs(int{actual[i]} - int{golden->rgba[i]});
        failed += delta > 1;
        differing += delta != 0;
        maximum = std::max(maximum, delta);
    }
    if (failed || oracleFailures) {
        write(path.string() + ".actual.png");
    }
    INFO(filename << ": failing channels=" << failed << ", differing channels=" << differing
                  << ", maximum byte error=" << maximum);
    REQUIRE(failed == 0);
}

} // namespace

//======================================================================================================================
TEST_CASE("SDR display ramps match the golden and linear-light oracle", "[gpu][display]") {
    verifyFixture(makeRamps(), "display-sdr-ramps.png");
}

//======================================================================================================================
TEST_CASE("SDR display bloom composite matches the golden and oracle", "[gpu][display]") {
    verifyFixture(makeComposite(), "display-sdr-bloom.png");
}
