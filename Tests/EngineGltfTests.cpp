#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/GltfLoader.h"
#include "EngineTestSupport.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace lmx::engine;
using lmx::test::near3;

namespace {
constexpr float kEps = 1e-4f;
}

namespace {

//======================================================================================================================
bool nearMat4(const glm::mat4& a, const glm::mat4& b) {
    for (int col = 0; col < 4; ++col) {
        if (!near3(glm::vec3(a[col]), glm::vec3(b[col])) || std::abs(a[col].w - b[col].w) > kEps) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
void writeFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

//======================================================================================================================
void appendBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), bytes, bytes + size);
}

//======================================================================================================================
// A self-contained translated quad with authored positions, normals and UVs. Generated tangents
// are (1,0,0,1); includeTangent adds distinct authored (0,0,1,-1) values to isolate read-through.
std::filesystem::path writeQuadGltfFixture(const std::filesystem::path& dir,
                                           bool includeTangent = false) {
    std::filesystem::create_directories(dir);

    // clang-format off
    const float positions[4][3] = {{-1.f, 0.f, 1.f}, {1.f, 0.f, 1.f},
                                    {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}};
    const float normals[4][3] = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    const float tangents[4][4] = {{0.f, 0.f, 1.f, -1.f}, {0.f, 0.f, 1.f, -1.f},
                                   {0.f, 0.f, 1.f, -1.f}, {0.f, 0.f, 1.f, -1.f}};
    const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    // clang-format on

    std::vector<uint8_t> bin;
    for (const auto& p : positions)
        appendBytes(bin, p, sizeof(p));
    for (const auto& n : normals)
        appendBytes(bin, n, sizeof(n));
    for (const auto& uv : uvs)
        appendBytes(bin, uv, sizeof(uv));
    if (includeTangent) {
        for (const auto& t : tangents)
            appendBytes(bin, t, sizeof(t));
    }
    appendBytes(bin, indices, sizeof(indices));
    // 48 + 48 + 32 (+ 64 TANGENT) + 12 -- the JSON byte offsets below assume this layout.
    REQUIRE(bin.size() == (includeTangent ? 204u : 140u));

    const std::filesystem::path binPath = dir / "quad.bin";
    std::ofstream binOut(binPath, std::ios::binary | std::ios::trunc);
    binOut.write(reinterpret_cast<const char*>(bin.data()),
                 static_cast<std::streamsize>(bin.size()));
    binOut.close();

    // clang-format off
    const std::string jsonNoTangent = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [1.0, 2.0, 3.0]}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3,
    "material": 0
  }]}],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.0, 0.0, 1.0],
      "metallicFactor": 0.0,
      "roughnessFactor": 0.5
    }
  }],
  "buffers": [{"uri": "quad.bin", "byteLength": 140}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
     "min": [-1.0, 0.0, -1.0], "max": [1.0, 0.0, 1.0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})";
    // Same fixture, with an authored VEC4 TANGENT accessor (bufferView 3, byte offset 128,
    // length 64) inserted before the index bufferView/accessor -- which shift from 3 to 4.
    const std::string jsonWithTangent = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [1.0, 2.0, 3.0]}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3},
    "indices": 4,
    "material": 0
  }]}],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.0, 0.0, 1.0],
      "metallicFactor": 0.0,
      "roughnessFactor": 0.5
    }
  }],
  "buffers": [{"uri": "quad.bin", "byteLength": 204}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962},
    {"buffer": 0, "byteOffset": 128, "byteLength": 64, "target": 34962},
    {"buffer": 0, "byteOffset": 192, "byteLength": 12, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
     "min": [-1.0, 0.0, -1.0], "max": [1.0, 0.0, 1.0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})";
    // clang-format on
    const std::filesystem::path gltfPath = dir / "quad.gltf";
    writeFile(gltfPath, includeTangent ? jsonWithTangent : jsonNoTangent);
    return gltfPath;
}

