//----------------------------------------------------------------------------------------------------------------------
/// @file GpuExposureBloomTests.cpp
/// @brief GPU conformance tests for the histogram exposure and bloom compute kernels (spec 9/10),
///        run directly against CPU reference implementations that mirror the shaders line for
///        line -- Shaders/HistogramAccumulate.slang, Shaders/ExposureResolve.slang, and
///        Shaders/BloomThreshold.slang -- rather than through Renderer::declarePasses(), so a
///        failure here points at one kernel's math instead of the whole frame's wiring.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace lmx::rhi;

template <typename T>
std::string errorOf(const Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Mirrors HistogramAccumulate.slang's kHistogramBins/kLuminanceWeights and its own and
// ExposureResolve.slang's shared binning range -- Renderer.cpp's kExposureLogLuminanceMin/Max.
constexpr uint32_t kBins = 256;
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};
constexpr float kLogLuminanceMin = -12.0f;
constexpr float kLogLuminanceMax = 4.0f;

// A half-float bit pattern for 2^exponent, exact because every power of two in the normal range
// has a zero mantissa -- so uploads need no float-to-half rounding logic to reason about.
constexpr uint16_t halfPow2(int exponent) {
    return static_cast<uint16_t>((exponent + 15) << 10);
}

// Mirrors HistogramAccumulate.slang's computeHistogramAccumulate, texel for texel.
std::array<uint32_t, kBins> cpuHistogram(std::span<const glm::vec3> pixels, float preExposure) {
    std::array<uint32_t, kBins> bins{};
    const float range = std::max(kLogLuminanceMax - kLogLuminanceMin, 1e-6f);
    for (const glm::vec3& color : pixels) {
        const float displayedLuminance = glm::dot(color, kLuminanceWeights);
        const float sceneLuminance = displayedLuminance / std::max(preExposure, 1e-6f);
        const float logLuminance = std::log2(std::max(sceneLuminance, 1e-6f));
        const float t = std::clamp((logLuminance - kLogLuminanceMin) / range, 0.0f, 1.0f);
        const uint32_t bin =
            std::min(static_cast<uint32_t>(t * static_cast<float>(kBins)), kBins - 1u);
        ++bins[bin];
    }
    return bins;
}

// Mirrors ExposureResolve.slang's computeExposureResolve, statement for statement.
float cpuResolveExposure(const std::array<uint32_t, kBins>& bins, float lowPercentile,
                         float highPercentile, float targetGrey, float evMin, float evMax,
                         float compensationEv) {
    uint64_t totalCount = 0;
    for (uint32_t count : bins) {
        totalCount += count;
    }
    if (totalCount == 0) {
        return 1.0f;
    }

    const float lowCount = lowPercentile * 0.01f * static_cast<float>(totalCount);
    const float highCount = highPercentile * 0.01f * static_cast<float>(totalCount);
    const float binWidth = (kLogLuminanceMax - kLogLuminanceMin) / static_cast<float>(kBins);

    float weightedLogLuminance = 0.0f;
    float weight = 0.0f;
    float cumulative = 0.0f;
    for (uint32_t i = 0; i < kBins; ++i) {
        const float count = static_cast<float>(bins[i]);
        const float binStart = cumulative;
        const float binEnd = cumulative + count;
        const float overlap =
            std::max(std::min(binEnd, highCount) - std::max(binStart, lowCount), 0.0f);
        const float binCenterLogLuminance =
            kLogLuminanceMin + (static_cast<float>(i) + 0.5f) * binWidth;
        weightedLogLuminance += overlap * binCenterLogLuminance;
        weight += overlap;
        cumulative = binEnd;
    }

    const float averageLogLuminance = weight > 0.0f ? weightedLogLuminance / weight : 0.0f;
    const float averageLuminance = std::exp2(averageLogLuminance);
    float exposure = (targetGrey / std::max(averageLuminance, 1e-6f)) * std::exp2(compensationEv);
    exposure = std::clamp(exposure, std::exp2(evMin), std::exp2(evMax));
    return exposure;
}

// Mirrors BloomThreshold.slang's per-texel formula (the 2x2 box read is the caller's job below,
// since it is identical for every probe pixel this file constructs).
glm::vec3 cpuThreshold(glm::vec3 color, float threshold) {
    const float luminance = glm::dot(color, kLuminanceWeights);
    const float contribution = std::max(luminance - threshold, 0.0f) / std::max(luminance, 1e-4f);
    return color * contribution;
}

