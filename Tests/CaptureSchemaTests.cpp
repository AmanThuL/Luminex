#include "RHI/CaptureSchema.h"
#include "Render/Renderer.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
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
    for (const char* name : {"PassUniforms", "DrawUniforms", "ShadowPassUniforms", "SkyUniforms"}) {
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
    REQUIRE(json.find("\"metallic\"") != std::string::npos);
    REQUIRE(json.find("\"occlusionStrength\"") != std::string::npos);
    REQUIRE(json.find("\"emissive\"") != std::string::npos);
    REQUIRE(json.find("\"normalMatrix\"") != std::string::npos);
    REQUIRE(json.find("\"fresnelR0\"") == std::string::npos);
    REQUIRE(json.find("\"previousModel\"") != std::string::npos);
    REQUIRE(json.find("\"sizeBytes\": 240") != std::string::npos);
    REQUIRE(json.find("\"sizeBytes\": 112") != std::string::npos);
    REQUIRE(json.find("\"sizeBytes\": 16") != std::string::npos);
    REQUIRE(json.find("ObjectUniforms") == std::string::npos);
    REQUIRE(json.find("AlphaMaskParams") == std::string::npos);

    const auto requireLayout =
        [&](std::string_view name, uint32_t slot, uint32_t size,
            std::initializer_list<std::pair<std::string_view, uint32_t>> fields) {
            const auto start = json.find(
                std::format("\"name\": \"{}\", \"slot\": {}, \"sizeBytes\": {}", name, slot, size));
            REQUIRE(start != std::string::npos);
            const auto end = json.find("\n    ]}", start);
            REQUIRE(end != std::string::npos);
            const auto layout = json.substr(start, end - start);
            for (const auto& [field, offset] : fields) {
                INFO(field);
                REQUIRE(layout.find(std::format("\"name\": \"{}\", \"offsetBytes\": {}", field,
                                                offset)) != std::string::npos);
            }
        };
    requireLayout("DrawUniforms", 1, 16, {{"firstEntry", 0}});
    requireLayout("InstanceRow", 5, 240,
                  {{"model", 0},
                   {"previousModel", 64},
                   {"normalMatrix", 128},
                   {"meshRow", 192},
                   {"materialRow", 196},
                   {"flags", 200},
                   {"emissiveScale", 204},
                   {"worldBoundsMin", 208},
                   {"worldBoundsMax", 224}});
    requireLayout("MaterialRow", 6, 112,
                  {{"uvTransform", 0},
                   {"albedo", 64},
                   {"emissive", 80},
                   {"roughness", 92},
                   {"metallic", 96},
                   {"occlusionStrength", 100},
                   {"alphaCutoff", 104},
                   {"flags", 108}});
    requireLayout("MeshRow", 7, 48,
                  {{"firstIndex", 0},
                   {"indexCount", 4},
                   {"firstVertex", 8},
                   {"vertexCount", 12},
                   {"boundsMin", 16},
                   {"boundsMax", 32}});
}