//======================================================================================================================
// cgltf accepts an index accessor without a bufferView, but unpacking it returns zero. This
// fixture ensures the loader reports that failure instead of keeping zero-initialized indices.
std::filesystem::path writeBadIndexAccessorFixture(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);

    // clang-format off
    const float positions[4][3] = {{-1.f, 0.f, 1.f}, {1.f, 0.f, 1.f},
                                    {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}};
    const float normals[4][3] = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    // clang-format on

    std::vector<uint8_t> bin;
    for (const auto& p : positions)
        appendBytes(bin, p, sizeof(p));
    for (const auto& n : normals)
        appendBytes(bin, n, sizeof(n));
    for (const auto& uv : uvs)
        appendBytes(bin, uv, sizeof(uv));
    REQUIRE(bin.size() == 128); // no index data -- the index accessor references no bufferView

    const std::filesystem::path binPath = dir / "quad.bin";
    std::ofstream binOut(binPath, std::ios::binary | std::ios::trunc);
    binOut.write(reinterpret_cast<const char*>(bin.data()),
                 static_cast<std::streamsize>(bin.size()));
    binOut.close();

    const std::string json = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3,
    "material": 0
  }]}],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.0, 0.0, 1.0],
      "metallicFactor": 0.0,
      "roughnessFactor": 0.5
    }
  }],
  "buffers": [{"uri": "quad.bin", "byteLength": 128}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
     "min": [-1.0, 0.0, -1.0], "max": [1.0, 0.0, 1.0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})";
    const std::filesystem::path gltfPath = dir / "quad.gltf";
    writeFile(gltfPath, json);
    return gltfPath;
}

//======================================================================================================================
// Asset tests run from the binary directory, so locate fetched inputs by walking toward the root.
std::filesystem::path findRepoPath(const std::filesystem::path& relative) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / relative)) {
            return dir / relative;
        }
        if (!dir.has_parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return dir / relative;
}

} // namespace

//======================================================================================================================
TEST_CASE("loadGltf reports a descriptive error for a missing file", "[engine]") {
    const auto result = loadGltf("/nonexistent/does-not-exist.gltf");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == AssetErrorCode::NotFound);
    REQUIRE_FALSE(result.error().message.empty());
}

//======================================================================================================================
TEST_CASE("loadGltf classifies a missing external buffer as NotFound", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-missing-buffer-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);
    std::filesystem::remove(dir / "quad.bin");

    const auto result = loadGltf(gltfPath.string());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == AssetErrorCode::NotFound);
    REQUIRE(result.error().message.find("buffers") != std::string::npos);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf classifies an unreadable external buffer as Io", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-unreadable-buffer-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);
    std::filesystem::remove(dir / "quad.bin");
    std::filesystem::create_directory(dir / "quad.bin");

    const auto result = loadGltf(gltfPath.string());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == AssetErrorCode::Io);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf parses a minimal quad: mesh/material/instance", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-loader-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    const GltfScene& scene = *result;

    REQUIRE(scene.meshes.size() == 1);
    REQUIRE(scene.meshes[0].vertices.size() == 4);
    REQUIRE(scene.meshes[0].indices.size() == 6);

    REQUIRE(scene.materials.size() == 1);
    const GltfMaterial& mat = scene.materials[0];
    REQUIRE(near3(glm::vec3(mat.baseColorFactor), {1.0f, 0.0f, 0.0f}));
    REQUIRE(mat.baseColorFactor.a == Catch::Approx(1.0f));
    REQUIRE(mat.metallic == Catch::Approx(0.0f));
    REQUIRE(mat.roughness == Catch::Approx(0.5f));
    REQUIRE(mat.baseColorImage == -1);
    REQUIRE(mat.normalImage == -1);
    // No metallicRoughnessTexture/occlusionTexture/emissiveTexture/emissiveFactor in the fixture:
    // every new input keeps its default.
    REQUIRE(mat.metallicRoughnessImage == -1);
    REQUIRE(mat.occlusionImage == -1);
    REQUIRE(mat.occlusionStrength == Catch::Approx(1.0f));
    REQUIRE(mat.emissiveImage == -1);
    REQUIRE(near3(mat.emissiveFactor, {0.0f, 0.0f, 0.0f}));

    REQUIRE(scene.instances.size() == 1);
    REQUIRE(scene.instances[0].meshIndex == 0);
    REQUIRE(scene.instances[0].materialIndex == 0);
    const glm::mat4 expectedWorld = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(nearMat4(scene.instances[0].world, expectedWorld));

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf traverses only the active scene and preserves parent transforms", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-active-scene-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const auto replaceOnce = [&](std::string_view from, std::string_view to) {
        const size_t offset = json.find(from);
        REQUIRE(offset != std::string::npos);
        json.replace(offset, from.size(), to);
    };
    replaceOnce("\"scene\": 0", "\"scene\": 1");
    replaceOnce("\"scenes\": [{\"nodes\": [0]}]",
                "\"scenes\": [{\"nodes\": [0]}, {\"nodes\": [1]}]");
    replaceOnce("\"nodes\": [{\"mesh\": 0, \"translation\": [1.0, 2.0, 3.0]}]",
                "\"nodes\": [{\"mesh\": 1, \"translation\": [100.0, 0.0, 0.0]}, "
                "{\"translation\": [10.0, 0.0, 0.0], \"children\": [2]}, "
                "{\"mesh\": 0, \"translation\": [1.0, 0.0, 0.0]}]");
    const size_t meshStart = json.find("[{\"primitives\": [{");
    const size_t meshEnd = json.find("],\n  \"materials\"", meshStart);
    REQUIRE(meshStart != std::string::npos);
    REQUIRE(meshEnd != std::string::npos);
    const std::string mesh = json.substr(meshStart + 1, meshEnd - meshStart - 1);
    json.insert(meshEnd, ", " + mesh);
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    REQUIRE(result->instances.size() == 1);
    REQUIRE(result->meshes.size() == 1);
    REQUIRE(result->instances[0].world[3][0] == Catch::Approx(11.0f));
    REQUIRE(result->instances[0].world[3][1] == Catch::Approx(0.0f));

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf ignores images referenced only by unused materials", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-unused-material-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const std::string_view materialEnd = "  }],\n  \"buffers\"";
    const size_t offset = json.find(materialEnd);
    REQUIRE(offset != std::string::npos);
    json.replace(offset, materialEnd.size(),
                 "  }, {\"pbrMetallicRoughness\": {\"baseColorTexture\": {\"index\": 0}}}],\n"
                 "  \"textures\": [{\"source\": 0}],\n"
                 "  \"images\": [{\"uri\": \"missing-unused.png\"}],\n"
                 "  \"buffers\"");
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    REQUIRE(result->materials.size() == 1);
    REQUIRE(result->meshes.size() == 1);
    REQUIRE(result->images.size() == 1);
    REQUIRE(result->images[0].rgba8.empty());

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf permits one image in base-color and normal slots", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-dual-colorspace-image-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);
    {
        std::ofstream image(dir / "shared.ppm", std::ios::binary | std::ios::trunc);
        image << "P6\n1 1\n255\n";
        const std::array<unsigned char, 3> pixel{128, 128, 255};
        image.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
    }

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const auto replaceOnce = [&](std::string_view from, std::string_view to) {
        const size_t offset = json.find(from);
        REQUIRE(offset != std::string::npos);
        json.replace(offset, from.size(), to);
    };
    replaceOnce("\"materials\": [{", "\"materials\": [{\"normalTexture\": {\"index\": 0},");
    replaceOnce("\"baseColorFactor\"", "\"baseColorTexture\": {\"index\": 0}, \"baseColorFactor\"");
    replaceOnce("  \"buffers\"", "  \"textures\": [{\"source\": 0}],\n"
                                 "  \"images\": [{\"uri\": \"shared.ppm\"}],\n"
                                 "  \"buffers\"");
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    REQUIRE(result->materials.size() == 1);
    REQUIRE(result->materials[0].baseColorImage == 0);
    REQUIRE(result->materials[0].normalImage == 0);
    REQUIRE(result->images[0].rgba8.size() == 4);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
