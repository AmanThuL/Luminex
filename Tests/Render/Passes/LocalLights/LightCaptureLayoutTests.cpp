#include "Render/Renderer/Renderer.h"
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
// CPU-only on purpose: the layouts are plain arithmetic over the mirror structs, so no device is
// needed to pin them -- which is what keeps this the test that fails first when a uniform struct
// changes shape without the sidecar being told.
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
