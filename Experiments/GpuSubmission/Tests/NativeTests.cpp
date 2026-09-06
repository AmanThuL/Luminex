//----------------------------------------------------------------------------------------------------------------------
/// @file NativeTests.cpp
/// @brief Validates native visibility, image parity, and retired argument/slot reuse.
//----------------------------------------------------------------------------------------------------------------------
#include "Metal/Host.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#ifndef LMX_SUBMISSION_SHADER_DIR
#error "Native tests require the experiment shader directory"
#endif

namespace {

using namespace lmx::experimental::submission;
constexpr std::array kModes{Variant::Direct, Variant::CpuIndirect, Variant::GpuArgs,
                            Variant::Batched};

//======================================================================================================================
RunConfig verification() {
    RunConfig config;
    config.verify = true;
    config.shaderDirectory = LMX_SUBMISSION_SHADER_DIR;
    return config;
}

//======================================================================================================================
FrameImage nativeImage(const Case& spec, Suite suite, Variant mode, uint32_t frame) {
    auto image = renderNativeFrame(spec, suite, mode, frame, verification());
    INFO((image ? "native frame retired" : image.error()));
    REQUIRE(image.has_value());
    const auto input = makeFrame(spec, frame);
    REQUIRE(input.has_value());
    // GpuArgs returns IDs decoded from retired GPU-written argument records, not cached IDs.
    REQUIRE(image->visibleIds == classify(*input));
    REQUIRE(image->rgba.size() == size_t{kExtent} * kExtent * 4);
    return std::move(*image);
}

//======================================================================================================================
void parity(const FrameImage& expected, const FrameImage& actual) {
    REQUIRE(actual.visibleIds == expected.visibleIds);
    REQUIRE(actual.rgba.size() == expected.rgba.size());
    uint32_t maximumError = 0;
    size_t coverageMismatch = 0, alphaMismatch = 0;
    for (size_t offset = 0; offset < actual.rgba.size(); offset += 4) {
        const bool a = actual.rgba[offset] || actual.rgba[offset + 1] || actual.rgba[offset + 2];
        const bool b =
            expected.rgba[offset] || expected.rgba[offset + 1] || expected.rgba[offset + 2];
        coverageMismatch += a != b;
        alphaMismatch += actual.rgba[offset + 3] != 255;
        for (size_t channel = 0; channel < 4; ++channel) {
            maximumError =
                std::max(maximumError,
                         static_cast<uint32_t>(std::abs(int(actual.rgba[offset + channel]) -
                                                        int(expected.rgba[offset + channel]))));
        }
    }
    REQUIRE(coverageMismatch == 0);
    REQUIRE(alphaMismatch == 0);
    REQUIRE(maximumError <= 1);
}

//======================================================================================================================
void validateRun(const Case& spec, Suite suite, Variant mode, uint32_t frames) {
    auto config = verification();
    config.frames = frames;
    const auto run = runNative(spec, suite, mode, Lane::Headline, config);
    INFO((run ? "native replay retired and verified" : run.error()));
    REQUIRE(run.has_value());
    REQUIRE(run->verified);
    REQUIRE(run->samples.size() == frames);
    REQUIRE(run->requestedBytes <= 256ULL * 1024 * 1024);
    REQUIRE(run->allocatedBytes > 0);
    REQUIRE(std::isfinite(run->throughput));
    REQUIRE(run->throughput > 0);
    REQUIRE(run->setupMs > 0);
    REQUIRE(run->drainMs >= 0);
    REQUIRE(run->gpuSpanStatus.starts_with("unavailable"));
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const auto& sample = run->samples[frame];
        const auto expected = makeFrame(spec, frame);
        REQUIRE(expected.has_value());
        REQUIRE(sample.frame == frame);
        REQUIRE(sample.cpuWorkNs > 0);
        REQUIRE_FALSE(sample.gpuSpanMs.has_value());
        REQUIRE_FALSE(sample.preparationMs.has_value());
        REQUIRE_FALSE(sample.rasterMs.has_value());
        uint64_t draws = expected->visibleIds.size();
        if (mode == Variant::GpuArgs) {
            draws = spec.count;
        } else if (mode == Variant::Batched) {
            draws = 0;
            for (uint32_t bin = 0; bin < spec.bins; ++bin) {
                draws += expected->binOffsets[bin] != expected->binOffsets[bin + 1];
            }
        }
        REQUIRE(sample.drawCalls == draws);
    }
}

} // namespace