// A tiny CPU grid with BloomDownsample.slang/BloomUpsample.slang's clamped-coordinate box filters,
// used to build the full-chain reference the bloom energy oracle compares the GPU chain against.
struct Grid {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<glm::vec3> texels;

    Grid(uint32_t w, uint32_t h) : width(w), height(h), texels(size_t{w} * h, glm::vec3(0.0f)) {}
    glm::vec3& at(uint32_t x, uint32_t y) { return texels[size_t{y} * width + x]; }
    glm::vec3 load(uint32_t x, uint32_t y) const {
        const uint32_t cx = std::min(x, width - 1);
        const uint32_t cy = std::min(y, height - 1);
        return texels[size_t{cy} * width + cx];
    }
};

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

// Mirrors BloomUpsample.slang.
Grid cpuUpsampleAccumulate(const Grid& base, const Grid& small) {
    Grid dst(base.width, base.height);
    for (uint32_t y = 0; y < base.height; ++y) {
        for (uint32_t x = 0; x < base.width; ++x) {
            const uint32_t sx = std::min(x / 2, small.width - 1);
            const uint32_t sy = std::min(y / 2, small.height - 1);
            dst.at(x, y) = base.load(x, y) + small.load(sx, sy);
        }
    }
    return dst;
}

Result<std::unique_ptr<Texture>> makeSceneColorTexture(Device& device, uint32_t width,
                                                       uint32_t height,
                                                       std::span<const uint16_t> rgbaHalf,
                                                       const char* label) {
    const TextureMip mip{.data = rgbaHalf.data(),
                         .bytesPerRow = uint64_t{width} * 4 * sizeof(uint16_t)};
    return device.createTexture({.width = width,
                                 .height = height,
                                 .format = Format::RGBA16Float,
                                 .sampled = true,
                                 .label = label},
                                std::span{&mip, 1});
}

} // namespace

//======================================================================================================================
// A known 4x4 image (13 texels at luminance 1, one each at 2, 4, and 0.5 -- all exact powers of
// two, so both the display->scene-referred division and the log2 binning are exact) run through
// the real compute kernel and compared bin-for-bin against the CPU port above.
TEST_CASE("the GPU histogram matches a CPU reference on a known image", "[gpu]") {
    constexpr uint32_t kWidth = 4, kHeight = 4;
    constexpr float kPreExposure = 1.0f;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    std::vector<glm::vec3> pixels(size_t{kWidth} * kHeight,
                                  glm::vec3(1.0f)); // luminance 1 (bin 192)
    pixels[0] = glm::vec3(2.0f);                    // luminance 2 (bin 208)
    pixels[1] = glm::vec3(4.0f);                    // luminance 4 (bin 224)
    pixels[2] = glm::vec3(0.5f);                    // luminance 0.5 (bin 176)

    std::vector<uint16_t> rgbaHalf(pixels.size() * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const int exponent = static_cast<int>(std::round(std::log2(pixels[i].r)));
        const uint16_t channel = halfPow2(exponent);
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0); // alpha 1.0
    }

    auto sceneColor =
        makeSceneColorTexture(**device, kWidth, kHeight, rgbaHalf, "lmx.test.histogramImage");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/HistogramAccumulate");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeHistogramAccumulate",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.histogramPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto histogram = (*device)->createBuffer({.size = kBins * sizeof(uint32_t),
                                              .storageRead = true,
                                              .storageWrite = true,
                                              .cpuReadback = true,
                                              .label = "lmx.test.histogramBuffer"},
                                             nullptr);
    INFO(errorOf(histogram));
    REQUIRE(histogram.has_value());

    struct HistogramParams {
        float preExposure;
        float logLuminanceMin;
        float logLuminanceMax;
        uint32_t width;
        uint32_t height;
    };
    const HistogramParams params{.preExposure = kPreExposure,
                                 .logLuminanceMin = kLogLuminanceMin,
                                 .logLuminanceMax = kLogLuminanceMax,
                                 .width = kWidth,
                                 .height = kHeight};

    CommandList& commands = (*device)->beginFrame();
    commands.beginCopyPass("lmx.test.clearHistogram");
    commands.fillBuffer(**histogram, 0, kBins * sizeof(uint32_t), 0);
    commands.endCopyPass();
    commands.bufferBarrier(**histogram, BufferUse::CopyDestination, BufferUse::StorageWrite);

    commands.beginComputePass("lmx.test.histogram");
    commands.bindComputePipeline(**pipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::ReadWrite);
    commands.setUniforms(1, &params, sizeof(params));
    commands.dispatch(1, 1, 1); // 4x4 fits one 8x8 threadgroup; the kernel bounds-checks the rest.
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::array<uint32_t, kBins> gpuBins{};
    (*histogram)->readback(gpuBins.data(), gpuBins.size() * sizeof(uint32_t));

    const std::array<uint32_t, kBins> expected = cpuHistogram(pixels, kPreExposure);
    for (uint32_t bin = 0; bin < kBins; ++bin) {
        if (gpuBins[bin] != 0 || expected[bin] != 0) {
            INFO("bin " << bin);
            REQUIRE(gpuBins[bin] == expected[bin]);
        }
    }
    REQUIRE(expected[192] == 13);
    REQUIRE(expected[208] == 1);
    REQUIRE(expected[224] == 1);
    REQUIRE(expected[176] == 1);
}

