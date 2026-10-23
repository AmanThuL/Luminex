#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <rojoRHI/CaptureSchema.h>
#include <sstream>

using rojoRHI::debug::CaptureSchema;

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
                           {.width = 4, .height = 2, .format = rojoRHI::Format::D32Float});
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
    schema.recordFrameDataUpload({"rojorhi.device.frameData.0.page.0", 1, 0, 64, 256, 0x1000});
    schema.beginFrameRecords();
    schema.recordFrameDataUpload({"rojorhi.device.frameData.1.page.2", 3, 1536, 304, 512, 0x2b00});
    schema.endFrameRecords();
    schema.recordFrameDataUpload({"rojorhi.device.frameData.2.page.0", 5, 0, 16, 256, 0x3000});

    const std::string json = writeAndRead(schema);
    REQUIRE(json.find("\"rojorhi.device.frameData.1.page.2\"") != std::string::npos);
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