//======================================================================================================================
TEST_CASE("capture schema pins GPU visibility storage and frame parameter records") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const auto json = writeAndRead(schema);
    const auto requireLayout =
        [&](std::string_view name, uint32_t slot, uint32_t size,
            std::initializer_list<std::pair<std::string_view, uint32_t>> fields) {
            CAPTURE(name);
            const auto start = json.find(
                std::format("\"name\": \"{}\", \"slot\": {}, \"sizeBytes\": {}", name, slot, size));
            REQUIRE(start != std::string::npos);
            const auto end = json.find("\n    ]}", start);
            REQUIRE(end != std::string::npos);
            const auto layout = json.substr(start, end - start);
            for (const auto& [field, offset] : fields)
                REQUIRE(layout.find(std::format("\"name\": \"{}\", \"offsetBytes\": {}", field,
                                                offset)) != std::string::npos);
        };
    requireLayout("CandidateRecord", 0, 8, {{"instanceRow", 0}, {"run", 4}});
    requireLayout(
        "RunRecord", 1, 16,
        {{"firstCandidate", 0}, {"candidateCount", 4}, {"firstSlot", 8}, {"argumentIndex", 12}});
    requireLayout("ChunkRecord", 2, 16,
                  {{"run", 0}, {"firstCandidate", 4}, {"candidateCount", 8}, {"padding", 12}});
    requireLayout("VisibilityViewParams", 5, 112,
                  {{"planes", 0},
                   {"flags", 80},
                   {"firstCandidate", 84},
                   {"candidateCount", 88},
                   {"firstRun", 92},
                   {"runCount", 96},
                   {"firstChunk", 100},
                   {"chunkCount", 104}});
    requireLayout("VisibilityParams", 6, 32,
                  {{"layout", 0},
                   {"rowCapacity", 4},
                   {"argumentCapacity", 8},
                   {"stateCapacity", 12},
                   {"candidateCount", 16},
                   {"viewCount", 20}});
    requireLayout("VisibilityCounterWords", 8, 80,
                  {{"candidates", 0},
                   {"visible", 4},
                   {"rejected", 8},
                   {"disabled", 12},
                   {"unculled", 16},
                   {"unreliable", 20},
                   {"nonfinite", 24},
                   {"emittedRows", 28},
                   {"emittedCommands", 32},
                   {"overflowedRows", 36},
                   {"overflowedCommands", 40},
                   {"occluded", 44},
                   {"tested", 48},
                   {"historyInvalid", 52},
                   {"nearCrossing", 56},
                   {"outsideSource", 60},
                   {"rectTooLarge", 64},
                   {"padding", 68}});
    requireLayout("OcclusionParams", 13, 96,
                  {{"sourceRows", 0},
                   {"sourceWidth", 64},
                   {"sourceHeight", 68},
                   {"levelCount", 72},
                   {"flags", 76},
                   {"nearGuard", 80},
                   {"depthGuard", 84},
                   {"padding", 88}});
}

//======================================================================================================================
TEST_CASE("Capture schema identifies scene lighting and cluster kernel layouts",
          "[capture][lighting-measurement]") {
    auto& schema = CaptureSchema::instance();
    schema.resetForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const auto json = writeAndRead(schema);
    const auto requireLayout =
        [&](std::string_view name, uint32_t slot, uint32_t size,
            std::initializer_list<std::pair<std::string_view, uint32_t>> fields) {
            const auto start = json.find(
                std::format("\"name\": \"{}\", \"slot\": {}, \"sizeBytes\": {}", name, slot, size));
            REQUIRE(start != std::string::npos);
            const auto end = json.find("\n    ]}", start);
            REQUIRE(end != std::string::npos);
            const auto layout = json.substr(start, end - start);
            for (const auto& [field, offset] : fields)
                REQUIRE(layout.contains(
                    std::format("\"name\": \"{}\", \"offsetBytes\": {}", field, offset)));
        };
    requireLayout("LightRow", 8, 64,
                  {{"position", 0},
                   {"range", 12},
                   {"strength", 16},
                   {"spotScale", 28},
                   {"direction", 32},
                   {"spotOffset", 44},
                   {"boundCentre", 48},
                   {"boundRadius", 60}});
    requireLayout("LocalLightParams", 11, 136,
                  {{"mode", 0},
                   {"rowCount", 4},
                   {"gridX", 8},
                   {"gridY", 12},
                   {"gridZ", 16},
                   {"activeOriginX", 20},
                   {"activeOriginY", 24},
                   {"activeWidth", 28},
                   {"activeHeight", 32},
                   {"sliceDepth", 36}});
    requireLayout("LightClusterParams", 0, 272,
                  {{"view", 0},
                   {"inverseProjection", 64},
                   {"rowCount", 128},
                   {"activeWidth", 132},
                   {"activeHeight", 136},
                   {"perClusterCap", 140},
                   {"globalCapacity", 144},
                   {"froxelCount", 148},
                   {"sliceDepth", 160}});
}