// Factor-only: emissiveFactor with no emissive texture.
TEST_CASE("loadGltf reads an emissive factor without an emissive texture", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-emissive-factor-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const size_t offset = json.find("\"pbrMetallicRoughness\"");
    REQUIRE(offset != std::string::npos);
    json.insert(offset, "\"emissiveFactor\": [0.5, 0.25, 0.75],\n    ");
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    REQUIRE(result->materials.size() == 1);
    const GltfMaterial& mat = result->materials[0];
    REQUIRE(near3(mat.emissiveFactor, {0.5f, 0.25f, 0.75f}));
    REQUIRE(mat.emissiveImage == -1);
    REQUIRE(mat.metallicRoughnessImage == -1);
    REQUIRE(mat.occlusionImage == -1);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
// Texture-only: metallic-roughness and occlusion images, no emissiveFactor override.
TEST_CASE("loadGltf reads metallic-roughness and occlusion textures", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-mr-occlusion-texture-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);
    {
        std::ofstream image(dir / "mr.ppm", std::ios::binary | std::ios::trunc);
        image << "P6\n1 1\n255\n";
        const std::array<unsigned char, 3> pixel{0, 128, 200};
        image.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
    }

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const auto replaceOnce = [&](std::string_view from, std::string_view to) {
        const size_t offset = json.find(from);
        REQUIRE(offset != std::string::npos);
        json.replace(offset, from.size(), to);
    };
    replaceOnce("\"materials\": [{",
                "\"materials\": [{\"occlusionTexture\": {\"index\": 0, \"strength\": 0.35},");
    replaceOnce("\"baseColorFactor\"",
                "\"metallicRoughnessTexture\": {\"index\": 0}, \"baseColorFactor\"");
    replaceOnce("  \"buffers\"", "  \"textures\": [{\"source\": 0}],\n"
                                 "  \"images\": [{\"uri\": \"mr.ppm\"}],\n"
                                 "  \"buffers\"");
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    REQUIRE(result->materials.size() == 1);
    const GltfMaterial& mat = result->materials[0];
    REQUIRE(mat.metallicRoughnessImage == 0);
    REQUIRE(mat.occlusionImage == 0);
    REQUIRE(mat.occlusionStrength == Catch::Approx(0.35f));
    REQUIRE(mat.emissiveImage == -1);
    REQUIRE(near3(mat.emissiveFactor, {0.0f, 0.0f, 0.0f}));
    REQUIRE(result->images[0].rgba8.size() == 4);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
// Both: an emissiveFactor and an emissive texture together, isolating that neither overwrites the
// other.
TEST_CASE("loadGltf reads an emissive factor together with an emissive texture", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-emissive-both-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);
    {
        std::ofstream image(dir / "emissive.ppm", std::ios::binary | std::ios::trunc);
        image << "P6\n1 1\n255\n";
        const std::array<unsigned char, 3> pixel{255, 200, 100};
        image.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
    }

    std::ifstream input(gltfPath);
    std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    const auto replaceOnce = [&](std::string_view from, std::string_view to) {
        const size_t offset = json.find(from);
        REQUIRE(offset != std::string::npos);
        json.replace(offset, from.size(), to);
    };
    replaceOnce("\"materials\": [{", "\"materials\": [{\"emissiveTexture\": {\"index\": 0}, "
                                     "\"emissiveFactor\": [0.2, 0.3, 0.4],");
    replaceOnce("  \"buffers\"", "  \"textures\": [{\"source\": 0}],\n"
                                 "  \"images\": [{\"uri\": \"emissive.ppm\"}],\n"
                                 "  \"buffers\"");
    writeFile(gltfPath, json);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    REQUIRE(result->materials.size() == 1);
    const GltfMaterial& mat = result->materials[0];
    REQUIRE(mat.emissiveImage == 0);
    REQUIRE(near3(mat.emissiveFactor, {0.2f, 0.3f, 0.4f}));
    REQUIRE(mat.metallicRoughnessImage == -1);
    REQUIRE(mat.occlusionImage == -1);
    REQUIRE(result->images[0].rgba8.size() == 4);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf generates tangents for a quad with no authored TANGENT", "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-loader-tangent-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    for (const VertexPNTU& v : result->meshes[0].vertices) {
        REQUIRE(near3({v.tx, v.ty, v.tz}, {1.0f, 0.0f, 0.0f}));
        REQUIRE(v.tw == Catch::Approx(1.0f));
    }

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
// The authored (0,0,1,-1) tangent differs from the generated (1,0,0,1), isolating read-through.
TEST_CASE("loadGltf reads an authored TANGENT accessor verbatim (does not regenerate)",
          "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-loader-authored-tangent-test";
    const std::filesystem::path gltfPath = writeQuadGltfFixture(dir, /*includeTangent=*/true);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE(result.has_value());
    for (const VertexPNTU& v : result->meshes[0].vertices) {
        REQUIRE(near3({v.tx, v.ty, v.tz}, {0.0f, 0.0f, 1.0f}));
        REQUIRE(v.tw == Catch::Approx(-1.0f));
    }

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf fails descriptively when an index accessor's unpack fails "
          "(missing bufferView)",
          "[engine]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lmx-gltf-loader-bad-index-test";
    const std::filesystem::path gltfPath = writeBadIndexAccessorFixture(dir);

    const auto result = loadGltf(gltfPath.string());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.find("indices") != std::string::npos);

    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("loadGltf(DamagedHelmet.glb): one mesh, generated tangents (no authored TANGENT), "
          "base color + normal images",
          "[engine]") {
    const std::filesystem::path path =
        findRepoPath("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb");
    if (!std::filesystem::exists(path)) {
        SKIP("Assets/Fetched/DamagedHelmet/DamagedHelmet.glb not present (xmake setup fetches "
             "it) -- skipping the asset-gated pin");
    }

    const auto result = loadGltf(path.string());
    REQUIRE(result.has_value());
    const GltfScene& scene = *result;

    REQUIRE(scene.meshes.size() == 1);
    // This primitive has no TANGENT accessor, so every tangent is generated from the real mesh.
    for (const VertexPNTU& v : scene.meshes[0].vertices) {
        const glm::vec3 n{v.nx, v.ny, v.nz};
        const glm::vec3 t{v.tx, v.ty, v.tz};
        REQUIRE(glm::length(t) == Catch::Approx(1.0f).margin(1e-3));
        REQUIRE(glm::dot(n, t) == Catch::Approx(0.0f).margin(1e-2));
        REQUIRE(std::abs(std::abs(v.tw) - 1.0f) < 1e-4f);
    }

    REQUIRE_FALSE(scene.materials.empty());
    bool foundBaseColor = false;
    bool foundNormal = false;
    bool foundMetallicRoughness = false;
    bool foundOcclusion = false;
    bool foundEmissive = false;
    const auto checkPlausibleImage = [&](int imageIndex) {
        const GltfImage& img = scene.images[static_cast<size_t>(imageIndex)];
        REQUIRE(img.width > 0);
        REQUIRE(img.height > 0);
        REQUIRE(img.rgba8.size() == static_cast<size_t>(img.width) * img.height * 4);
    };
    for (const GltfMaterial& mat : scene.materials) {
        if (mat.baseColorImage >= 0) {
            foundBaseColor = true;
            checkPlausibleImage(mat.baseColorImage);
        }
        if (mat.normalImage >= 0) {
            foundNormal = true;
            checkPlausibleImage(mat.normalImage);
        }
        if (mat.metallicRoughnessImage >= 0) {
            foundMetallicRoughness = true;
            checkPlausibleImage(mat.metallicRoughnessImage);
        }
        if (mat.occlusionImage >= 0) {
            foundOcclusion = true;
            checkPlausibleImage(mat.occlusionImage);
        }
        if (mat.emissiveImage >= 0) {
            foundEmissive = true;
            checkPlausibleImage(mat.emissiveImage);
        }
    }
    REQUIRE(foundBaseColor);
    REQUIRE(foundNormal);
    // Damaged Helmet ships all three of these inputs.
    REQUIRE(foundMetallicRoughness);
    REQUIRE(foundOcclusion);
    REQUIRE(foundEmissive);
}

//======================================================================================================================
TEST_CASE("loadGltf(Sponza.gltf): converted images decode and tangents generate", "[engine]") {
    const std::filesystem::path path = findRepoPath("Assets/Fetched/Sponza/Sponza.gltf");
    if (!std::filesystem::exists(path)) {
        SKIP("Assets/Fetched/Sponza/Sponza.gltf not present (xmake setup fetches it) -- "
             "skipping the asset-gated pin");
    }

    const auto result = loadGltf(path.string());
    REQUIRE(result.has_value());
    const GltfScene& scene = *result;

    REQUIRE(scene.meshes.size() == 25);
    REQUIRE(scene.materials.size() == 25);
    REQUIRE(scene.images.size() == 24);
    size_t decodedImages = 0;
    for (const GltfImage& img : scene.images) {
        if (img.rgba8.empty()) {
            REQUIRE(img.width == 0);
            REQUIRE(img.height == 0);
            continue;
        }
        ++decodedImages;
        REQUIRE(img.width > 0);
        REQUIRE(img.height > 0);
        REQUIRE(img.rgba8.size() == static_cast<size_t>(img.width) * img.height * 4);
    }
    REQUIRE(decodedImages == scene.images.size());

    size_t generatedTangentVertices = 0;
    float maximumLengthError = 0.0f;
    float maximumOrthogonalityError = 0.0f;
    size_t invalidNormals = 0;
    size_t invalidHandedness = 0;
    for (const GeoData& mesh : scene.meshes) {
        for (const VertexPNTU& v : mesh.vertices) {
            const glm::vec3 n{v.nx, v.ny, v.nz};
            const glm::vec3 t{v.tx, v.ty, v.tz};
            const float normalLength = glm::length(n);
            if (!std::isfinite(normalLength) || normalLength < 1e-8f) {
                ++invalidNormals;
                continue;
            }
            maximumLengthError = std::max(maximumLengthError, std::abs(glm::length(t) - 1.0f));
            maximumOrthogonalityError =
                std::max(maximumOrthogonalityError, std::abs(glm::dot(n / normalLength, t)));
            invalidHandedness += std::abs(std::abs(v.tw) - 1.0f) >= 1e-3f ? 1 : 0;
            ++generatedTangentVertices;
        }
    }
    REQUIRE(generatedTangentVertices == 184406);
    REQUIRE(invalidNormals == 0);
    REQUIRE(maximumLengthError < 1e-2f);
    REQUIRE(maximumOrthogonalityError < 1e-3f);
    REQUIRE(invalidHandedness == 0);
}
