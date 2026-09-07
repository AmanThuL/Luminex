#include "EngineTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/epsilon.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace lmx::test {

namespace {
constexpr float kEpsilon = 1e-4f;

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

} // namespace

//======================================================================================================================
bool near3(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEpsilon));
}

//======================================================================================================================
std::optional<std::filesystem::path> findRepoAsset(std::string_view relativePath) {
    std::filesystem::path directory = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::path candidate = directory / relativePath;
            std::filesystem::exists(candidate)) {
            return candidate;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
    return std::nullopt;
}

//======================================================================================================================
engine::ibl::IblTextures makeUniformIbl(rhi::Device& device, const glm::vec3& radiance,
                                        std::string_view label) {
    // Face size 1: a constant environment carries no detail for a larger source to hold, and the
    // generators' output extents are fixed by engine::ibl regardless of what they read from.
    auto generated =
        engine::ibl::generate(device, engine::ibl::makeConstantCubemap(radiance, 1), label);
    REQUIRE(generated.has_value());
    return std::move(*generated);
}

//======================================================================================================================
std::filesystem::path writeAnimatedQuadGltf(const std::filesystem::path& dir,
                                            std::string_view interpolation, bool hierarchy) {
    std::filesystem::create_directories(dir);

    // clang-format off
    const float positions[4][3] = {{-1.f, 0.f, 1.f}, {1.f, 0.f, 1.f},
                                    {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}};
    const float normals[4][3] = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    const float times[2] = {0.f, 1.f};
    const float linearTranslations[2][3] = {{0.f, 0.f, 0.f}, {2.f, 4.f, -6.f}};
    // in-tangent, value, out-tangent per key, all tangents zero.
    const float cubicTranslations[6][3] = {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f},
                                            {0.f, 0.f, 0.f}, {2.f, 4.f, -6.f}, {0.f, 0.f, 0.f}};
    // clang-format on

    std::vector<uint8_t> bin;
    for (const auto& p : positions)
        appendBytes(bin, p, sizeof(p));
    for (const auto& n : normals)
        appendBytes(bin, n, sizeof(n));
    for (const auto& uv : uvs)
        appendBytes(bin, uv, sizeof(uv));
    appendBytes(bin, indices, sizeof(indices));
    appendBytes(bin, times, sizeof(times));
    for (const auto& t : linearTranslations)
        appendBytes(bin, t, sizeof(t));
    for (const auto& t : cubicTranslations)
        appendBytes(bin, t, sizeof(t));
    // 48 + 48 + 32 + 12 + 8 + 24 + 72 -- the JSON byte offsets below assume this layout.
    REQUIRE(bin.size() == 244u);

    const std::filesystem::path binPath = dir / "quad.bin";
    std::ofstream binOut(binPath, std::ios::binary | std::ios::trunc);
    binOut.write(reinterpret_cast<const char*>(bin.data()),
                 static_cast<std::streamsize>(bin.size()));
    binOut.close();

    const bool cubic = interpolation == "CUBICSPLINE";
    const std::string json =
        std::string(R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  )") +
        (hierarchy ? R"("nodes": [{"children": [1], "translation": [10.0, 0.0, 0.0]},
            {"mesh": 0, "translation": [0.0, 1.0, 2.0]}],)"
                   : R"("nodes": [{"mesh": 0, "translation": [10.0, 0.0, 0.0]}],)") +
        R"(
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3,
    "material": 0
  }]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0]}}],
  "animations": [{
    "samplers": [{"input": 4, "output": )" +
        (cubic ? "6" : "5") + R"(, "interpolation": ")" + std::string(interpolation) + R"("}],
    "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]
  }],
  "buffers": [{"uri": "quad.bin", "byteLength": 244}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12, "target": 34963},
    {"buffer": 0, "byteOffset": 140, "byteLength": 8},
    {"buffer": 0, "byteOffset": 148, "byteLength": 24},
    {"buffer": 0, "byteOffset": 172, "byteLength": 72}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
     "min": [-1.0, 0.0, -1.0], "max": [1.0, 0.0, 1.0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0.0], "max": [1.0]},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC3"},
    {"bufferView": 6, "componentType": 5126, "count": 6, "type": "VEC3"}
  ]
})";
    const std::filesystem::path gltfPath = dir / "quad.gltf";
    writeFile(gltfPath, json);
    return gltfPath;
}

} // namespace lmx::test