//======================================================================================================================
TEST_CASE("submission native reports unavailable routes without fabricating timings",
          "[unit][submission][native]") {
    auto config = verification();
    config.frames = 1;
    for (auto lane : {Lane::GpuSpan, Lane::Stages}) {
        auto run = runNative({}, Suite::E, Variant::GpuArgs, lane, config);
        REQUIRE_FALSE(run.has_value());
        REQUIRE(run.error().starts_with("unavailable"));
    }
    const Case empty{.id = "unavailable-icb", .count = 0, .visibleFraction = 0};
    const auto icb = renderNativeFrame(empty, Suite::E, Variant::GpuIcb, 0, config);
    REQUIRE_FALSE(icb.has_value());
    REQUIRE(icb.error().find("ICB") != std::string::npos);
    config.frames = 0;
    REQUIRE_FALSE(runNative({}, Suite::S, Variant::Direct, Lane::Headline, config).has_value());
    const auto capabilities = capabilitiesJson();
    REQUIRE(capabilities.find("\"gpuSpan\":\"unavailable\"") != std::string::npos);
    REQUIRE(capabilities.find("Probe.slang") != std::string::npos);
    REQUIRE(capabilities.find("\"residentBytes\":null") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("submission diagnostic dependencies cannot enter verification or measurement",
          "[unit][submission][native]") {
    RunConfig config;
    config.diagnosticAllStages = true;
    auto run = runNative({}, Suite::S, Variant::GpuArgs, Lane::Headline, config);
    REQUIRE_FALSE(run.has_value());
    REQUIRE(run.error().find("requires diagnostics") != std::string::npos);
    config.diagnostics = true;
    run = runNative({}, Suite::S, Variant::Direct, Lane::Headline, config);
    REQUIRE_FALSE(run.has_value());
    REQUIRE(run.error().find("gpu-args") != std::string::npos);
    for (bool allStages : {false, true}) {
        config.diagnostics = !allStages;
        config.diagnosticAllStages = allStages;
        auto image = renderNativeFrame({}, Suite::S, Variant::GpuArgs, 0, config);
        REQUIRE_FALSE(image.has_value());
        REQUIRE(image.error().find("without verification") != std::string::npos);
    }
}

//======================================================================================================================
TEST_CASE("submission native scored artifacts preserve empty sparse dense and tail images",
          "[gpu][submission][native]") {
    const std::array cases{
        Case{.id = "empty", .count = 0, .bins = 64, .visibleFraction = 0},
        Case{.id = "one", .count = 1, .visibleFraction = 1},
        Case{.id = "invisible-tail", .count = 257, .bins = 64, .visibleFraction = 0},
        Case{.id = "sparse-tail", .count = 257, .bins = 64, .visibleFraction = 0.1},
        Case{.id = "dense-tail", .count = 257, .bins = 64, .visibleFraction = 1},
        Case{.id = "tessellated-tail",
             .count = 257,
             .triangles = 32,
             .bins = 16,
             .visibleFraction = 0.5}};
    for (const auto& spec : cases) {
        for (auto suite : {Suite::S, Suite::E}) {
            CAPTURE(spec.id, suite);
            const auto direct = nativeImage(spec, suite, Variant::Direct, 32);
            size_t covered = 0;
            for (size_t pixel = 0; pixel < direct.rgba.size(); pixel += 4) {
                covered += direct.rgba[pixel] || direct.rgba[pixel + 1] || direct.rgba[pixel + 2];
            }
            REQUIRE((covered == 0) == (spec.visibleFraction == 0));
            for (auto mode : kModes) {
                CAPTURE(mode);
                parity(direct, nativeImage(spec, suite, mode, 32));
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("submission native verifies complete scored replay in every supported route",
          "[gpu][submission][native][replay]") {
    const Case spec{
        .id = "native-replay", .count = 257, .triangles = 32, .bins = 64, .visibleFraction = 0.5};
    for (auto suite : {Suite::S, Suite::E}) {
        for (auto mode : kModes) {
            CAPTURE(suite, mode);
            validateRun(spec, suite, mode, kReplayFrames);
        }
    }
}

//======================================================================================================================
TEST_CASE("submission native stresses changing visibility and slot canaries for 900 frames",
          "[gpu][submission][native][stress]") {
    const Case spec{.id = "native-stress", .count = 257, .bins = 64, .visibleFraction = 0.5};
    for (auto mode : kModes) {
        CAPTURE(mode);
        validateRun(spec, Suite::E, mode, 900);
    }
    validateRun(spec, Suite::S, Variant::GpuArgs, 900);
}
