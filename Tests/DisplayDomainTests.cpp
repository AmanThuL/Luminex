#include "Engine/PngImage.h"
#include "RHI/Validate.h"
#include "Render/DisplayDomain.h"
#include "Render/Renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("the SDR display domain has stable names and serialization", "[render][display]") {
    REQUIRE(name(kSdrDisplayDomain.view) == "sdr");
    REQUIRE(name(kSdrDisplayDomain.transfer) == "srgb");
    REQUIRE(name(kSdrDisplayDomain.primaries) == "bt709");
    REQUIRE(name(kSdrDisplayDomain.toneMap) == "pbr-neutral");
    REQUIRE(describe(kSdrDisplayDomain) ==
            "sdr / srgb / bt709 / pbr-neutral / 8-bit / white 1 / peak 1 / opaque");
    REQUIRE(
        toJson(kSdrDisplayDomain) ==
        R"({"view":"sdr","transfer":"srgb","primaries":"bt709","toneMap":"pbr-neutral","referenceWhite":1,"peakWhite":1,"bitsPerChannel":8,"opaque":true})");
    REQUIRE(kDisplayFormat == lmx::rhi::Format::BGRA8Unorm);
    REQUIRE(lmx::rhi::bytesPerPixel(kDisplayFormat) * 8 / 4 == kSdrDisplayDomain.bitsPerChannel);
}

//======================================================================================================================
TEST_CASE("display goldens carry the production domain and expected extent", "[render][display]") {
    const std::array names{"display-sdr-ramps.png", "display-sdr-bloom.png"};
    for (const char* name : names) {
        const auto path = std::filesystem::path(LMX_REPO_ROOT) / "Tests/Golden" / name;
        auto image = lmx::engine::readPng(path);
        INFO(path.string());
        INFO((image ? "" : image.error().message));
        REQUIRE(image.has_value());
        REQUIRE(image->width == (std::string_view(name) == names[0] ? 256 : 64));
        REQUIRE(image->height == 64);
        REQUIRE(image->rgba.size() == size_t{image->width} * image->height * 4);
        const auto chunk =
            std::ranges::find(image->text, "lmx:display", &lmx::engine::PngTextChunk::keyword);
        REQUIRE(chunk != image->text.end());
        REQUIRE(chunk->text == toJson(kSdrDisplayDomain));
    }
}
