//----------------------------------------------------------------------------------------------------------------------
/// @file ReferenceTests.cpp
/// @brief Exercises retired reference images, visibility, and compute-to-indirect graph hazards.
//----------------------------------------------------------------------------------------------------------------------
#include "Reference/RhiReference.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#ifndef LMX_SUBMISSION_SHADER_DIR
#error "Define LMX_SUBMISSION_SHADER_DIR to the test executable's experiment shader directory"
#endif

namespace {

using namespace lmx::experimental::submission;

constexpr std::array kOrdinaryVariants{Variant::Direct, Variant::CpuIndirect, Variant::GpuArgs,
                                       Variant::Batched};

//======================================================================================================================
FrameImage render(const Case& spec, Suite suite, Variant variant, uint32_t logicalFrame) {
    auto result = renderReferenceFrame(spec, suite, variant, logicalFrame,
                                       std::filesystem::path{LMX_SUBMISSION_SHADER_DIR});
    INFO((result ? "Reference frame completed" : result.error()));
    REQUIRE(result.has_value());
    REQUIRE(result->rgba.size() == size_t{kExtent} * kExtent * 4);
    return std::move(*result);
}

//======================================================================================================================
void requireVisibility(const FrameImage& image, const Case& spec, uint32_t logicalFrame) {
    auto input = makeFrame(spec, logicalFrame);
    REQUIRE(input.has_value());
    auto expected = classify(*input);
    auto actual = image.visibleIds;
    std::ranges::sort(expected);
    std::ranges::sort(actual);
    REQUIRE(actual == expected);
    REQUIRE(std::adjacent_find(actual.begin(), actual.end()) == actual.end());
    REQUIRE(actual.size() == static_cast<size_t>(std::floor(spec.count * spec.visibleFraction)));
}

//======================================================================================================================
void requireImageParity(const FrameImage& actual, const FrameImage& expected) {
    REQUIRE(actual.rgba.size() == expected.rgba.size());
    uint32_t maxError = 0;
    size_t coverageMismatches = 0;
    for (size_t offset = 0; offset < actual.rgba.size(); offset += 4) {
        const bool actualCovered =
            actual.rgba[offset] || actual.rgba[offset + 1] || actual.rgba[offset + 2];
        const bool expectedCovered =
            expected.rgba[offset] || expected.rgba[offset + 1] || expected.rgba[offset + 2];
        coverageMismatches += actualCovered != expectedCovered;
        for (size_t channel = 0; channel < 4; ++channel) {
            maxError = std::max(
                maxError, static_cast<uint32_t>(std::abs(int{actual.rgba[offset + channel]} -
                                                         int{expected.rgba[offset + channel]})));
        }
    }
    REQUIRE(coverageMismatches == 0);
    REQUIRE(maxError <= 1);
}

//======================================================================================================================
std::string resourceToken(const std::string& dump, std::string_view name) {
    const auto end = dump.find(" buffer \"" + std::string{name} + "\"");
    REQUIRE(end != std::string::npos);
    const auto start = dump.rfind('\n', end) + 3;
    return dump.substr(start, end - start);
}

//======================================================================================================================
void requireExecutedGraph(const FrameImage& image, Suite suite, Variant variant, uint32_t count) {
    const auto& dump = image.graphDump;
    const bool generated = variant == Variant::GpuArgs && count != 0;
    const bool indirect = variant != Variant::Direct;
    INFO(dump);
    const auto raster = dump.find("raster \"submission.reference.raster\"");
    const auto readback = dump.find("copy \"submission.reference.readback\"");
    REQUIRE(raster != std::string::npos);
    REQUIRE(readback != std::string::npos);
    REQUIRE(raster < readback);
    REQUIRE(dump.find("sinks\n  readback ") != std::string::npos);
    REQUIRE(dump.find("RenderTarget -> CopySource") != std::string::npos);
    REQUIRE(dump.find("culled\ntransitions\n") != std::string::npos);
    const auto args = resourceToken(dump, "arguments");
    const auto bitmap = resourceToken(dump, suite == Suite::S ? "visibility" : "visibilityDummy");
    const auto ids = resourceToken(dump, "visibleIds");
    const auto pixels = resourceToken(dump, "pixelReadback");
    const auto readArgs = resourceToken(dump, "argumentReadback");
    const std::string argumentVersion = generated ? " v1" : " v0";
    REQUIRE((dump.find("indirect argument " + args + argumentVersion) != std::string::npos) ==
            indirect);
    REQUIRE((dump.find("copy source " + args + argumentVersion) != std::string::npos) == indirect);
    REQUIRE((dump.find("shader read " + bitmap + " v0") != std::string::npos) == generated);
    REQUIRE(dump.find("    read " + ids + " v0") != std::string::npos);
    REQUIRE(dump.find("  readback " + pixels + " v1") != std::string::npos);
    REQUIRE((dump.find("  readback " + readArgs + " v1") != std::string::npos) == indirect);
    if (variant == Variant::Batched) {
        const auto readIds = resourceToken(dump, "idsReadback");
        REQUIRE(dump.find("copy source " + ids + " v0") != std::string::npos);
        REQUIRE(dump.find("  readback " + readIds + " v1") != std::string::npos);
    }
    if (generated) {
        const auto prepare = dump.find("compute \"submission.reference.prepare\"");
        REQUIRE(prepare != std::string::npos);
        REQUIRE(prepare < raster);
        REQUIRE(dump.find("StorageWrite -> IndirectArgument") != std::string::npos);
        REQUIRE(dump.find("StorageWrite -> CopySource") != std::string::npos);
    } else {
        REQUIRE(dump.find("compute \"submission.reference.prepare\"") == std::string::npos);
    }
}

//======================================================================================================================
void requireClearOrCovered(const FrameImage& image, bool empty) {
    size_t covered = 0;
    size_t invalidAlpha = 0;
    for (size_t offset = 0; offset < image.rgba.size(); offset += 4) {
        covered += image.rgba[offset] || image.rgba[offset + 1] || image.rgba[offset + 2];
        invalidAlpha += image.rgba[offset + 3] != 255;
    }
    REQUIRE(invalidAlpha == 0);
    if (empty) {
        REQUIRE(covered == 0);
    } else {
        REQUIRE(covered > 0);
    }
}

} // namespace

