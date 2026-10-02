#pragma once

#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/SceneDocument.h"

#include <array>
#include <span>

namespace lmx::test {
using namespace asset;

//======================================================================================================================
inline SceneDocument animatedDocument() {
    SceneDocument doc;
    doc.name = "Document test";
    doc.nodes = {{.name = "Camera", .camera = 0}};
    doc.rootNodes = {0};
    doc.cameras = {{.name = "Perspective"}};
    DocAnimation animation;
    animation.name = "Camera rail";
    animation.keyCount = 2;
    animation.channels = {{.node = 0,
                           .path = DocChannelPath::Translation,
                           .values = {{0.0f, 1.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 3.0f, 0.0f}}}};
    doc.animations.push_back(animation);
    return doc;
}

//======================================================================================================================
inline SceneDocument completeDocument() {
    auto doc = animatedDocument();
    doc.look.environment.hdri =
        SceneLook::Hdri{.uri = "Fetched/studio.hdr", .sha256 = std::string(64, 'a')};
    DocNode source;
    source.name = "Asset";
    source.asset = DocAsset{"Fetched/model.gltf", std::string(64, 'b')};
    source.overrides = {{.node = 7, .name = "Mesh", .enabled = false, .pose = ObjectPose{}}};
    doc.nodes.push_back(source);
    doc.nodes.push_back(
        {.name = "Lab", .generator = DocGenerator{"MaterialLab", {{"first", 1.25}, {"a/b", 5.0}}}});
    doc.lights = {{.name = "Key"}};
    doc.nodes.push_back({.name = "Key node", .light = 0, .role = "key", .castsShadow = true});
    doc.rootNodes = {0, 1, 2, 3};
    return doc;
}

//======================================================================================================================
inline SceneDocument contentDocument() {
    SceneDocument doc;
    doc.schemaVersion = kSceneDocumentSchema;
    doc.name = "Content test";
    doc.nodes = {{.name = "Camera", .camera = 0},
                 {.name = "First cube", .translation = {1.25f, 2.f, 3.f}, .mesh = 0},
                 {.name = "Second cube",
                  .scale = {2.f, 3.f, 4.f},
                  .mesh = 1,
                  .motion = DocMotion::Invalid,
                  .enabled = false}};
    doc.rootNodes = {0, 1, 2};
    doc.cameras = {{.name = "Perspective"}};
    doc.bounds = std::pair{glm::vec3(-2.f, -3.f, -4.f), glm::vec3(5.f, 6.f, 7.f)};
    auto content = std::make_shared<DocContent>();
    GeoData cube;
    for (uint32_t v = 0; v < 8; ++v)
        cube.vertices.push_back({v & 1 ? 1.f : -1.f, v & 2 ? 1.f : -1.f, v & 4 ? 1.f : -1.f, 0.f,
                                 0.f, 0.99999f, 0.99998f, -0.f, 0.f, -1.f, float(v & 1),
                                 float(v & 2)});
    cube.indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                    2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    std::vector<std::byte> geometry;
    const auto vertices = std::as_bytes(std::span(cube.vertices));
    const auto indices = std::as_bytes(std::span(cube.indices));
    geometry.insert(geometry.end(), vertices.begin(), vertices.end());
    geometry.insert(geometry.end(), indices.begin(), indices.end());
    content->geometrySha256 = sha256Hex(geometry);
    content->geometries.push_back(std::move(cube));
    {
        constexpr uint8_t file[]{
            137, 80,  78,  71,  13,  10,  26,  10,  0,   0,   0,   13,  73,  72,  68,  82,  0,
            0,   0,   4,   0,   0,   0,   4,   8,   6,   0,   0,   0,   169, 241, 158, 126, 0,
            0,   0,   79,  73,  68,  65,  84,  120, 156, 1,   68,  0,   187, 255, 0,   0,   3,
            6,   255, 12,  15,  18,  255, 24,  27,  30,  255, 36,  39,  42,  255, 0,   48,  51,
            54,  255, 60,  63,  66,  255, 72,  75,  78,  255, 84,  87,  90,  255, 0,   96,  99,
            102, 255, 108, 111, 114, 255, 120, 123, 126, 255, 132, 135, 138, 255, 0,   144, 147,
            150, 255, 156, 159, 162, 255, 168, 171, 174, 255, 180, 183, 186, 255, 149, 33,  33,
            97,  138, 177, 12,  128, 0,   0,   0,   0,   73,  69,  78,  68,  174, 66,  96,  130,
        };
        constexpr uint8_t pixels[]{0,   3,   6,   255, 12,  15,  18,  255, 24,  27,  30,  255, 36,
                                   39,  42,  255, 48,  51,  54,  255, 60,  63,  66,  255, 72,  75,
                                   78,  255, 84,  87,  90,  255, 96,  99,  102, 255, 108, 111, 114,
                                   255, 120, 123, 126, 255, 132, 135, 138, 255, 144, 147, 150, 255,
                                   156, 159, 162, 255, 168, 171, 174, 255, 180, 183, 186, 255};
        DocImage image{.name = "color", .width = 4, .height = 4, .mipmapped = true};
        const auto bytes = std::as_bytes(std::span(file));
        image.file.assign(bytes.begin(), bytes.end());
        image.sha256 = sha256Hex(image.file);
        const auto rgba = std::as_bytes(std::span(pixels));
        image.rgba8.assign(rgba.begin(), rgba.end());
        content->images.push_back(std::move(image));
    }
    {
        constexpr uint8_t file[]{
            137, 80,  78,  71,  13,  10,  26,  10,  0,  0,  0,   13,  73,  72,  68,
            82,  0,   0,   0,   2,   0,   0,   0,   1,  8,  6,   0,   0,   0,   244,
            34,  127, 138, 0,   0,   0,   17,  73,  68, 65, 84,  120, 156, 99,  104,
            104, 248, 255, 159, 161, 254, 255, 127, 0,  22, 248, 5,   124, 165, 216,
            182, 132, 0,   0,   0,   0,   73,  69,  78, 68, 174, 66,  96,  130,
        };
        constexpr uint8_t pixels[]{128, 128, 255, 255, 0, 127, 255, 255};
        DocImage image{.name = "linear", .width = 2, .height = 1, .mipmapped = false};
        const auto bytes = std::as_bytes(std::span(file));
        image.file.assign(bytes.begin(), bytes.end());
        image.sha256 = sha256Hex(image.file);
        const auto rgba = std::as_bytes(std::span(pixels));
        image.rgba8.assign(rgba.begin(), rgba.end());
        content->images.push_back(std::move(image));
    }
    doc.content = std::move(content);
    doc.meshes = {{"Cube color", 0, 0}, {"Cube data", 0, 1}};
    DocMaterial color{.name = "Color", .emissiveStrength = 2.5f};
    color.values.baseColorFactor = {0.2f, 0.3f, 0.4f, 0.5f};
    color.values.baseColorImage = 0;
    color.values.metallic = 0.25f;
    color.values.roughness = 0.75f;
    color.values.emissiveFactor = {0.1f, 0.2f, 0.3f};
    color.values.emissiveImage = 0;
    DocMaterial data{.name = "Data"};
    data.values.normalImage = 1;
    data.values.metallicRoughnessImage = 1;
    data.values.occlusionImage = 1;
    data.values.occlusionStrength = 0.625f;
    data.values.alphaMode = GltfAlphaMode::Mask;
    data.values.alphaCutoff = 0.375f;
    data.values.doubleSided = true;
    doc.materials = {color, data};
    return doc;
}

} // namespace lmx::test
