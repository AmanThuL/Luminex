//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityLab.cpp
/// @brief Builds seeded repeated geometry, boundary probes, and a looping visibility camera rail.
//----------------------------------------------------------------------------------------------------------------------

#include "Scene/Scene.h"

#include "Core/Color.h"
#include "Scene/SceneEnvironment.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace lmx::scene {
namespace {
constexpr double kDuration = 12.0;
constexpr float kSpacing = 3.0f;

//======================================================================================================================
render::MeshData makeIcosphere() {
    const float golden = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const std::array<glm::vec3, 12> positions{{{-1, golden, 0},
                                               {1, golden, 0},
                                               {-1, -golden, 0},
                                               {1, -golden, 0},
                                               {0, -1, golden},
                                               {0, 1, golden},
                                               {0, -1, -golden},
                                               {0, 1, -golden},
                                               {golden, 0, -1},
                                               {golden, 0, 1},
                                               {-golden, 0, -1},
                                               {-golden, 0, 1}}};
    const std::array<uint32_t, 60> faces{{0, 11, 5, 0, 5,  1,  0,  1,  7,  0,  7, 10, 0, 10, 11,
                                          1, 5,  9, 5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1,  8,
                                          3, 9,  4, 3, 4,  2,  3,  2,  6,  3,  6, 8,  3, 8,  9,
                                          4, 9,  5, 2, 4,  11, 6,  2,  10, 8,  6, 7,  9, 8,  1}};
    render::MeshData mesh;
    const auto vertex = [&](glm::vec3 normal) {
        normal = glm::normalize(normal);
        const glm::vec3 point = normal * 0.5f;
        const glm::vec3 tangent = std::abs(normal.y) > 0.99f
                                      ? glm::vec3(1, 0, 0)
                                      : glm::normalize(glm::cross(glm::vec3(0, 1, 0), normal));
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back({point.x, point.y, point.z, normal.x, normal.y, normal.z, tangent.x,
                                 tangent.y, tangent.z, 1.0f,
                                 std::atan2(normal.z, normal.x) / glm::two_pi<float>() + 0.5f,
                                 std::acos(normal.y) / glm::pi<float>()});
    };
    for (size_t face = 0; face < faces.size(); face += 3) {
        const glm::vec3 a = glm::normalize(positions[faces[face]]);
        const glm::vec3 b = glm::normalize(positions[faces[face + 1]]);
        const glm::vec3 c = glm::normalize(positions[faces[face + 2]]);
        const glm::vec3 ab = glm::normalize(a + b);
        const glm::vec3 bc = glm::normalize(b + c);
        const glm::vec3 ca = glm::normalize(c + a);
        for (const glm::vec3 point : {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca}) {
            vertex(point);
        }
    }
    return mesh;
}

//======================================================================================================================
float jitter(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xffffu) / 65535.0f - 0.5f;
}

} // namespace