//======================================================================================================================
// A uniform image at a known scene-referred luminance, metered wide open (0th/100th percentile,
// so every pixel's one bin is kept whole), resolved in one dispatch and compared against the CPU
// port of the exact same binning + weighted-average formula -- the one-frame convergence spec 9
// requires: at a known manual EV (folded into kPreExposure here, since the resolve pass never
// reads EV directly) and known content, the target exposure equals the CPU prediction after
// exactly one frame's histogram + resolve.
TEST_CASE("auto exposure converges to the CPU-predicted target after one frame", "[gpu]") {
    constexpr uint32_t kWidth = 8, kHeight = 8;
    constexpr float kPreExposure = 1.0f;    // exp2(manual EV 0)
    constexpr float kSceneLuminance = 2.0f; // every pixel
    constexpr float kTargetGrey = 0.18f;
    constexpr float kEvMin = -100.0f, kEvMax = 100.0f; // wide enough the clamp never engages
    constexpr float kCompensationEv = 0.0f;
    constexpr float kLowPercentile = 0.0f, kHighPercentile = 100.0f;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    std::vector<uint16_t> rgbaHalf(size_t{kWidth} * kHeight * 4);
    const uint16_t channel = halfPow2(1); // 2.0
    for (size_t i = 0; i < size_t{kWidth} * kHeight; ++i) {
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0);
    }
    auto sceneColor =
        makeSceneColorTexture(**device, kWidth, kHeight, rgbaHalf, "lmx.test.convergenceImage");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto histogramLibrary = (*device)->loadShaderLibrary("Shaders/HistogramAccumulate");
    INFO(errorOf(histogramLibrary));
    REQUIRE(histogramLibrary.has_value());
    auto histogramPipeline =
        (*device)->createComputePipeline({.library = histogramLibrary->get(),
                                          .computeEntry = "computeHistogramAccumulate",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.convergenceHistogramPipeline"});
    INFO(errorOf(histogramPipeline));
    REQUIRE(histogramPipeline.has_value());

    auto resolveLibrary = (*device)->loadShaderLibrary("Shaders/ExposureResolve");
    INFO(errorOf(resolveLibrary));
    REQUIRE(resolveLibrary.has_value());
    auto resolvePipeline =
        (*device)->createComputePipeline({.library = resolveLibrary->get(),
                                          .computeEntry = "computeExposureResolve",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "lmx.test.resolvePipeline"});
    INFO(errorOf(resolvePipeline));
    REQUIRE(resolvePipeline.has_value());

    auto histogram = (*device)->createBuffer({.size = kBins * sizeof(uint32_t),
                                              .storageRead = true,
                                              .storageWrite = true,
                                              .label = "lmx.test.convergenceHistogramBuffer"},
                                             nullptr);
    INFO(errorOf(histogram));
    REQUIRE(histogram.has_value());
    auto exposure = (*device)->createBuffer({.size = sizeof(float),
                                             .storageWrite = true,
                                             .cpuReadback = true,
                                             .label = "lmx.test.convergenceExposureBuffer"},
                                            nullptr);
    INFO(errorOf(exposure));
    REQUIRE(exposure.has_value());

    struct HistogramParams {
        float preExposure;
        float logLuminanceMin;
        float logLuminanceMax;
        uint32_t width;
        uint32_t height;
    };
    struct ExposureResolveParams {
        float lowPercentile, highPercentile, targetGrey, evMin, evMax, compensationEv;
        float logLuminanceMin, logLuminanceMax;
    };
    const HistogramParams histogramParams{.preExposure = kPreExposure,
                                          .logLuminanceMin = kLogLuminanceMin,
                                          .logLuminanceMax = kLogLuminanceMax,
                                          .width = kWidth,
                                          .height = kHeight};
    const ExposureResolveParams resolveParams{.lowPercentile = kLowPercentile,
                                              .highPercentile = kHighPercentile,
                                              .targetGrey = kTargetGrey,
                                              .evMin = kEvMin,
                                              .evMax = kEvMax,
                                              .compensationEv = kCompensationEv,
                                              .logLuminanceMin = kLogLuminanceMin,
                                              .logLuminanceMax = kLogLuminanceMax};

    CommandList& commands = (*device)->beginFrame();
    commands.beginCopyPass("lmx.test.convergenceClear");
    commands.fillBuffer(**histogram, 0, kBins * sizeof(uint32_t), 0);
    commands.endCopyPass();
    commands.bufferBarrier(**histogram, BufferUse::CopyDestination, BufferUse::StorageWrite);

    commands.beginComputePass("lmx.test.convergenceHistogram");
    commands.bindComputePipeline(**histogramPipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::ReadWrite);
    commands.setUniforms(1, &histogramParams, sizeof(histogramParams));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();

    commands.bufferBarrier(**histogram, BufferUse::StorageWrite, BufferUse::StorageRead);

    commands.beginComputePass("lmx.test.convergenceResolve");
    commands.bindComputePipeline(**resolvePipeline);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::Read);
    commands.bindStorageBuffer(1, **exposure, StorageAccess::Write);
    commands.setUniforms(2, &resolveParams, sizeof(resolveParams));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    float gpuExposure = 0.0f;
    (*exposure)->readback(&gpuExposure, sizeof(gpuExposure));

    const std::array<uint32_t, kBins> bins = cpuHistogram(
        std::vector<glm::vec3>(size_t{kWidth} * kHeight, glm::vec3(kSceneLuminance)), kPreExposure);
    const float expected = cpuResolveExposure(bins, kLowPercentile, kHighPercentile, kTargetGrey,
                                              kEvMin, kEvMax, kCompensationEv);

    REQUIRE(gpuExposure == Catch::Approx(expected).epsilon(1e-5));
    // The formula's own sanity: a uniform image at luminance 2 metered toward 0.18 grey wants an
    // exposure a little under 0.09 -- the binning's ~0.06-stop quantization is why "a little
    // under" rather than exact.
    REQUIRE(gpuExposure == Catch::Approx(0.09f).epsilon(0.05));
}

