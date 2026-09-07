#include "RHI/CaptureSchema.h"
#include "Render/Renderer.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using lmx::rhi::debug::CaptureSchema;

namespace {

//======================================================================================================================
std::string writeAndRead(CaptureSchema& schema) {
    const std::filesystem::path path = "lmx-schema-test.json";
    std::filesystem::remove(path);
    REQUIRE(schema.writeJson(path));
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    std::filesystem::remove(path);
    return text.str();
}
} // namespace

//======================================================================================================================
TEST_CASE("CaptureSchema registers and unregisters resources") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    int textureKey = 0, bufferKey = 0;
    schema.registerTexture(&textureKey, "lmx.test.tex",
                           {.width = 4, .height = 2, .format = lmx::rhi::Format::D32Float});
    schema.registerBuffer(&bufferKey, "lmx.test.buf", 256);
    std::string json = writeAndRead(schema);
    REQUIRE(json.find("\"lmx.test.tex\"") != std::string::npos);
    REQUIRE(json.find("\"D32Float\"") != std::string::npos);
    REQUIRE(json.find("\"lmx.test.buf\"") != std::string::npos);
    schema.unregisterResource(&textureKey);
    json = writeAndRead(schema);
    REQUIRE(json.find("lmx.test.tex") == std::string::npos);
    REQUIRE(json.find("lmx.test.buf") != std::string::npos);
}

//======================================================================================================================
// A frame-data record has to name one range of one labeled page, because that is what makes an
// address seen in a capture traceable back to the call that produced it. Records outside
// begin/endFrameRecords are dropped, exactly like every other frame-record window this class
// keeps.
TEST_CASE("CaptureSchema records frame-data ranges inside the frame window") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    schema.recordFrameDataUpload({"lmx.device.frameData.0.page.0", 1, 0, 64, 256, 0x1000});
    schema.beginFrameRecords();
    schema.recordFrameDataUpload({"lmx.device.frameData.1.page.2", 3, 1536, 304, 512, 0x2b00});
    schema.endFrameRecords();
    schema.recordFrameDataUpload({"lmx.device.frameData.2.page.0", 5, 0, 16, 256, 0x3000});

    const std::string json = writeAndRead(schema);
    REQUIRE(json.find("\"lmx.device.frameData.1.page.2\"") != std::string::npos);
    REQUIRE(json.find("frameData.0.page.0") == std::string::npos);
    REQUIRE(json.find("frameData.2.page.0") == std::string::npos);
    // Every field the record exists to carry, so a dropped one fails here rather than in a capture.
    REQUIRE(json.find("\"slot\": 3") != std::string::npos);
    REQUIRE(json.find("\"pageOffset\": 1536") != std::string::npos);
    REQUIRE(json.find("\"sizeBytes\": 304") != std::string::npos);
    REQUIRE(json.find("\"alignmentBytes\": 512") != std::string::npos);
    REQUIRE(json.find("\"gpuAddress\": 11008") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("CaptureSchema escapes JSON strings") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    int key = 0;
    schema.registerBuffer(&key, "quote\"back\\slash", 1);
    const std::string json = writeAndRead(schema);
    REQUIRE(json.find("quote\\\"back\\\\slash") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("CaptureSchema writes context and layouts") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    schema.registerUniformStruct(
        {"PassUniforms", 2, 288, {{"viewProj", 0, "float4x4"}, {"shadowFilter", 272, "int"}}});
    schema.beginFrameRecords();
    schema.setContext({.sceneName = "Sponza",
                       .frameIndex = 120,
                       .boundingSphere = {1.0f, 2.0f, 3.0f, 4.0f},
                       .shadowFilter = "PCF"});
    schema.endFrameRecords();
    const std::string json = writeAndRead(schema);
    REQUIRE(json.find("\"Sponza\"") != std::string::npos);
    REQUIRE(json.find("\"PassUniforms\"") != std::string::npos);
    REQUIRE(json.find("\"shadowFilter\"") != std::string::npos);
}

//======================================================================================================================
// CPU-only on purpose: the layouts are plain arithmetic over the mirror structs, so no device is
// needed to pin them -- which is what keeps this the test that fails first when a uniform struct
// changes shape without the sidecar being told.
TEST_CASE("renderer registers the four uniform struct layouts") {
    auto& schema = lmx::rhi::debug::CaptureSchema::instance();
    schema.resetForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const std::string json = writeAndRead(schema);
    for (const char* name :
         {"PassUniforms", "ObjectUniforms", "ShadowObjectUniforms", "SkyUniforms"}) {
        INFO(name);
        REQUIRE(json.find(name) != std::string::npos);
    }
    // Spot-check one offset the whole parser hangs off: shadowTransform at byte 64.
    REQUIRE(json.find("\"shadowTransform\"") != std::string::npos);
    REQUIRE(json.find("\"preExposure\"") != std::string::npos);
    // PassUniforms lost the ambient float4 when image-based lighting replaced the flat ambient
    // term, and the 16 bytes it occupied went with it: 288 bytes to 272. The motion pair appended
    // for the temporal path took it to 400.
    REQUIRE(json.find("\"sizeBytes\": 400") != std::string::npos);
    REQUIRE(json.find("\"viewProjUnjittered\"") != std::string::npos);
    REQUIRE(json.find("\"previousViewProjUnjittered\"") != std::string::npos);
    REQUIRE(json.find("\"ambient\"") == std::string::npos);
    // SkyUniforms carries preExposure and grew for it: 80 bytes to 96, then to 176 for the sky's
    // own motion pair and the jitter its vertex stage applies.
    REQUIRE(json.find("\"sizeBytes\": 176") != std::string::npos);
    REQUIRE(json.find("\"previousViewProj\"") != std::string::npos);
    REQUIRE(json.find("\"previousEyePos\"") != std::string::npos);
    // ObjectUniforms traded fresnelR0 (16 bytes, now derived in-shader from albedo and metallic)
    // for a 64-byte inverse-transpose normal matrix: 256 bytes to 304.
    REQUIRE(json.find("\"metallic\"") != std::string::npos);
    REQUIRE(json.find("\"occlusionStrength\"") != std::string::npos);
    REQUIRE(json.find("\"emissive\"") != std::string::npos);
    REQUIRE(json.find("\"normalMatrix\"") != std::string::npos);
    REQUIRE(json.find("\"fresnelR0\"") == std::string::npos);
    // The previous frame's transform, appended for motion, took it from 304 to 368.
    REQUIRE(json.find("\"previousModel\"") != std::string::npos);
    REQUIRE(json.find("\"sizeBytes\": 368") != std::string::npos);
}
