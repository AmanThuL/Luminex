#include <catch2/catch_test_macros.hpp>

#include "RHI/Validate.h"

using namespace lmx::rhi;

namespace {
// ShaderLibrary is an interface; pipeline validation only inspects the pointer,
// so a trivial concrete subclass is enough to exercise the happy path.
struct DummyShaderLibrary : ShaderLibrary {};

// Stands in for the CAMetalLayer* the windowing layer supplies; validation only
// checks it against nullptr.
int dummyNativeLayer = 0;
} // namespace

TEST_CASE("BufferDesc with zero size is rejected", "[rhi]") {
    BufferDesc desc{};
    desc.size = 0;
    desc.label = "zero";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("size"));
}

TEST_CASE("BufferDesc with a non-zero size is accepted", "[rhi]") {
    BufferDesc desc{};
    desc.size = 256;
    desc.label = "vertices";

    REQUIRE(validate(desc).has_value());
}

TEST_CASE("TextureDesc with zero width is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 0;
    desc.height = 64;
    desc.label = "empty";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("width"));
}

TEST_CASE("TextureDesc with zero height is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 0;
    desc.label = "flat";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("height"));
}

// Metal 4 / Apple7+ caps a 2D texture at 16384 per side (Validate.cpp); one pixel over that on
// either axis must be rejected before it ever reaches the backend, since MTLTextureDescriptor
// validation aborts the process on an oversized texture rather than returning a diagnosable
// error. Pure validation -- no device or GPU needed to exercise this.
TEST_CASE("TextureDesc exceeding the max 2D dimension is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 16385;
    desc.height = 16385;
    desc.label = "oversized";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("width"));
}

TEST_CASE("TextureDesc with Format::Unknown is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = Format::Unknown;
    desc.label = "unknown";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("format"));
}

TEST_CASE("TextureDesc cpuReadback with a non-8-bit format is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = Format::D32Float;
    desc.cpuReadback = true;
    desc.label = "depth readback";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("cpuReadback"));
    REQUIRE(r.error().message.contains("8-bit"));
}

TEST_CASE("TextureDesc with a render target and readback is accepted", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = Format::BGRA8Unorm;
    desc.renderTarget = true;
    desc.cpuReadback = true;
    desc.label = "offscreen";

    REQUIRE(validate(desc).has_value());
}

TEST_CASE("GraphicsPipelineDesc with a null library is rejected", "[rhi]") {
    GraphicsPipelineDesc desc{};
    desc.library = nullptr;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.label = "no library";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("library"));
}

TEST_CASE("GraphicsPipelineDesc with an empty vertexEntry is rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "";
    desc.fragmentEntry = "fragmentMain";
    desc.label = "no vertex entry";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("vertexEntry"));
}

TEST_CASE("GraphicsPipelineDesc with an empty fragmentEntry is rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "";
    desc.label = "no fragment entry";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("fragmentEntry"));
}

TEST_CASE("GraphicsPipelineDesc with Format::Unknown color is rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.colorFormat = Format::Unknown;
    desc.label = "unknown color";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("colorFormat"));
}

// Depth in a color slot is not a recoverable driver error: Metal's render-pipeline
// descriptor validator aborts the process on it, so validation has to catch it first.
TEST_CASE("GraphicsPipelineDesc with a depth colorFormat is rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.colorFormat = Format::D32Float;
    desc.label = "depth as color";

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("colorFormat"));
    REQUIRE(r.error().message.contains("color-renderable"));
}

TEST_CASE("GraphicsPipelineDesc with a library and both entries is accepted", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.colorFormat = Format::BGRA8Unorm;
    desc.label = "triangle";

    REQUIRE(validate(desc).has_value());
}

TEST_CASE("SwapchainDesc with a null nativeLayer is rejected", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = nullptr;
    desc.width = 1280;
    desc.height = 720;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("nativeLayer"));
}

TEST_CASE("SwapchainDesc with a zero extent is rejected", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = &dummyNativeLayer;
    desc.width = 0;
    desc.height = 720;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("width"));
}

TEST_CASE("SwapchainDesc with a zero height is rejected", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = &dummyNativeLayer;
    desc.width = 1280;
    desc.height = 0;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("height"));
}

TEST_CASE("SwapchainDesc with Format::Unknown is rejected", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = &dummyNativeLayer;
    desc.width = 1280;
    desc.height = 720;
    desc.format = Format::Unknown;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("format"));
}

// CAMetalLayer rejects a depth pixel format outright, so this must not reach the backend.
TEST_CASE("SwapchainDesc with a depth format is rejected", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = &dummyNativeLayer;
    desc.width = 1280;
    desc.height = 720;
    desc.format = Format::D32Float;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("format"));
    REQUIRE(r.error().message.contains("color-renderable"));
}

TEST_CASE("SwapchainDesc with a layer and non-zero extent is accepted", "[rhi]") {
    SwapchainDesc desc{};
    desc.nativeLayer = &dummyNativeLayer;
    desc.width = 1280;
    desc.height = 720;
    desc.format = Format::BGRA8Unorm;

    REQUIRE(validate(desc).has_value());
}
