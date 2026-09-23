#include "Support/ExposureBloomTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace rojoRHI;

using lmx::test::errorOf;
using lmx::test::halfPow2;
using lmx::test::makeSceneColorTexture;

// Mirrors BloomThreshold.slang's kLuminanceWeights.
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};

//======================================================================================================================
// Mirrors BloomThreshold.slang's per-texel formula (the 2x2 box read is the caller's job below,
// since it is identical for every probe pixel this file constructs).
glm::vec3 cpuThreshold(glm::vec3 color, float threshold) {
    const float luminance = glm::dot(color, kLuminanceWeights);
    const float contribution = std::max(luminance - threshold, 0.0f) / std::max(luminance, 1e-4f);
    return color * contribution;
}

// A tiny CPU grid with BloomDownsample.slang/BloomUpsample.slang's clamped-coordinate filters,
// used to build the full-chain reference the bloom energy oracle compares the GPU chain against.
struct Grid {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<glm::vec3> texels;

    //==================================================================================================================
    Grid(uint32_t w, uint32_t h) : width(w), height(h), texels(size_t{w} * h, glm::vec3(0.0f)) {}

    //==================================================================================================================
    glm::vec3& at(uint32_t x, uint32_t y) { return texels[size_t{y} * width + x]; }

    //==================================================================================================================
    glm::vec3 load(uint32_t x, uint32_t y) const {
        const uint32_t cx = std::min(x, width - 1);
        const uint32_t cy = std::min(y, height - 1);
        return texels[size_t{cy} * width + cx];
    }
};

//======================================================================================================================
// Mirrors BloomDownsample.slang.
Grid cpuDownsample(const Grid& src, uint32_t dstWidth, uint32_t dstHeight) {
    Grid dst(dstWidth, dstHeight);
    for (uint32_t y = 0; y < dstHeight; ++y) {
        for (uint32_t x = 0; x < dstWidth; ++x) {
            const uint32_t bx = x * 2, by = y * 2;
            const glm::vec3 sum = src.load(bx, by) + src.load(bx + 1, by) + src.load(bx, by + 1) +
                                  src.load(bx + 1, by + 1);
            dst.at(x, y) = sum * 0.25f;
        }
    }
    return dst;
}

//======================================================================================================================
// Mirrors BloomUpsample.slang.
Grid cpuUpsampleAccumulate(const Grid& base, const Grid& small) {
    Grid dst(base.width, base.height);
    for (uint32_t y = 0; y < base.height; ++y) {
        for (uint32_t x = 0; x < base.width; ++x) {
            const float sx =
                std::clamp((static_cast<float>(x) + 0.5f) * small.width / base.width - 0.5f, 0.0f,
                           static_cast<float>(small.width - 1));
            const float sy =
                std::clamp((static_cast<float>(y) + 0.5f) * small.height / base.height - 0.5f, 0.0f,
                           static_cast<float>(small.height - 1));
            const auto x0 = static_cast<uint32_t>(sx);
            const auto y0 = static_cast<uint32_t>(sy);
            const glm::vec3 top = glm::mix(small.load(x0, y0), small.load(x0 + 1, y0), sx - x0);
            const glm::vec3 bottom =
                glm::mix(small.load(x0, y0 + 1), small.load(x0 + 1, y0 + 1), sx - x0);
            dst.at(x, y) = base.load(x, y) + glm::mix(top, bottom, sy - y0);
        }
    }
    return dst;
}

} // namespace