//======================================================================================================================
TEST_CASE("submission reference rejects unsupported routes before creating GPU work",
          "[unit][submission][reference]") {
    const auto icb = renderReferenceFrame({}, Suite::S, Variant::GpuIcb, 0, {});
    REQUIRE_FALSE(icb.has_value());
    REQUIRE(icb.error().find("ICB") != std::string::npos);

    const Case invalid{.id = "invalid", .count = 257, .triangles = 3};
    const auto rejected = renderReferenceFrame(invalid, Suite::S, Variant::Direct, 0, {});
    REQUIRE_FALSE(rejected.has_value());

    const auto badSuite = renderReferenceFrame({}, static_cast<Suite>(-1), Variant::Direct, 0, {});
    REQUIRE_FALSE(badSuite.has_value());
    REQUIRE(badSuite.error().find("suite") != std::string::npos);

    const auto badVariant = renderReferenceFrame({}, Suite::S, static_cast<Variant>(-1), 0, {});
    REQUIRE_FALSE(badVariant.has_value());
    REQUIRE(badVariant.error().find("variant") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("submission reference executes ordinary routes for empty and partial threadgroups",
          "[gpu][submission][reference]") {
    const std::array fixtures{
        Case{.id = "empty", .count = 0, .bins = 64, .visibleFraction = 0},
        Case{.id = "single", .count = 1, .visibleFraction = 1},
        Case{.id = "culled-tail", .count = 257, .bins = 64, .visibleFraction = 0},
        Case{.id = "partial-tail", .count = 257, .bins = 64, .visibleFraction = 0.5},
        Case{.id = "full-tail", .count = 257, .bins = 64, .visibleFraction = 1},
    };
    for (const auto& spec : fixtures) {
        for (const auto suite : {Suite::S, Suite::E}) {
            CAPTURE(spec.id, suite);
            const auto direct = render(spec, suite, Variant::Direct, 0);
            requireVisibility(direct, spec, 0);
            requireExecutedGraph(direct, suite, Variant::Direct, spec.count);
            requireClearOrCovered(direct, spec.visibleFraction == 0);
            for (const auto variant : {Variant::CpuIndirect, Variant::GpuArgs, Variant::Batched}) {
                CAPTURE(variant);
                const auto image = render(spec, suite, variant, 0);
                requireVisibility(image, spec, 0);
                requireImageParity(image, direct);
                requireExecutedGraph(image, suite, variant, spec.count);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("submission reference preserves rotated IDs and tessellated bin addressing",
          "[gpu][submission][reference]") {
    Case spec{
        .id = "rotated-tail", .count = 257, .triangles = 2, .bins = 64, .visibleFraction = 0.5};
    const auto initial = makeFrame(spec, 0);
    const auto rotated = makeFrame(spec, 32);
    REQUIRE(initial.has_value());
    REQUIRE(rotated.has_value());
    REQUIRE(classify(*initial) != classify(*rotated));

    for (uint32_t logicalFrame : {0u, 32u, 64u, 96u, 128u, 160u, 192u, 224u, 255u, 256u}) {
        CAPTURE(logicalFrame);
        spec.triangles = 2;
        const auto coarse = render(spec, Suite::S, Variant::Direct, logicalFrame);
        requireVisibility(coarse, spec, logicalFrame);
        spec.triangles = 32;
        for (auto suite : {Suite::S, Suite::E}) {
            for (auto variant : kOrdinaryVariants) {
                CAPTURE(suite, variant);
                const auto image = render(spec, suite, variant, logicalFrame);
                requireVisibility(image, spec, logicalFrame);
                requireImageParity(image, coarse);
                requireExecutedGraph(image, suite, variant, spec.count);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("submission reference anchors every matrix point in both suites",
          "[gpu][submission][reference][matrix]") {
    for (const auto& spec : caseMatrix()) {
        CAPTURE(spec.id);
        const auto direct = render(spec, Suite::S, Variant::Direct, 32);
        requireVisibility(direct, spec, 32);
        requireClearOrCovered(direct, false);
        for (const auto suite : {Suite::S, Suite::E}) {
            for (const auto variant : kOrdinaryVariants) {
                if (suite == Suite::S && variant == Variant::Direct) {
                    continue;
                }
                CAPTURE(suite, variant);
                const auto image = render(spec, suite, variant, 32);
                requireVisibility(image, spec, 32);
                requireImageParity(image, direct);
                requireExecutedGraph(image, suite, variant, spec.count);
            }
        }
    }
}