//======================================================================================================================
asset::AssetResult<std::unique_ptr<Scene>>
loadVisibilityLabScene(rojoRHI::Device& device, uint32_t instanceCount, uint32_t occluderCount) {
    if (instanceCount == 0 || instanceCount > 1048576) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                 "VisibilityLab instances must be 1..1048576"});
    }
    if (occluderCount > 1024) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                 "VisibilityLab occluders must be 0..1024"});
    }
    auto scene = std::make_unique<Scene>();
    scene->name = "VisibilityLab";
    auto cube = render::makeCube();
    for (size_t i = 0; i < cube.vertices.size(); ++i) {
        cube.vertices[i].u = (i % 4 == 1 || i % 4 == 2) ? 1.0f : 0.0f;
        cube.vertices[i].v = i % 4 >= 2 ? 1.0f : 0.0f;
    }
    const std::array<MeshId, 2> meshes{scene->addMesh(std::move(cube), "VisibilityLab.cube"),
                                       scene->addMesh(makeIcosphere(), "VisibilityLab.icosphere")};
    const std::array<uint8_t, 16> maskPixels{255, 255, 255, 255, 255, 255, 255, 0,
                                             255, 255, 255, 0,   255, 255, 255, 255};
    const rojoRHI::TextureMip maskMip{.data = maskPixels.data(), .bytesPerRow = 8};
    auto mask = device.createTexture({.width = 2,
                                      .height = 2,
                                      .format = rojoRHI::Format::RGBA8Unorm_sRGB,
                                      .sampled = true,
                                      .label = "VisibilityLab.mask"},
                                     std::span(&maskMip, 1));
    if (!mask) {
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::UploadFailed, mask.error().message});
    }
    const auto maskId = scene->addTexture(std::move(*mask));
    const std::array<glm::vec4, 4> colors{{{0.82f, 0.30f, 0.15f, 1},
                                           {0.15f, 0.52f, 0.85f, 1},
                                           {0.82f, 0.68f, 0.12f, 1},
                                           {0.24f, 0.72f, 0.48f, 1}}};
    std::array<MaterialId, 4> materials;
    for (size_t i = 0; i < materials.size(); ++i) {
        MaterialRecord material;
        material.albedo = srgbToLinear(colors[i]);
        material.roughness = 0.35f + static_cast<float>(i) * 0.15f;
        if (i >= 2) {
            material.alphaMode = render::AlphaMode::Mask;
            material.diffuse = maskId;
            material.doubleSided = i == 3;
        }
        materials[i] = scene->addMaterial(material);
    }
    scene->objects.reserve(instanceCount);
    render::Aabb bounds{glm::vec3(std::numeric_limits<float>::max()),
                        glm::vec3(std::numeric_limits<float>::lowest())};
    const auto add = [&](std::string name, glm::vec3 position, float scale, uint32_t index) {
        scene->addObject({.name = std::move(name),
                          .position = position,
                          .scale = glm::vec3(scale),
                          .mesh = meshes[index % 2],
                          .material = materials[index % 4]});
        bounds.minimum = glm::min(bounds.minimum, position - glm::vec3(scale * 0.5f));
        bounds.maximum = glm::max(bounds.maximum, position + glm::vec3(scale * 0.5f));
    };
    constexpr float fov = glm::pi<float>() / 3.0f;
    const float edgeY = 4.0f * std::tan(fov * 0.5f);
    const float edgeX = edgeY * 16.0f / 9.0f;
    const std::array<glm::vec3, 5> probes{
        {{-edgeX, 0, -4}, {edgeX, 0, -4}, {0, edgeY, -4}, {0, -edgeY, -4}, {0, 0, -0.1f}}};
    const std::array<const char*, 5> names{"left", "right", "top", "bottom", "near"};
    for (uint32_t i = 0; i < std::min(instanceCount, 5u); ++i) {
        add(std::string("Boundary ") + names[i], probes[i], i == 4 ? 0.04f : 0.4f, i);
    }
    const uint32_t side = static_cast<uint32_t>(std::ceil(std::cbrt(instanceCount)));
    uint32_t seed = 0x4C4D5836u;
    for (uint32_t i = 5; i < instanceCount; ++i) {
        const uint32_t grid = i - 5;
        const float x = (float(grid % side) - float(side - 1) * 0.5f) * kSpacing;
        const float y = (float((grid / side) % side) - float(side - 1) * 0.5f) * kSpacing;
        const float z = (float(grid / (side * side)) - float(side - 1) * 0.5f) * kSpacing;
        const float jitterX = jitter(seed) * 0.2f;
        const float jitterY = jitter(seed) * 0.2f;
        const float jitterZ = jitter(seed) * 0.2f;
        const glm::vec3 offset(jitterX, jitterY, jitterZ);
        add("Grid " + std::to_string(grid), glm::vec3(x, y, z) + offset, 1.0f, i);
    }
    if (occluderCount != 0) {
        MaterialRecord opaque;
        opaque.albedo = srgbToLinear(glm::vec4(0.28f, 0.32f, 0.38f, 1.0f));
        opaque.roughness = 0.9f;
        const auto opaqueId = scene->addMaterial(opaque);
        auto masked = opaque;
        masked.alphaMode = render::AlphaMode::Mask;
        masked.diffuse = maskId;
        masked.doubleSided = true;
        const auto maskedId = scene->addMaterial(masked);
        // The initial camera faces -Z through these slabs into the grid. Their horizontal gaps
        // and the first slab's two broad alpha windows stay many pixels wide in the lab matrix.
        // Adding them after the seeded grid leaves every original instance and material intact.
        const float span = std::max(12.0f, bounds.maximum.x - bounds.minimum.x);
        const float height = std::max(8.0f, bounds.maximum.y - bounds.minimum.y);
        const float spacing = span / static_cast<float>(occluderCount);
        const float width = spacing * 0.75f;
        for (uint32_t i = 0; i < occluderCount; ++i) {
            const glm::vec3 position{(static_cast<float>(i) + 0.5f) * spacing - span * 0.5f, 0.0f,
                                     -5.0f};
            const glm::vec3 scale{width, height, 0.2f};
            scene->addObject({.name = "Occluder " + std::to_string(i),
                              .position = position,
                              .scale = scale,
                              .mesh = meshes[0],
                              .material = i == 0 ? maskedId : opaqueId});
            bounds.minimum = glm::min(bounds.minimum, position - scale * 0.5f);
            bounds.maximum = glm::max(bounds.maximum, position + scale * 0.5f);
        }
    }
    const glm::vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const float radius = glm::length(bounds.maximum - center);
    scene->boundingSphere = glm::vec4(center, radius);
    scene->initialCamera = {{0, 0, 0}, 0, 0, fov, 0.1f, radius * 8.0f};
    const glm::vec3 endpoint =
        center + glm::vec3(radius * 0.5f, radius * 0.2f, radius / std::sin(fov * 0.5f) * 1.2f);
    const size_t keyCount = static_cast<size_t>(kDuration * asset::kAnimationBakeRate) + 1;
    scene->animation.cameraTrack.reserve(keyCount);
    for (size_t i = 0; i < keyCount; ++i) {
        const double time = static_cast<double>(i) / asset::kAnimationBakeRate;
        const float phase = static_cast<float>(time / kDuration) * glm::two_pi<float>();
        const float fraction = (1.0f - std::cos(phase)) * 0.5f;
        const glm::vec3 position = endpoint * fraction;
        const glm::vec3 target = glm::mix(glm::vec3(0, 0, -radius), center, fraction);
        const glm::vec3 direction = glm::normalize(target - position);
        scene->animation.cameraTrack.push_back({.time = time,
                                                .position = position,
                                                .yaw = std::atan2(direction.x, -direction.z),
                                                .pitch = std::asin(direction.y)});
    }
    scene->animation.duration = kDuration;
    scene->animation.loop = true;
    scene->resetMotion();
    if (auto environment = attachNeutralEnvironment(device, *scene, "VisibilityLab");
        !environment) {
        return std::unexpected(environment.error());
    }
    if (auto finalized = scene->finalize(device); !finalized) {
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::UploadFailed, finalized.error().message});
    }
    return scene;
}

} // namespace lmx::scene
