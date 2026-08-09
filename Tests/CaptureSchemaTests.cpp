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
TEST_CASE("CaptureSchema records uploads only while recording") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    schema.recordUniformUpload({"lmx.device.uniformRing.0", 2, 0, 288}); // before begin: dropped
    schema.beginFrameRecords();
    schema.recordUniformUpload({"lmx.device.uniformRing.1", 2, 1024, 288});
    schema.endFrameRecords();
    schema.recordUniformUpload({"lmx.device.uniformRing.2", 2, 2048, 288}); // after end: dropped
    const std::string json = writeAndRead(schema);
    REQUIRE(json.find("uniformRing.1") != std::string::npos);
    REQUIRE(json.find("uniformRing.0") == std::string::npos);
    REQUIRE(json.find("uniformRing.2") == std::string::npos);
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
    REQUIRE(json.find("\"sizeBytes\": 288") != std::string::npos);
    // preExposure folds into padding PassUniforms already carried, so the block's size is
    // unchanged -- which is exactly why the sidecar has to be told about the field explicitly.
    REQUIRE(json.find("\"preExposure\"") != std::string::npos);
    // SkyUniforms carries the same field and does grow for it: 80 bytes to 96.
    REQUIRE(json.find("\"sizeBytes\": 96") != std::string::npos);
}
