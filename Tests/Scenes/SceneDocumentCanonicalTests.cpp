#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/IO/File.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using lmx::asset::readSceneDocument;
using lmx::asset::saveSceneDocument;
using lmx::asset::SceneDocument;
using lmx::asset::sceneDocumentBuffer;
using lmx::asset::sceneDocumentJson;

namespace {

//======================================================================================================================
bool canonicalBytesMatch(const SceneDocument& doc, const fs::path& path) {
    auto binPath = path;
    binPath.replace_extension(".bin");
    const auto jsonBytes = lmx::readWholeFile(path);
    const auto binBytes = lmx::readWholeFile(binPath);
    if (!jsonBytes || !binBytes)
        return false;
    const std::string json(reinterpret_cast<const char*>(jsonBytes->data()), jsonBytes->size());
    return json == sceneDocumentJson(doc, binPath.filename().string()) &&
           *binBytes == sceneDocumentBuffer(doc);
}

//======================================================================================================================
void writeText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file << text;
    REQUIRE(file.good());
}

} // namespace

//======================================================================================================================
TEST_CASE("canonical scene document comparison detects noncanonical bytes",
          "[scene-document][canonical]") {
    fs::create_directories("SceneDocuments");
    const fs::path path = "SceneDocuments/canonical-probe.scene.gltf";
    SceneDocument doc;
    doc.name = "Canonical probe";
    doc.nodes = {{.name = "Camera", .camera = 0}};
    doc.rootNodes = {0};
    doc.cameras = {{.name = "Perspective"}};
    REQUIRE(saveSceneDocument(doc, path));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK(canonicalBytesMatch(*read, path));

    const auto original = lmx::readWholeFile(path);
    REQUIRE(original);
    const std::string originalText(reinterpret_cast<const char*>(original->data()),
                                   original->size());
    writeText(path, " " + originalText);
    REQUIRE(readSceneDocument(path));
    CHECK_FALSE(canonicalBytesMatch(*read, path));
    writeText(path, originalText);
    CHECK(canonicalBytesMatch(*read, path));

    auto binPath = path;
    binPath.replace_extension(".bin");
    writeText(binPath, "x");
    CHECK_FALSE(canonicalBytesMatch(*read, path));
    writeText(binPath, "");
    CHECK(canonicalBytesMatch(*read, path));
}

//======================================================================================================================
TEST_CASE("every catalog scene document equals its canonical rewrite",
          "[scene-document][canonical]") {
    const fs::path catalog = fs::path(LMX_REPO_ROOT) / "Assets/Scenes";
    std::vector<fs::path> paths;
    if (fs::is_directory(catalog)) {
        for (const auto& entry : fs::directory_iterator(catalog)) {
            if (entry.is_regular_file() &&
                entry.path().filename().string().ends_with(".scene.gltf"))
                paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    if (paths.empty())
        SKIP("Catalog has no .scene.gltf documents before export; canonical catalog gate did "
             "not run.");

    for (const auto& path : paths) {
        INFO(path.string());
        const auto doc = readSceneDocument(path);
        REQUIRE(doc);
        CHECK(canonicalBytesMatch(*doc, path));
    }
}