//======================================================================================================================
// Renderer uses ceil(scene / 2) for bloom mip 0 so an odd-sized image's final source row and column
// still have a destination texel. Put the only above-threshold value in that bottom-right corner:
// floor division would allocate and dispatch only 2x1 here and silently lose the probe.
TEST_CASE("bloom threshold preserves the final texel of an odd-sized image", "[gpu]") {
    constexpr uint32_t kSrcWidth = 5, kSrcHeight = 3;
    constexpr uint32_t kDstWidth = 3, kDstHeight = 2;
    constexpr float kThreshold = 4.0f;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/BloomThreshold");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline =
        (*device)->createComputePipeline({.library = library->get(),
                                          .computeEntry = "computeBloomThreshold",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.oddBloomThresholdPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    std::vector<uint16_t> source(size_t{kSrcWidth} * kSrcHeight * 4, halfPow2(0));
    const size_t probe = (size_t{kSrcHeight - 1} * kSrcWidth + (kSrcWidth - 1)) * 4;
    source[probe] = halfPow2(4);
    source[probe + 1] = halfPow2(4);
    source[probe + 2] = halfPow2(4);
    auto scene =
        makeSceneColorTexture(**device, kSrcWidth, kSrcHeight, source, "lmx.test.oddBloomScene");
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());

    auto bloom = (*device)->createTexture({.width = kDstWidth,
                                           .height = kDstHeight,
                                           .format = Format::RGBA16Float,
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.oddBloomMip0"});
    INFO(errorOf(bloom));
    REQUIRE(bloom.has_value());

    struct ThresholdParams {
        float threshold;
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    const ThresholdParams params{.threshold = kThreshold,
                                 .srcWidth = kSrcWidth,
                                 .srcHeight = kSrcHeight,
                                 .dstWidth = kDstWidth,
                                 .dstHeight = kDstHeight};

    CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.oddBloomThreshold");
    commands.bindComputePipeline(**pipeline);
    commands.bindTexture(0, **scene);
    commands.bindStorageTexture(1, **bloom, {}, StorageAccess::Write);
    commands.bindFrameData(0, params);
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> result(size_t{kDstWidth} * kDstHeight * 4);
    (*bloom)->readback(result.data(), result.size() * sizeof(uint16_t));
    const size_t last = (size_t{kDstHeight - 1} * kDstWidth + (kDstWidth - 1)) * 4;
    REQUIRE(result[last] != 0);
    REQUIRE(result[last + 1] != 0);
    REQUIRE(result[last + 2] != 0);
}

//======================================================================================================================
// Below-threshold content contributes nothing (an exact CPU match against every mip-0 texel the
// CPU chain also computes as zero, not just an ad hoc "elsewhere"), and a bright probe's energy
// after the full chain matches a CPU port of the same kernels within a stated tolerance -- spec
// 10's bloom energy oracle. Runs Renderer.cpp's own chain depth (kMaxBloomDownsampleLevels == 4,
// clamped for the extent), not a shortened stand-in, so the oracle covers what actually ships.
TEST_CASE("bloom threshold and the full four-level chain match a CPU reference", "[gpu]") {
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto thresholdLibrary = (*device)->loadShaderLibrary("Shaders/BloomThreshold");
    INFO(errorOf(thresholdLibrary));
    REQUIRE(thresholdLibrary.has_value());
    auto thresholdPipeline =
        (*device)->createComputePipeline({.library = thresholdLibrary->get(),
                                          .computeEntry = "computeBloomThreshold",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomThresholdPipeline"});
    INFO(errorOf(thresholdPipeline));
    REQUIRE(thresholdPipeline.has_value());
    auto downsampleLibrary = (*device)->loadShaderLibrary("Shaders/BloomDownsample");
    INFO(errorOf(downsampleLibrary));
    REQUIRE(downsampleLibrary.has_value());
    auto downsamplePipeline =
        (*device)->createComputePipeline({.library = downsampleLibrary->get(),
                                          .computeEntry = "computeBloomDownsample",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomDownsamplePipeline"});
    INFO(errorOf(downsamplePipeline));
    REQUIRE(downsamplePipeline.has_value());
    auto upsampleLibrary = (*device)->loadShaderLibrary("Shaders/BloomUpsample");
    INFO(errorOf(upsampleLibrary));
    REQUIRE(upsampleLibrary.has_value());
    auto upsamplePipeline =
        (*device)->createComputePipeline({.library = upsampleLibrary->get(),
                                          .computeEntry = "computeBloomUpsample",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomUpsamplePipeline"});
    INFO(errorOf(upsamplePipeline));
    REQUIRE(upsamplePipeline.has_value());

    // 64x64 scene -> bloomChain mip 0..4 (32, 16, 8, 4, 2) -- Renderer.cpp's own
    // kMaxBloomDownsampleLevels -- -> bloomBlur mip 0..3 (32, 16, 8, 4). One bright probe texel,
    // well above threshold; a uniform background well below it.
    constexpr uint32_t kSceneSize = 64;
    constexpr uint32_t kLevels = 4; // downsample steps; mirrors Renderer.cpp's constant
    constexpr uint32_t kMip0Size = kSceneSize / 2;
    constexpr float kThreshold = 4.0f;
    constexpr uint32_t kProbeX = 0, kProbeY = 0; // scene texel; lands at bloom mip0 texel (0, 0)

    std::array<uint32_t, kLevels + 1> chainSize{};
    chainSize[0] = kMip0Size;
    for (uint32_t level = 1; level <= kLevels; ++level) {
        chainSize[level] = chainSize[level - 1] / 2;
    }

    std::vector<glm::vec3> scenePixels(size_t{kSceneSize} * kSceneSize, glm::vec3(1.0f));
    scenePixels[kProbeY * kSceneSize + kProbeX] = glm::vec3(16.0f);

    std::vector<uint16_t> rgbaHalf(scenePixels.size() * 4);
    for (size_t i = 0; i < scenePixels.size(); ++i) {
        const int exponent = static_cast<int>(std::round(std::log2(scenePixels[i].r)));
        const uint16_t channel = halfPow2(exponent);
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0);
    }
    auto sceneColor =
        makeSceneColorTexture(**device, kSceneSize, kSceneSize, rgbaHalf, "lmx.test.bloomScene");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto bloomChain = (*device)->createTexture({.width = kMip0Size,
                                                .height = kMip0Size,
                                                .format = Format::RGBA16Float,
                                                .mipLevels = kLevels + 1,
                                                .storageRead = true,
                                                .storageWrite = true,
                                                .label = "lmx.test.bloomChain"});
    INFO(errorOf(bloomChain));
    REQUIRE(bloomChain.has_value());
    auto bloomBlur = (*device)->createTexture({.width = kMip0Size,
                                               .height = kMip0Size,
                                               .format = Format::RGBA16Float,
                                               .mipLevels = kLevels,
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = true,
                                               .label = "lmx.test.bloomBlur"});
    INFO(errorOf(bloomBlur));
    REQUIRE(bloomBlur.has_value());

    struct ThresholdParams {
        float threshold;
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    struct DownsampleParams {
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    struct UpsampleParams {
        uint32_t smallWidth, smallHeight, dstWidth, dstHeight;
    };
    const auto mipView = [](uint32_t level) {
        return TextureViewDesc{.range = {.baseMipLevel = level, .mipLevelCount = 1}};
    };

    CommandList& commands = (*device)->beginFrame();

    const ThresholdParams thresholdParams{.threshold = kThreshold,
                                          .srcWidth = kSceneSize,
                                          .srcHeight = kSceneSize,
                                          .dstWidth = kMip0Size,
                                          .dstHeight = kMip0Size};
    commands.beginComputePass("lmx.test.bloomThreshold");
    commands.bindComputePipeline(**thresholdPipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageTexture(1, **bloomChain, mipView(0), StorageAccess::Write);
    commands.bindFrameData(0, thresholdParams);
    commands.dispatch((kMip0Size + 7) / 8, (kMip0Size + 7) / 8, 1);
    commands.endComputePass();

    // Downsample chain: mip (L - 1) -> mip L, one pass per level.
    for (uint32_t level = 1; level <= kLevels; ++level) {
        commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);
        const DownsampleParams params{.srcWidth = chainSize[level - 1],
                                      .srcHeight = chainSize[level - 1],
                                      .dstWidth = chainSize[level],
                                      .dstHeight = chainSize[level]};
        commands.beginComputePass("lmx.test.bloomDownsample");
        commands.bindComputePipeline(**downsamplePipeline);
        commands.bindStorageTexture(0, **bloomChain, mipView(level - 1), StorageAccess::Read);
        commands.bindStorageTexture(1, **bloomChain, mipView(level), StorageAccess::Write);
        commands.bindFrameData(0, params);
        commands.dispatch((params.dstWidth + 7) / 8, (params.dstHeight + 7) / 8, 1);
        commands.endComputePass();
    }

    // Upsample-accumulate: walks kLevels back down to mip 0, one pass per level. The first step's
    // "small" input is bloomChain's own smallest mip; every later step's is the previous step's
    // own bloomBlur output.
    commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);
    for (uint32_t stepsRemaining = kLevels; stepsRemaining > 0; --stepsRemaining) {
        const uint32_t level = stepsRemaining - 1; // walks kLevels - 1 down to 0
        const bool smallFromChain = level == kLevels - 1;
        const UpsampleParams params{.smallWidth = chainSize[level + 1],
                                    .smallHeight = chainSize[level + 1],
                                    .dstWidth = chainSize[level],
                                    .dstHeight = chainSize[level]};
        commands.beginComputePass("lmx.test.bloomUpsample");
        commands.bindComputePipeline(**upsamplePipeline);
        commands.bindStorageTexture(0, **bloomChain, mipView(level), StorageAccess::Read);
        if (smallFromChain) {
            commands.bindStorageTexture(1, **bloomChain, mipView(level + 1), StorageAccess::Read);
        } else {
            commands.bindStorageTexture(1, **bloomBlur, mipView(level + 1), StorageAccess::Read);
        }
        commands.bindStorageTexture(2, **bloomBlur, mipView(level), StorageAccess::Write);
        commands.bindFrameData(0, params);
        commands.dispatch((params.dstWidth + 7) / 8, (params.dstHeight + 7) / 8, 1);
        commands.endComputePass();
        if (level > 0) {
            commands.textureBarrier(**bloomBlur, TextureUse::StorageWrite, TextureUse::StorageRead);
        }
    }
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> blurHalf(size_t{kMip0Size} * kMip0Size * 4);
    (*bloomBlur)->readback(blurHalf.data(), blurHalf.size() * sizeof(uint16_t));

    // Decode every FP16 channel, including the small filtered values away from the probe.
    const auto toFloat = [](uint16_t half) {
        const uint32_t sign = (half >> 15) & 1u;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;
        float value;
        if (exponent == 0) {
            value = std::ldexp(static_cast<float>(mantissa), -24);
        } else {
            value =
                std::ldexp(static_cast<float>(mantissa) + 1024.0f, static_cast<int>(exponent) - 25);
        }
        return sign != 0 ? -value : value;
    };
    const auto texelAt = [&](uint32_t x, uint32_t y) {
        const size_t base = (size_t{y} * kMip0Size + x) * 4;
        return glm::vec3(toFloat(blurHalf[base]), toFloat(blurHalf[base + 1]),
                         toFloat(blurHalf[base + 2]));
    };

    // CPU reference: the same kernels, over the same levels, run on the same scene pixels.
    Grid sceneGrid(kSceneSize, kSceneSize);
    for (uint32_t y = 0; y < kSceneSize; ++y) {
        for (uint32_t x = 0; x < kSceneSize; ++x) {
            sceneGrid.at(x, y) = scenePixels[y * kSceneSize + x];
        }
    }
    Grid mip0(kMip0Size, kMip0Size);
    for (uint32_t y = 0; y < kMip0Size; ++y) {
        for (uint32_t x = 0; x < kMip0Size; ++x) {
            const glm::vec3 sum = sceneGrid.load(x * 2, y * 2) + sceneGrid.load(x * 2 + 1, y * 2) +
                                  sceneGrid.load(x * 2, y * 2 + 1) +
                                  sceneGrid.load(x * 2 + 1, y * 2 + 1);
            mip0.at(x, y) = cpuThreshold(sum * 0.25f, kThreshold);
        }
    }
    std::vector<Grid> chainMips;
    chainMips.push_back(mip0);
    for (uint32_t level = 1; level <= kLevels; ++level) {
        chainMips.push_back(cpuDownsample(chainMips.back(), chainSize[level], chainSize[level]));
    }
    Grid accumulated = chainMips[kLevels]; // the smallest mip, as its own "previous step" result
    for (uint32_t stepsRemaining = kLevels; stepsRemaining > 0; --stepsRemaining) {
        const uint32_t level = stepsRemaining - 1;
        accumulated = cpuUpsampleAccumulate(chainMips[level], accumulated);
    }
    const Grid& expectedBlur = accumulated;

    // Below-threshold background: every mip-0 texel the CPU reference itself computed as exactly
    // zero has to read back as exactly zero from the GPU too -- the bloom counterpart of the
    // histogram test's exact CPU match, proving "below-threshold contributes nothing" over the
    // whole image rather than a hand-picked region.
    bool sawAZeroTexel = false;
    for (uint32_t y = 0; y < kMip0Size; ++y) {
        for (uint32_t x = 0; x < kMip0Size; ++x) {
            const glm::vec3 expected = expectedBlur.load(x, y);
            const glm::vec3 actual = texelAt(x, y);
            INFO("full-chain texel (" << x << "," << y << ")");
            for (int channel = 0; channel < 3; ++channel) {
                REQUIRE(actual[channel] ==
                        Catch::Approx(expected[channel]).epsilon(0.01).margin(1e-6));
            }
            if (expected.r != 0.0f || expected.g != 0.0f || expected.b != 0.0f) {
                continue;
            }
            sawAZeroTexel = true;
            const glm::vec3 gpu = texelAt(x, y);
            INFO("below-threshold-only texel (" << x << "," << y << ")");
            REQUIRE(gpu.r == 0.0f);
            REQUIRE(gpu.g == 0.0f);
            REQUIRE(gpu.b == 0.0f);
        }
    }
    // The filtered corner probe still leaves zero-valued texels at the opposite edges, so the
    // exact-zero check must exercise some pixels as well as the positive full-chain comparisons.
    REQUIRE(sawAZeroTexel);

    // The probe: GPU result within 1% of the CPU chain's own prediction.
    const glm::vec3 gpuProbe = texelAt(0, 0);
    const glm::vec3 expectedProbe = expectedBlur.load(0, 0);
    INFO("expected " << expectedProbe.r << ", got " << gpuProbe.r);
    REQUIRE(expectedProbe.r > 0.0f); // the oracle itself has to be measuring something
    REQUIRE(gpuProbe.r == Catch::Approx(expectedProbe.r).epsilon(0.01));
}

//======================================================================================================================
// A separable colour ramp has hand-derived filtered values: 2 -> 4 maps to {0, 1/4, 3/4, 1},
// while 2 -> 5 maps to {0, 1/10, 1/2, 9/10, 1}. Nearest-neighbour reconstruction cannot pass
// the interior probes; the constant base also proves accumulation retains the larger mip.
TEST_CASE("bloom upsample reconstructs ramps across even odd and single-axis extents", "[gpu]") {
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto library = (*device)->loadShaderLibrary("Shaders/BloomUpsample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeBloomUpsample",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.filteredBloomPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    struct Probe {
        uint32_t smallWidth, smallHeight, dstWidth, dstHeight;
        std::array<float, 5> x, y;
    };
    const std::array<Probe, 3> probes{{
        {2, 2, 4, 4, {0.0f, 0.25f, 0.75f, 1.0f}, {0.0f, 0.25f, 0.75f, 1.0f}},
        {2, 2, 5, 3, {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}, {0.0f, 0.5f, 1.0f}},
        {1, 2, 3, 5, {0.0f, 0.0f, 0.0f}, {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}},
    }};
    for (const Probe& probe : probes) {
        DYNAMIC_SECTION(probe.smallWidth << "x" << probe.smallHeight << " -> " << probe.dstWidth
                                         << "x" << probe.dstHeight) {
            std::vector<uint16_t> smallPixels(size_t{probe.smallWidth} * probe.smallHeight * 4);
            for (uint32_t y = 0; y < probe.smallHeight; ++y) {
                for (uint32_t x = 0; x < probe.smallWidth; ++x) {
                    const size_t offset = (size_t{y} * probe.smallWidth + x) * 4;
                    smallPixels[offset] = x == 0 ? 0 : halfPow2(-2);
                    smallPixels[offset + 1] = y == 0 ? 0 : halfPow2(-1);
                    smallPixels[offset + 2] = halfPow2(-2);
                    smallPixels[offset + 3] = halfPow2(0);
                }
            }
            const TextureMip smallMip{.data = smallPixels.data(),
                                      .bytesPerRow = uint64_t{probe.smallWidth} * 8};
            auto small = (*device)->createTexture({.width = probe.smallWidth,
                                                   .height = probe.smallHeight,
                                                   .format = Format::RGBA16Float,
                                                   .storageRead = true,
                                                   .label = "lmx.test.filteredBloomSmall"},
                                                  std::span{&smallMip, 1});
            INFO(errorOf(small));
            REQUIRE(small.has_value());
            std::vector<uint16_t> basePixels(size_t{probe.dstWidth} * probe.dstHeight * 4,
                                             halfPow2(-3));
            const TextureMip baseMip{.data = basePixels.data(),
                                     .bytesPerRow = uint64_t{probe.dstWidth} * 8};
            auto base = (*device)->createTexture({.width = probe.dstWidth,
                                                  .height = probe.dstHeight,
                                                  .format = Format::RGBA16Float,
                                                  .storageRead = true,
                                                  .label = "lmx.test.filteredBloomBase"},
                                                 std::span{&baseMip, 1});
            INFO(errorOf(base));
            REQUIRE(base.has_value());
            auto dst = (*device)->createTexture({.width = probe.dstWidth,
                                                 .height = probe.dstHeight,
                                                 .format = Format::RGBA16Float,
                                                 .storageWrite = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.filteredBloomDst"});
            INFO(errorOf(dst));
            REQUIRE(dst.has_value());
            const std::array<uint32_t, 4> params{probe.smallWidth, probe.smallHeight,
                                                 probe.dstWidth, probe.dstHeight};
            CommandList& commands = (*device)->beginFrame();
            commands.beginComputePass("lmx.test.filteredBloomUpsample");
            commands.bindComputePipeline(**pipeline);
            commands.bindStorageTexture(0, **base, {}, StorageAccess::Read);
            commands.bindStorageTexture(1, **small, {}, StorageAccess::Read);
            commands.bindStorageTexture(2, **dst, {}, StorageAccess::Write);
            commands.bindFrameData(0, params);
            commands.dispatch(1, 1, 1);
            commands.endComputePass();
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            std::vector<uint16_t> result(basePixels.size());
            (*dst)->readback(result.data(), result.size() * sizeof(uint16_t));
            for (uint32_t y = 0; y < probe.dstHeight; ++y) {
                for (uint32_t x = 0; x < probe.dstWidth; ++x) {
                    INFO("texel " << x << "," << y);
                    const size_t offset = (size_t{y} * probe.dstWidth + x) * 4;
                    REQUIRE(glm::unpackHalf1x16(result[offset]) ==
                            Catch::Approx(0.125f + 0.25f * probe.x[x]).margin(0.0005));
                    REQUIRE(glm::unpackHalf1x16(result[offset + 1]) ==
                            Catch::Approx(0.125f + 0.5f * probe.y[y]).margin(0.0005));
                    REQUIRE(glm::unpackHalf1x16(result[offset + 2]) == 0.375f);
                }
            }
        }
    }
}
