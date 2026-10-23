#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/CaptureSchema.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

//======================================================================================================================
TEST_CASE("registerUniformStruct reaches the capture sidecar", "[engine]") {
    lmx::engine::resetCaptureSchemaForTest();

    lmx::engine::registerUniformStruct({.name = "FacadeProbe",
                                        .sizeBytes = 48,
                                        .fields = {{"alpha", 0, "float4"}, {"beta", 32, "uint"}}});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lmx_engine_capture_schema_probe.json";
    REQUIRE(lmx::engine::writeCaptureSchema(path));
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    std::filesystem::remove(path);
    lmx::engine::resetCaptureSchemaForTest();

    CHECK(json.find("FacadeProbe") != std::string::npos);
    CHECK(json.find("alpha") != std::string::npos);
    CHECK(json.find("beta") != std::string::npos);
    CHECK(json.find("\"sizeBytes\": 48") != std::string::npos);
    CHECK(json.find("\"version\": 2") != std::string::npos);
    CHECK(json.find("\"slot\"") == std::string::npos);
    CHECK(json.find("frameDataUploads") == std::string::npos);
    CHECK(json.find("\"offsetBytes\": 32") != std::string::npos);
}