//======================================================================================================================
// Below-threshold content contributes nothing (an exact CPU match, since the formula has no
// binning quantization to blur the comparison), and a bright probe's energy after the full
// threshold -> downsample -> upsample-accumulate chain matches a CPU port of the same three
// kernels within a stated tolerance -- spec 10's bloom energy oracle.
TEST_CASE("bloom threshold and the full chain match a CPU reference", "[gpu]") {
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

    // 8x8 scene color -> 4x4 bloomChain mip0 -> 2x2 mip1 -> 4x4 bloomBlur mip0. One bright probe
    // texel (16, well above threshold), a uniform background at luminance 1 (well below it).
    constexpr uint32_t kSceneSize = 8, kMip0Size = 4, kMip1Size = 2;
    constexpr float kThreshold = 4.0f;
    constexpr uint32_t kProbeX = 0, kProbeY = 0; // scene texel; lands at bloom mip0 texel (0, 0)

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
                                                .mipLevels = 2,
                                                .storageRead = true,
                                                .storageWrite = true,
                                                .label = "lmx.test.bloomChain"});
    INFO(errorOf(bloomChain));
    REQUIRE(bloomChain.has_value());
    auto bloomBlur = (*device)->createTexture({.width = kMip0Size,
                                               .height = kMip0Size,
                                               .format = Format::RGBA16Float,
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
    const ThresholdParams thresholdParams{.threshold = kThreshold,
                                          .srcWidth = kSceneSize,
                                          .srcHeight = kSceneSize,
                                          .dstWidth = kMip0Size,
                                          .dstHeight = kMip0Size};
    const DownsampleParams downsampleParams{.srcWidth = kMip0Size,
                                            .srcHeight = kMip0Size,
                                            .dstWidth = kMip1Size,
                                            .dstHeight = kMip1Size};
    const UpsampleParams upsampleParams{.smallWidth = kMip1Size,
                                        .smallHeight = kMip1Size,
                                        .dstWidth = kMip0Size,
                                        .dstHeight = kMip0Size};
    const TextureViewDesc mip0View{.range = {.baseMipLevel = 0, .mipLevelCount = 1}};
    const TextureViewDesc mip1View{.range = {.baseMipLevel = 1, .mipLevelCount = 1}};

    CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.bloomThreshold");
    commands.bindComputePipeline(**thresholdPipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageTexture(1, **bloomChain, mip0View, StorageAccess::Write);
    commands.setUniforms(0, &thresholdParams, sizeof(thresholdParams));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();

    commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);

    commands.beginComputePass("lmx.test.bloomDownsample");
    commands.bindComputePipeline(**downsamplePipeline);
    commands.bindStorageTexture(0, **bloomChain, mip0View, StorageAccess::Read);
    commands.bindStorageTexture(1, **bloomChain, mip1View, StorageAccess::Write);
    commands.setUniforms(0, &downsampleParams, sizeof(downsampleParams));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();

    commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);

    commands.beginComputePass("lmx.test.bloomUpsample");
    commands.bindComputePipeline(**upsamplePipeline);
    commands.bindStorageTexture(0, **bloomChain, mip0View, StorageAccess::Read);
    commands.bindStorageTexture(1, **bloomChain, mip1View, StorageAccess::Read);
    commands.bindStorageTexture(2, **bloomBlur, TextureViewDesc{}, StorageAccess::Write);
    commands.setUniforms(0, &upsampleParams, sizeof(upsampleParams));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> blurHalf(size_t{kMip0Size} * kMip0Size * 4);
    (*bloomBlur)->readback(blurHalf.data(), blurHalf.size() * sizeof(uint16_t));

    // Half -> float via the standard library's own round trip: only the four values this test
    // ever produces (0, 1, 3, 15, and sums thereof) are read back, all exactly representable.
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

    // CPU reference: the same three kernels, run on the same scene pixels.
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
    const Grid mip1 = cpuDownsample(mip0, kMip1Size, kMip1Size);
    const Grid expectedBlur = cpuUpsampleAccumulate(mip0, mip1);

    // Below-threshold background: the box filters give the probe's energy a 2x2-texel footprint
    // (its own mip0 quadrant, x < 2 && y < 2), so every mip0 texel *outside* that footprint is
    // built entirely from below-threshold source pixels through every stage of the chain -- the
    // bloom counterpart of the histogram test's exact CPU match, proving "below-threshold
    // contributes nothing" rather than merely "the probe contributes something".
    for (uint32_t y = 0; y < kMip0Size; ++y) {
        for (uint32_t x = 0; x < kMip0Size; ++x) {
            if (x < 2 && y < 2) {
                continue; // the probe's own footprint; checked separately below
            }
            const glm::vec3 gpu = texelAt(x, y);
            INFO("below-threshold-only texel (" << x << "," << y << ")");
            REQUIRE(gpu.r == 0.0f);
            REQUIRE(gpu.g == 0.0f);
            REQUIRE(gpu.b == 0.0f);
        }
    }

    // The probe: GPU result within 1% of the CPU chain's own prediction.
    const glm::vec3 gpuProbe = texelAt(0, 0);
    const glm::vec3 expectedProbe = expectedBlur.load(0, 0);
    INFO("expected " << expectedProbe.r << ", got " << gpuProbe.r);
    REQUIRE(expectedProbe.r > 0.0f); // the oracle itself has to be measuring something
    REQUIRE(gpuProbe.r == Catch::Approx(expectedProbe.r).epsilon(0.01));
}
