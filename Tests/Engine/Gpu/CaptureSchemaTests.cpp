#include "Engine/Gpu/CaptureSchema.h"
#include "Support/TestGpu.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

using namespace lmx::engine;

namespace {

//======================================================================================================================
std::string writeAndRead() {
    const std::filesystem::path path = "lmx-schema-test.json";
    std::filesystem::remove(path);
    REQUIRE(writeCaptureSchema(path));
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    std::filesystem::remove(path);
    return text.str();
}
} // namespace

//======================================================================================================================
TEST_CASE("CaptureSchema registers and unregisters resources", "[device]") {
    resetCaptureSchemaForTest();
    auto gpu = createTestGpu();
    auto buffer = GpuBuffer::create(gpu->device(), {.size = 256, .label = "lmx.test.buf"});
    INFO(errorOf(buffer));
    REQUIRE(buffer.has_value());
    {
        auto texture = GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::D32Float,
                                                          .extent = {4, 2, 1},
                                                          .usage = rojoRHI::TextureUsage::Sampled,
                                                          .label = "lmx.test.tex"});
        INFO(errorOf(texture));
        REQUIRE(texture.has_value());
        const auto identity = texture->rhi();
        GpuTexture moved = std::move(*texture);
        REQUIRE(moved.rhi() == identity);
        const auto json = writeAndRead();
        REQUIRE(json.contains("\"lmx.test.tex\""));
        REQUIRE(json.contains("\"D32Float\""));
        REQUIRE(json.contains("\"lmx.test.buf\""));
    }
    const auto json = writeAndRead();
    REQUIRE_FALSE(json.contains("lmx.test.tex"));
    REQUIRE(json.contains("lmx.test.buf"));
}

//======================================================================================================================
// Named root addresses are recorded only during the active capture window and deduplicated
// by name/address pair. A later window starts with fresh roots while layouts persist.
TEST_CASE("CaptureSchema records named root addresses inside the frame window") {
    resetCaptureSchemaForTest();
    registerUniformStruct({"WindowRoot", 64, {{"value", 0, "float4"}}});
    registerFrameRoot("outsideBefore", {0x1000, 0});
    beginCaptureFrameRecords();
    registerFrameRoot("WindowRoot", {0x2b00, 3});
    registerFrameRoot("WindowRoot", {0x2b00, 3});
    endCaptureFrameRecords();
    registerFrameRoot("outsideAfter", {0x3000, 0});
    const auto json = writeAndRead();
    REQUIRE(json.contains("\"version\": 2"));
    REQUIRE(json.contains("WindowRoot"));
    REQUIRE_FALSE(json.contains("outsideBefore"));
    REQUIRE_FALSE(json.contains("outsideAfter"));
    REQUIRE(json.contains("\"lo\": 11008"));
    REQUIRE(json.contains("\"hi\": 3"));
    const auto roots = json.substr(json.find("\"frameRoots\""));
    REQUIRE(roots.find("\"name\": \"WindowRoot\"") != std::string::npos);
    REQUIRE(roots.find("\"name\": \"WindowRoot\"") == roots.rfind("\"name\": \"WindowRoot\""));
    REQUIRE_FALSE(json.contains("frameDataUploads"));
    REQUIRE_FALSE(json.contains("\"slot\""));
    beginCaptureFrameRecords();
    endCaptureFrameRecords();
    const auto next = writeAndRead();
    REQUIRE(next.contains("WindowRoot"));
    REQUIRE_FALSE(next.contains("11008"));
}

//======================================================================================================================
TEST_CASE("CaptureSchema escapes JSON strings") {
    resetCaptureSchemaForTest();
    const rojoRHI::Buffer key{1};
    registerCaptureResource(key, {.size = 1, .label = "quote\"back\\slash"});
    const rojoRHI::Texture sameBits{1};
    registerCaptureResource(
        sameBits,
        {.format = rojoRHI::Format::D32Float, .extent = {4, 2, 1}, .label = "same-bits-texture"});
    unregisterCaptureResource(sameBits);
    const std::string json = writeAndRead();
    REQUIRE(json.find("quote\\\"back\\\\slash") != std::string::npos);
    REQUIRE_FALSE(json.contains("same-bits-texture"));
    unregisterCaptureResource(key);
}

//======================================================================================================================
TEST_CASE("CaptureSchema writes context and layouts") {
    resetCaptureSchemaForTest();
    registerUniformStruct(
        {"PassUniforms", 288, {{"viewProj", 0, "float4x4"}, {"shadowFilter", 272, "int"}}});
    beginCaptureFrameRecords();
    setCaptureContext({.sceneName = "Sponza",
                       .frameIndex = 120,
                       .boundingSphere = {1.0f, 2.0f, 3.0f, 4.0f},
                       .shadowFilter = "PCF"});
    endCaptureFrameRecords();
    const std::string json = writeAndRead();
    REQUIRE(json.find("\"Sponza\"") != std::string::npos);
    REQUIRE(json.find("\"PassUniforms\"") != std::string::npos);
    REQUIRE(json.find("\"shadowFilter\"") != std::string::npos);
}
