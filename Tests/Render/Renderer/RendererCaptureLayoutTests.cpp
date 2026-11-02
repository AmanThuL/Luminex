#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Renderer/Renderer.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

//======================================================================================================================
std::string writeAndRead() {
    const std::filesystem::path path = "lmx-schema-test.json";
    std::filesystem::remove(path);
    REQUIRE(lmx::engine::writeCaptureSchema(path));
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    std::filesystem::remove(path);
    return text.str();
}

//======================================================================================================================
// Schema version 2 names each layout once, with no binding slot, and lists its fields in order;
// a layout's block runs from its name to the closing bracket of its field array.
void requireLayout(const std::string& json, std::string_view name, uint32_t size,
                   std::initializer_list<std::pair<std::string_view, uint32_t>> fields) {
    CAPTURE(name);
    const auto start =
        json.find(std::format("{{\"name\": \"{}\", \"sizeBytes\": {}, \"fields\": [", name, size));
    REQUIRE(start != std::string::npos);
    const auto end = json.find("]}", start);
    REQUIRE(end != std::string::npos);
    const auto layout = json.substr(start, end - start);
    for (const auto& [field, offset] : fields) {
        INFO(field);
        REQUIRE(
            layout.contains(std::format("\"name\": \"{}\", \"offsetBytes\": {}", field, offset)));
    }
}
} // namespace

//======================================================================================================================
// CPU-only on purpose: the layouts are plain arithmetic over the mirror structs, so no device is
// needed to pin them -- which is what keeps this the test that fails first when a uniform struct
// changes shape without the sidecar being told.
TEST_CASE("renderer registers the four uniform struct layouts") {
    lmx::engine::resetCaptureSchemaForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const std::string json = writeAndRead();
    // The per-draw uniform block became the draw root that addresses the pass root: v2 has no
    // binding slots, so a draw's first visible row travels in SceneDrawRoot instead.
    for (const char* name :
         {"PassUniforms", "SceneDrawRoot", "ShadowPassUniforms", "SkyUniforms"}) {
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
    REQUIRE(json.find("\"sizeBytes\": 48") != std::string::npos);
    REQUIRE(json.find("ObjectUniforms") == std::string::npos);
    REQUIRE(json.find("AlphaMaskParams") == std::string::npos);
    REQUIRE(json.find("DrawUniforms") == std::string::npos);
    REQUIRE(json.find("\"slot\"") == std::string::npos);
    REQUIRE(json.contains("\"version\": 2"));

    requireLayout(json, "SceneDrawRoot", 48, {{"pass", 0}, {"firstEntry", 8}});
    requireLayout(json, "ScenePassRoot", 112,
                  {{"vertices", 0}, {"visibleRows", 8}, {"instances", 16}, {"materials", 24}});
    requireLayout(json, "SkyRoot", 32, {{"vertices", 0}, {"uniforms", 8}, {"exposure", 16}});
    requireLayout(json, "ShadowPassRoot", 48, {{"vertices", 0}, {"visibleRows", 8}});
    requireLayout(json, "ShadowDrawRoot", 32, {{"pass", 0}});
    requireLayout(json, "InstanceRow", 240,
                  {{"model", 0},
                   {"previousModel", 64},
                   {"normalMatrix", 128},
                   {"meshRow", 192},
                   {"materialRow", 196},
                   {"flags", 200},
                   {"emissiveScale", 204},
                   {"worldBoundsMin", 208},
                   {"worldBoundsMax", 224}});
    requireLayout(json, "MaterialRow", 112,
                  {{"uvTransform", 0},
                   {"albedo", 64},
                   {"emissive", 80},
                   {"roughness", 92},
                   {"metallic", 96},
                   {"occlusionStrength", 100},
                   {"alphaCutoff", 104},
                   {"flags", 108}});
    requireLayout(json, "MeshRow", 48,
                  {{"firstIndex", 0},
                   {"indexCount", 4},
                   {"firstVertex", 8},
                   {"vertexCount", 12},
                   {"boundsMin", 16},
                   {"boundsMax", 32}});
}

//======================================================================================================================
TEST_CASE("capture schema pins GPU visibility storage and frame parameter records") {
    lmx::engine::resetCaptureSchemaForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const auto json = writeAndRead();
    REQUIRE(json.find("\"slot\"") == std::string::npos);
    requireLayout(json, "CandidateRecord", 8, {{"instanceRow", 0}, {"run", 4}});
    requireLayout(
        json, "RunRecord", 16,
        {{"firstCandidate", 0}, {"candidateCount", 4}, {"firstSlot", 8}, {"argumentIndex", 12}});
    requireLayout(json, "ChunkRecord", 16,
                  {{"run", 0}, {"firstCandidate", 4}, {"candidateCount", 8}, {"padding", 12}});
    requireLayout(json, "VisibilityViewParams", 112,
                  {{"planes", 0},
                   {"flags", 80},
                   {"firstCandidate", 84},
                   {"candidateCount", 88},
                   {"firstRun", 92},
                   {"runCount", 96},
                   {"firstChunk", 100},
                   {"chunkCount", 104}});
    requireLayout(json, "VisibilityParams", 32,
                  {{"layout", 0},
                   {"rowCapacity", 4},
                   {"argumentCapacity", 8},
                   {"stateCapacity", 12},
                   {"candidateCount", 16},
                   {"viewCount", 20}});
    requireLayout(json, "VisibilityCounterWords", 84,
                  {{"candidates", 0},
                   {"visible", 4},
                   {"rejected", 8},
                   {"cullingOff", 12},
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
                   {"disabled", 68},
                   {"padding", 72}});
    requireLayout(json, "OcclusionParams", 96,
                  {{"sourceRows", 0},
                   {"sourceWidth", 64},
                   {"sourceHeight", 68},
                   {"levelCount", 72},
                   {"flags", 76},
                   {"nearGuard", 80},
                   {"depthGuard", 84},
                   {"padding", 88}});
    // The kernels reach those records through their address roots, which the sidecar names too.
    requireLayout(json, "VisibilityClassifyRoot", 64,
                  {{"candidatesAddr", 0}, {"paramsAddr", 32}, {"countsAddr", 56}});
    requireLayout(json, "VisibilityOcclusionRoot", 80,
                  {{"countsAddr", 56}, {"occlusionAddr", 64}, {"pyramid", 72}});
    requireLayout(json, "VisibilityScanRoot", 48, {{"runsAddr", 0}, {"offsetsAddr", 32}});
    requireLayout(json, "VisibilityEmitRoot", 96,
                  {{"candidatesAddr", 0}, {"rowsAddr", 80}, {"argumentsAddr", 88}});
}
