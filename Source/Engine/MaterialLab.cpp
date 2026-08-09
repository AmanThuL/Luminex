#include "Engine/Scene.h"

#include "Engine/Color.h"
#include "Engine/GeometryGenerator.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::engine {

namespace {

// Same neutral sky + three-light rig every catalog scene uses (Scene.cpp's attachSkyAndLights);
// duplicated here because that helper is file-local to Scene.cpp.
constexpr glm::vec3 kLightDirections[3] = {
    {0.577f, -0.577f, 0.577f},
    {-0.577f, -0.577f, 0.577f},
    {0.0f, -0.707f, -0.707f},
};

constexpr float kLightStrengths[3] = {0.7f, 0.2f, 0.2f};
static_assert(std::size(kLightStrengths) == std::size(kLightDirections),
              "kLightStrengths and kLightDirections must have the same length");

//======================================================================================================================
AssetError uploadFailure(rhi::Error error) {
    return AssetError{AssetErrorCode::UploadFailed, std::move(error.message)};
}

//======================================================================================================================
AssetResult<void> attachSkyAndLights(rhi::Device& device, Scene& scene, std::string_view label) {
    auto sphere = render::createMesh(device, render::fromGeo(makeSphere(0.5f, 20, 20)),
                                     std::string(label) + ".skySphere");
    if (!sphere) {
        return std::unexpected(uploadFailure(std::move(sphere.error())));
    }
    scene.skySphere = std::move(*sphere);

    constexpr std::array<uint8_t, 4> kNeutralSky = {149, 170, 196, 255};
    const rhi::TextureMip face{.data = kNeutralSky.data(), .bytesPerRow = 4};
    const std::array<rhi::TextureMip, 6> faces = {face, face, face, face, face, face};
    auto cubemap = device.createTexture({.width = 1,
                                         .height = 1,
                                         .format = rhi::Format::RGBA8Unorm_sRGB,
                                         .kind = rhi::TextureKind::Cube,
                                         .mipLevels = 1,
                                         .sampled = true,
                                         .label = std::string(label) + ".sky"},
                                        faces);
    if (!cubemap) {
        return std::unexpected(uploadFailure(std::move(cubemap.error())));
    }
    scene.skyCubemap = std::move(*cubemap);

    for (size_t i = 0; i < std::size(kLightDirections); ++i) {
        scene.lights[i].direction = kLightDirections[i];
        scene.lights[i].strength = glm::vec3(srgbToLinear(kLightStrengths[i]));
    }
    scene.ambient = srgbToLinear(glm::vec3(0.25f, 0.25f, 0.35f));
    return {};
}

//======================================================================================================================
// A flat quad in the local Z=0 plane, +Z normal, corners wound CCW as seen from +Z (this
// project's one front-facing winding -- RHI.h's CullMode::Back). UV spans the full [0,1] range
// left to right, top to bottom, unlike Render/Mesh.cpp's makePlane whose UVs are all zero.
render::MeshData makeMaterialQuad(float halfWidth, float halfHeight) {
    render::MeshData mesh;
    struct Corner {
        float x, y, u, v;
    };
    constexpr std::array<Corner, 4> kCorners = {{
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f, 0.0f},
    }};
    for (const Corner& c : kCorners) {
        mesh.vertices.push_back({c.x * halfWidth, c.y * halfHeight, 0.0f, // position
                                 0.0f, 0.0f, 1.0f,                        // normal: +Z
                                 1.0f, 0.0f, 0.0f, 1.0f,                  // tangent: +X
                                 c.u, c.v});
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

//======================================================================================================================
// 256x1, byte i in every channel at column i -- the simplest possible monotonic ramp, decoded
// through the sRGB texture view like every other authored colour this project uploads.
std::vector<uint8_t> makeGradientRampPixels() {
    std::vector<uint8_t> pixels(size_t{256} * 4);
    for (size_t i = 0; i < 256; ++i) {
        const uint8_t byte = static_cast<uint8_t>(i);
        pixels[i * 4 + 0] = byte;
        pixels[i * 4 + 1] = byte;
        pixels[i * 4 + 2] = byte;
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}

//======================================================================================================================
// 64x64 tangent-space normal map: a centred hemispherical bump of texel radius kBumpRadius, flat
// {128,128,255,255} (local +Z, i.e. no perturbation) outside it. Data, not colour -- RGBA8Unorm,
// no sRGB view.
std::vector<uint8_t> makeNormalMapPixels() {
    constexpr uint32_t kSize = 64;
    constexpr float kCenter = (kSize - 1) * 0.5f;
    constexpr float kBumpRadius = 28.0f;
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const float dx = (static_cast<float>(x) - kCenter) / kBumpRadius;
            const float dy = (static_cast<float>(y) - kCenter) / kBumpRadius;
            const float r2 = dx * dx + dy * dy;
            glm::vec3 normal{0.0f, 0.0f, 1.0f};
            if (r2 < 1.0f) {
                normal = glm::normalize(glm::vec3{dx, dy, std::sqrt(1.0f - r2)});
            }
            const size_t offset = (size_t{y} * kSize + x) * 4;
            pixels[offset + 0] = static_cast<uint8_t>((normal.x * 0.5f + 0.5f) * 255.0f);
            pixels[offset + 1] = static_cast<uint8_t>((normal.y * 0.5f + 0.5f) * 255.0f);
            pixels[offset + 2] = static_cast<uint8_t>((normal.z * 0.5f + 0.5f) * 255.0f);
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

//======================================================================================================================
// Deterministic diagnostic scene, entirely code-generated -- no fetched assets, no randomness.
// Exact placements (all on the Z=0 plane except the depth probes, which sit on the camera's
// forward axis):
//
//   Sphere grid: 5x5 unit-diameter spheres, 1.5-unit spacing, centred at the origin. Column c
//   (0..4, left to right) sweeps roughness 0.05->1.0; row r (0..4, bottom to top) sweeps
//   fresnelR0 0.04->1.0. Albedo white. Occupies x,y in [-3.5, 3.5].
//
//   Known-colour patches: 6 unit quads at y=-4.5, 1.2-unit spacing, x = (i-2.5)*1.2 for i=0..5,
//   albedo = srgbToLinear(red, green, blue, 18% gray (0.46), white, black) in that order.
//
//   Gradient ramp: one 6x1 quad at y=-6.0, textured with the 256x1 sRGB ramp above, albedo white.
//
//   Normal-map probe: one unit quad at y=-7.5, textured with the 64x64 normal map above.
//
//   Depth probes: three 0.5-unit cubes on the camera's forward axis (y = camera y = 0), at
//   world Z = cameraZ - distance for distance in {2, 10, 40}, laterally offset in X by
//   {-0.5, 0.0, 0.5} respectively so none hides another at the same screen pixel.
//
//   initialCamera: (0, 0, 12) looking down -Z (yaw=pitch=0), 45 degree vertical FOV -- frames the
//   sphere grid with margin. Nothing else is guaranteed to lie inside this frustum.
AssetResult<std::unique_ptr<Scene>> loadMaterialLabScene(rhi::Device& device) {
    auto scene = std::make_unique<Scene>();
    scene->name = "MaterialLab";

    glm::vec3 aabbMin{std::numeric_limits<float>::max()};
    glm::vec3 aabbMax{std::numeric_limits<float>::lowest()};
    const auto expandAabb = [&](const glm::vec3& center, const glm::vec3& halfExtent) {
        aabbMin = glm::min(aabbMin, center - halfExtent);
        aabbMax = glm::max(aabbMax, center + halfExtent);
    };

    auto sphereMesh = render::createMesh(device, render::fromGeo(makeSphere(0.5f, 32, 32)),
                                         "MaterialLab.sphereMesh");
    if (!sphereMesh) {
        return std::unexpected(uploadFailure(std::move(sphereMesh.error())));
    }
    const auto sphereMeshIndex = static_cast<uint32_t>(scene->meshes.size());
    scene->meshes.push_back(std::move(*sphereMesh));

    auto unitQuadMesh =
        render::createMesh(device, makeMaterialQuad(0.5f, 0.5f), "MaterialLab.unitQuadMesh");
    if (!unitQuadMesh) {
        return std::unexpected(uploadFailure(std::move(unitQuadMesh.error())));
    }
    const auto unitQuadMeshIndex = static_cast<uint32_t>(scene->meshes.size());
    scene->meshes.push_back(std::move(*unitQuadMesh));

    auto rampMesh =
        render::createMesh(device, makeMaterialQuad(3.0f, 0.5f), "MaterialLab.rampMesh");
    if (!rampMesh) {
        return std::unexpected(uploadFailure(std::move(rampMesh.error())));
    }
    const auto rampMeshIndex = static_cast<uint32_t>(scene->meshes.size());
    scene->meshes.push_back(std::move(*rampMesh));

    auto cubeMesh = render::createMesh(device, render::makeCube(), "MaterialLab.cubeMesh");
    if (!cubeMesh) {
        return std::unexpected(uploadFailure(std::move(cubeMesh.error())));
    }
    const auto cubeMeshIndex = static_cast<uint32_t>(scene->meshes.size());
    scene->meshes.push_back(std::move(*cubeMesh));

    // Sphere grid: roughness sweeps columns, fresnelR0 sweeps rows.
    constexpr int kGridSize = 5;
    constexpr float kGridSpacing = 1.5f;
    constexpr float kSphereRadius = 0.5f;
    for (int row = 0; row < kGridSize; ++row) {
        for (int col = 0; col < kGridSize; ++col) {
            render::Material material;
            material.albedo = srgbToLinear(glm::vec4(1.0f));
            material.roughness = 0.05f + static_cast<float>(col) * (1.0f - 0.05f) / (kGridSize - 1);
            material.fresnelR0 =
                glm::vec3(0.04f + static_cast<float>(row) * (1.0f - 0.04f) / (kGridSize - 1));
            const auto materialIndex = static_cast<uint32_t>(scene->materials.size());
            scene->materials.push_back(material);

            const glm::vec3 position{static_cast<float>(col - 2) * kGridSpacing,
                                     static_cast<float>(row - 2) * kGridSpacing, 0.0f};
            scene->objects.push_back(
                {.name = "material-lab sphere r" + std::to_string(row) + "c" + std::to_string(col),
                 .position = position,
                 .meshIndex = sphereMeshIndex,
                 .materialIndex = materialIndex});
            expandAabb(position, glm::vec3(kSphereRadius));
        }
    }

    // Known-colour patches: a row of 6 unit quads below the grid.
    struct PatchColor {
        const char* name;
        glm::vec3 srgb;
    };
    constexpr std::array<PatchColor, 6> kPatchColors = {{
        {"red", {1.0f, 0.0f, 0.0f}},
        {"green", {0.0f, 1.0f, 0.0f}},
        {"blue", {0.0f, 0.0f, 1.0f}},
        {"gray18", {0.46f, 0.46f, 0.46f}},
        {"white", {1.0f, 1.0f, 1.0f}},
        {"black", {0.0f, 0.0f, 0.0f}},
    }};
    constexpr float kPatchY = -4.5f;
    constexpr float kPatchSpacing = 1.2f;
    for (size_t i = 0; i < kPatchColors.size(); ++i) {
        render::Material material;
        material.albedo = srgbToLinear(glm::vec4(kPatchColors[i].srgb, 1.0f));
        const auto materialIndex = static_cast<uint32_t>(scene->materials.size());
        scene->materials.push_back(material);

        const glm::vec3 position{(static_cast<float>(i) - 2.5f) * kPatchSpacing, kPatchY, 0.0f};
        scene->objects.push_back({.name = std::string("material-lab patch ") + kPatchColors[i].name,
                                  .position = position,
                                  .meshIndex = unitQuadMeshIndex,
                                  .materialIndex = materialIndex});
        expandAabb(position, glm::vec3(0.5f, 0.5f, 0.0f));
    }

    // Gradient ramp: one 6x1 quad textured with the code-generated horizontal ramp.
    const std::vector<uint8_t> rampPixels = makeGradientRampPixels();
    const rhi::TextureMip rampMip{.data = rampPixels.data(), .bytesPerRow = uint64_t{256} * 4};
    auto rampTexture = device.createTexture({.width = 256,
                                             .height = 1,
                                             .format = rhi::Format::RGBA8Unorm_sRGB,
                                             .mipLevels = 1,
                                             .sampled = true,
                                             .label = "MaterialLab.rampTexture"},
                                            std::span(&rampMip, 1));
    if (!rampTexture) {
        return std::unexpected(uploadFailure(std::move(rampTexture.error())));
    }
    rhi::Texture* rampTexturePtr = rampTexture->get();
    scene->textures.push_back(std::move(*rampTexture));

    render::Material rampMaterial;
    rampMaterial.albedo = srgbToLinear(glm::vec4(1.0f));
    rampMaterial.diffuse = rampTexturePtr;
    const auto rampMaterialIndex = static_cast<uint32_t>(scene->materials.size());
    scene->materials.push_back(rampMaterial);

    constexpr float kRampY = -6.0f;
    const glm::vec3 rampPosition{0.0f, kRampY, 0.0f};
    scene->objects.push_back({.name = "material-lab gradient ramp",
                              .position = rampPosition,
                              .meshIndex = rampMeshIndex,
                              .materialIndex = rampMaterialIndex});
    expandAabb(rampPosition, glm::vec3(3.0f, 0.5f, 0.0f));

    // Normal-map probe: one unit quad textured with the code-generated hemispherical bump.
    const std::vector<uint8_t> normalPixels = makeNormalMapPixels();
    const rhi::TextureMip normalMip{.data = normalPixels.data(), .bytesPerRow = uint64_t{64} * 4};
    auto normalTexture = device.createTexture({.width = 64,
                                               .height = 64,
                                               .format = rhi::Format::RGBA8Unorm,
                                               .mipLevels = 1,
                                               .sampled = true,
                                               .label = "MaterialLab.normalMapTexture"},
                                              std::span(&normalMip, 1));
    if (!normalTexture) {
        return std::unexpected(uploadFailure(std::move(normalTexture.error())));
    }
    rhi::Texture* normalTexturePtr = normalTexture->get();
    scene->textures.push_back(std::move(*normalTexture));

    render::Material normalMaterial;
    normalMaterial.albedo = srgbToLinear(glm::vec4(1.0f));
    normalMaterial.normalMap = normalTexturePtr;
    const auto normalMaterialIndex = static_cast<uint32_t>(scene->materials.size());
    scene->materials.push_back(normalMaterial);

    constexpr float kNormalProbeY = -7.5f;
    const glm::vec3 normalProbePosition{0.0f, kNormalProbeY, 0.0f};
    scene->objects.push_back({.name = "material-lab normal probe",
                              .position = normalProbePosition,
                              .meshIndex = unitQuadMeshIndex,
                              .materialIndex = normalMaterialIndex});
    expandAabb(normalProbePosition, glm::vec3(0.5f, 0.5f, 0.0f));

    // Depth probes: three 0.5-unit cubes on the camera's forward axis at known distances,
    // laterally offset so none hides another at the same screen pixel.
    constexpr float kCameraDistance = 12.0f;
    render::Material depthProbeMaterial; // default albedo/roughness/fresnel
    const auto depthProbeMaterialIndex = static_cast<uint32_t>(scene->materials.size());
    scene->materials.push_back(depthProbeMaterial);

    struct DepthProbe {
        const char* name;
        float distance;
        float lateralOffset;
    };
    constexpr std::array<DepthProbe, 3> kDepthProbes = {{
        {"near", 2.0f, -0.5f},
        {"mid", 10.0f, 0.0f},
        {"far", 40.0f, 0.5f},
    }};
    for (const DepthProbe& probe : kDepthProbes) {
        const glm::vec3 position{probe.lateralOffset, 0.0f, kCameraDistance - probe.distance};
        scene->objects.push_back({.name = std::string("material-lab depth probe ") + probe.name,
                                  .position = position,
                                  .scale = glm::vec3(0.5f),
                                  .meshIndex = cubeMeshIndex,
                                  .materialIndex = depthProbeMaterialIndex});
        expandAabb(position, glm::vec3(0.25f));
    }

    const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
    scene->boundingSphere = glm::vec4(center, glm::length(aabbMax - center));

    if (auto sky = attachSkyAndLights(device, *scene, "MaterialLab"); !sky) {
        return std::unexpected(sky.error());
    }

    scene->initialCamera.position = {0.0f, 0.0f, kCameraDistance};
    scene->initialCamera.yaw = 0.0f;
    scene->initialCamera.pitch = 0.0f;
    scene->initialCamera.fovY = glm::radians(45.0f);
    scene->initialCamera.nearZ = 0.1f;
    scene->initialCamera.farZ = 100.0f;

    return scene;
}

} // namespace lmx::engine
