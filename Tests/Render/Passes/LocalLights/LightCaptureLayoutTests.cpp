//----------------------------------------------------------------------------------------------------------------------
/// @file LightCaptureLayoutTests.cpp
/// @brief Pins the capture schema's scene-lighting, cluster-kernel and light-diagnostic layouts.
//----------------------------------------------------------------------------------------------------------------------
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
    const std::filesystem::path path = "lmx-light-schema-test.json";
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
        json.find(std::format(R"({{"name": "{}", "sizeBytes": {}, "fields": [)", name, size));
    REQUIRE(start != std::string::npos);
    const auto end = json.find("]}", start);
    REQUIRE(end != std::string::npos);
    const auto layout = json.substr(start, end - start);
    for (const auto& [field, offset] : fields) {
        INFO(field);
        REQUIRE(layout.contains(std::format(R"("name": "{}", "offsetBytes": {})", field, offset)));
    }
}
} // namespace

//======================================================================================================================
// CPU-only on purpose: the layouts are plain arithmetic over the mirror structs, so no device is
// needed to pin them -- which is what keeps this the test that fails first when a uniform struct
// changes shape without the sidecar being told. Schema version 2 replaced v1's binding slots with
// the address roots that carry each block, so the slot each layout named is now a root field.
TEST_CASE("Capture schema identifies scene lighting and cluster kernel layouts",
          "[capture][lighting-measurement]") {
    lmx::engine::resetCaptureSchemaForTest();
    lmx::render::registerUniformLayoutsForCapture();
    const std::string json = writeAndRead();
    REQUIRE(json.contains("\"version\": 2"));
    REQUIRE(json.find("\"slot\"") == std::string::npos);
    requireLayout(json, "LightRow", 64,
                  {{"position", 0},
                   {"range", 12},
                   {"strength", 16},
                   {"spotScale", 28},
                   {"direction", 32},
                   {"spotOffset", 44},
                   {"boundCentre", 48},
                   {"boundRadius", 60}});
    requireLayout(json, "LocalLightParams", 136,
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
    requireLayout(json, "LightClusterParams", 272,
                  {{"view", 0},
                   {"inverseProjection", 64},
                   {"rowCount", 128},
                   {"activeWidth", 132},
                   {"activeHeight", 136},
                   {"perClusterCap", 140},
                   {"globalCapacity", 144},
                   {"froxelCount", 148},
                   {"sliceDepth", 160}});
    // The scene pass root carries the rows, the grid, the index lists and the parameters that
    // slots 8 to 11 bound in v1.
    requireLayout(
        json, "ScenePassRoot", 112,
        {{"lights", 48}, {"lightGrid", 56}, {"lightIndices", 64}, {"localLightParams", 72}});
    requireLayout(json, "LightClusterRoot", 48,
                  {{"params", 0},
                   {"lights", 8},
                   {"counts", 16},
                   {"grid", 24},
                   {"indices", 32},
                   {"counters", 40}});
    requireLayout(json, "LightDebugParams", 188,
                  {{"inverseViewProjection", 0},
                   {"mode", 64},
                   {"rowCount", 68},
                   {"activeWidth", 72},
                   {"activeHeight", 76},
                   {"outputWidth", 80},
                   {"outputHeight", 84},
                   {"sliceDepth", 88}});
    requireLayout(json, "LightDebugRoot", 48,
                  {{"params", 0},
                   {"lights", 8},
                   {"grid", 16},
                   {"indices", 24},
                   {"depth", 32},
                   {"display", 36},
                   {"padding0", 40},
                   {"padding1", 44}});
}
