#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/IO/File.h"
#include "Support/SceneDocumentFixtures.h"

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
    // A document without animations has no companion; a missing file is the empty buffer.
    using Bytes = decltype(lmx::readWholeFile(binPath));
    const auto binBytes =
        fs::exists(binPath) ? lmx::readWholeFile(binPath) : Bytes(std::vector<std::byte>{});
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
    REQUIRE(paths.size() == 6);

    for (const auto& path : paths) {
        INFO(path.string());
        const auto doc = readSceneDocument(path);
        REQUIRE(doc);
        CHECK(canonicalBytesMatch(*doc, path));
    }
}

//======================================================================================================================
TEST_CASE("schema 2 content and animation documents equal their canonical rewrite",
          "[scene-document][canonical][ux6-write]") {
    const fs::path root = "SceneDocuments/canonical-content";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto path = root / "cube.scene.gltf";
    auto doc = lmx::test::contentDocument();
    SECTION("geometry alone") {}
    SECTION("animation and geometry") {
        doc.animations = lmx::test::animatedDocument().animations;
    }
    REQUIRE(saveSceneDocument(doc, path));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK(canonicalBytesMatch(*read, path));
    CHECK(*lmx::readWholeFile(root / "cube.scene.geometry.bin") ==
          lmx::asset::sceneDocumentGeometry(*read));
}

//======================================================================================================================
TEST_CASE("catalog mobility authors only the helmet and every light movable",
          "[scene-document][canonical][ux6-mobility]") {
    const fs::path catalog = fs::path(LMX_REPO_ROOT) / "Assets/Scenes";
    size_t scenes = 0, helmets = 0, lights = 0;
    for (const auto& entry : fs::directory_iterator(catalog)) {
        if (!entry.path().filename().string().ends_with(".scene.gltf"))
            continue;
        const auto doc = readSceneDocument(entry.path());
        INFO(entry.path().string());
        REQUIRE(doc);
        CHECK(doc->schemaVersion == 2);
        ++scenes;
        for (const auto& node : doc->nodes) {
            const bool helmet = node.asset && node.asset->uri.contains("DamagedHelmet");
            const bool movable = node.light || helmet;
            CHECK(node.mobility ==
                  (movable ? lmx::asset::DocMobility::Movable : lmx::asset::DocMobility::Static));
            helmets += helmet;
            lights += node.light.has_value();
        }
    }
    CHECK(scenes == 6);
    CHECK(helmets == 1);
    CHECK(lights == 34);
}
