#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/CaptureSchema.h"

#include <rojoRHI/CaptureSchema.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

//======================================================================================================================
TEST_CASE("registerUniformStruct reaches the capture sidecar", "[engine]") {
    auto& schema = rojoRHI::debug::CaptureSchema::instance();
    schema.resetForTest();

    lmx::engine::registerUniformStruct({.name = "FacadeProbe",
                                        .slot = 7,
                                        .sizeBytes = 48,
                                        .fields = {{"alpha", 0, "float4"}, {"beta", 32, "uint"}}});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lmx_engine_capture_schema_probe.json";
    REQUIRE(schema.writeJson(path));
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    std::filesystem::remove(path);
    schema.resetForTest();

    CHECK(json.find("FacadeProbe") != std::string::npos);
    CHECK(json.find("alpha") != std::string::npos);
    CHECK(json.find("beta") != std::string::npos);
    CHECK(json.find("32") != std::string::npos);
    CHECK(json.find("48") != std::string::npos);
    CHECK(json.find('7') != std::string::npos);
}
