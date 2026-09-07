//----------------------------------------------------------------------------------------------------------------------
/// @file MaterialLab.cpp
/// @brief Builds the deterministic material-diagnostic scene.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene.h"

#include "Core/Log.h"
#include "Engine/Color.h"
#include "Engine/GeometryGenerator.h"
#include "Engine/HdrEnvironment.h"
#include "Engine/Ibl.h"
#include "Engine/SceneEnvironment.h"
#include "Engine/TextureBake.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::engine {

namespace {

// Stable lane centres make the scene readable as a horizontal strip: open on the material model,
// then pan right without rotating to inspect authored-colour/texture and depth diagnostics.
constexpr float kMaterialLaneX = 0.0f;
constexpr float kTextureLaneX = 14.0f;
constexpr float kDepthLaneX = 28.0f;
constexpr float kCameraDistance = 12.0f;
constexpr std::string_view kStudioEnvironmentPath =
    "Assets/Fetched/MaterialLab/studio_small_09_1k.hdr";
constexpr uint32_t kStudioEnvironmentFaceSize = 32;
constexpr float kStudioEnvironmentYaw = 0.0f;
constexpr float kStudioEnvironmentScale = 0.25f;

//======================================================================================================================
// Executables run below the repository root, so optional lookdev assets are found by walking up
// from the working directory. MaterialLab remains available when the search reaches the root.
std::optional<std::filesystem::path> findRepoAsset(std::string_view relative) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::path candidate = dir / relative; std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

//======================================================================================================================
AssetError uploadFailure(rhi::Error error) {
    return AssetError{AssetErrorCode::UploadFailed, std::move(error.message)};
}

//======================================================================================================================
AssetResult<void> attachStudioEnvironment(rhi::Device& device, Scene& scene,
                                          std::string_view label) {
    // The fetched studio is deliberately optional: a fresh checkout and hosted CI still get a
    // deterministic scene, while `xmake setup` upgrades both the visible sky and its IBL from the
    // exact same linear-light cubemap. The fallback is an authored neutral mid-gray bright enough
    // to keep the roughness sweep readable without the studio asset.
    ibl::CpuCubemap environment = ibl::makeConstantCubemap(srgbToLinear(glm::vec3(0.65f)), 1);
    bool usingStudioEnvironment = false;
    if (const auto path = findRepoAsset(kStudioEnvironmentPath)) {
        auto decoded = loadRadianceHdr(path->string());
        if (decoded) {
            auto converted =
                equirectangularToCubemap(*decoded, kStudioEnvironmentFaceSize,
                                         kStudioEnvironmentYaw, kStudioEnvironmentScale);
            if (converted) {
                environment = std::move(*converted);
                usingStudioEnvironment = true;
            } else {
                LMX_LOG_WARN("{}; using MaterialLab's neutral fallback environment",
                             converted.error().message);
            }
        } else {
            LMX_LOG_WARN("{}; using MaterialLab's neutral fallback environment",
                         decoded.error().message);
        }
    } else {
        LMX_LOG_WARN("MaterialLab environment '{}' was not found; run `xmake setup` to fetch the "
                     "CC0 studio HDRI. Using the neutral fallback environment.",
                     kStudioEnvironmentPath);
    }

    auto cubemap = ibl::uploadCubemap(device, environment, std::string(label) + ".sky");
    if (!cubemap) {
        return std::unexpected(uploadFailure(std::move(cubemap.error())));
    }

    // Studio Small 09 already contains its softboxes. The analytic rig exists only to keep the
    // asset-free fallback useful instead of double-lighting the fetched environment.
    return attachEnvironment(device, scene, std::move(*cubemap), environment,
                             !usingStudioEnvironment, label);
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
// Rounds rather than truncates: a flat normal's x/y component is exactly 0.5 before scaling to
// [0,255], and truncation would encode 127 instead of the intended 128.
uint8_t encodeUnitToByte(float unitComponent) {
    return static_cast<uint8_t>(unitComponent * 255.0f + 0.5f);
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
            pixels[offset + 0] = encodeUnitToByte(normal.x * 0.5f + 0.5f);
            pixels[offset + 1] = encodeUnitToByte(normal.y * 0.5f + 0.5f);
            pixels[offset + 2] = encodeUnitToByte(normal.z * 0.5f + 0.5f);
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

//======================================================================================================================
// 64x64, one texel per checker, alternating black/white by (x+y) parity -- deliberately the
// highest possible spatial frequency a box filter can still resolve. Every 2x2 block therefore
// contains exactly two black and two white texels, so box-filtering it converges to an exact
// uniform mid-gray from mip level 1 onward (Source/Engine/TextureBake.h's bakeMips); point-picking
// instead of filtering samples the same parity every time it steps by a power of two, producing a
// solid black or solid white mip instead. RGBA8Unorm_sRGB -- colour, not data, so it goes through
// the same sRGB decode/filter/encode path as an authored base-color texture.
std::vector<uint8_t> makeCheckerboardPixels() {
    constexpr uint32_t kSize = 64;
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
            const size_t offset = (size_t{y} * kSize + x) * 4;
            pixels[offset + 0] = pixels[offset + 1] = pixels[offset + 2] = v;
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

//======================================================================================================================
// Deterministic diagnostic geometry with an optional fetched studio environment and no randomness.
// Exact placements form three horizontal diagnostic lanes. The camera keeps the same Y, Z, yaw,
// pitch, and FOV for every lane, so A/D movement alone is enough to move between them:
//
//   Material lane (X=0): 5x5 unit-diameter spheres, 1.5-unit spacing, centred at the origin. Column
//   c (0..4, left to right) sweeps perceptual roughness 0.05->1.0; row r (0..4, bottom to top)
//   sweeps metallic 0->1. Albedo white. Occupies x,y in [-3.5, 3.5].
//
//   Those are the metallic-roughness model's own two axes, so the grid reads as the material space
//   it parameterizes: the bottom row is a white dielectric losing its highlight to roughness, the
//   top row is a white conductor whose reflection blurs across the same sweep, and the rows between
//   are the (physically unrealisable) mix the parameter admits. Both extreme rows are exactly the
//   furnace test's two probes, so what that test measures is what this grid shows.
//
//   Texture lane (X=14): 6 unit known-colour patches at y=1.5, 1.2-unit spacing, local
//   x = (i-2.5)*1.2 for i=0..5, albedo = srgbToLinear(red, green, blue, 18% gray (0.46), white,
//   black) in that order. The 6x1 gradient ramp is centred at y=0 and the normal-map probe at
//   y=-1.5, keeping the complete lane inside the same framing as the sphere grid.
//
//   Depth lane (X=28): three 0.5-unit cubes on the camera's forward axis (y = camera y = 0), at
//   world Z = cameraZ - distance for distance in {2, 10, 40}, laterally offset in X by
//   laneX + {-0.4, 1.0, 7.0} respectively. Offsets are sized in screen (tangent) space, not world
//   space, and accounting for the *nearest* face of each 0.5-unit cube, not its centre: a corner at
//   world offset o and distance d subtends tangent o/d, and the cube's own half-extent (0.25)
//   shifts both o and d toward the camera on its nearest corner, so the worst-case tangent is
//   (|offset|+0.25)/(distance-0.25), materially more than the naive offset/distance. The near
//   probe (distance 2) is by far the most constrained: its 0.25-unit half-extent is 12.5% of its
//   own distance, so the same 0.5 world-unit offset that comfortably separates the far probe
//   (distance 40) both clips the frustum edge and fails to clear the grid at distance 2. The
//   three offsets above put each probe in its own non-overlapping tangent band with margin;
//   Tests/EngineSceneTests.cpp verifies this by projecting each object's exact world AABB (all 8
//   corners) after a horizontal camera pan, not just its centre.
//
//   Mip probe: one unit quad at world Z = cameraZ + 15 (X = Y = 0), textured with the 64x64
//   checkerboard above. Behind initialCamera's default view on purpose -- see
//   makeCheckerboardPixels
//   -- a dedicated GPU test supplies its own camera on the far side to read it minified.
//
//   initialCamera: (0, 0, 12) looking down -Z (yaw=pitch=0), 45 degree vertical FOV. The complete
//   sphere matrix occupies roughly 70% of a square viewport's height while retaining comfortable
//   edge clearance. The texture and depth lanes use the identical pose translated to their lane X.
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

    // Sphere grid: perceptual roughness sweeps columns, metallic sweeps rows.
    constexpr int kGridSize = 5;
    constexpr float kGridSpacing = 1.5f;
    constexpr float kSphereRadius = 0.5f;
    for (int row = 0; row < kGridSize; ++row) {
        for (int col = 0; col < kGridSize; ++col) {
            render::Material material;
            material.albedo = srgbToLinear(glm::vec4(1.0f));
            material.roughness = 0.05f + static_cast<float>(col) * (1.0f - 0.05f) / (kGridSize - 1);
            material.metallic = static_cast<float>(row) / (kGridSize - 1);
            const auto materialIndex = static_cast<uint32_t>(scene->materials.size());
            scene->materials.push_back(material);

            const glm::vec3 position{kMaterialLaneX + static_cast<float>(col - 2) * kGridSpacing,
                                     static_cast<float>(row - 2) * kGridSpacing, 0.0f};
            scene->objects.push_back(
                {.name = "material-lab sphere r" + std::to_string(row) + "c" + std::to_string(col),
                 .position = position,
                 .meshIndex = sphereMeshIndex,
                 .materialIndex = materialIndex});
            expandAabb(position, glm::vec3(kSphereRadius));
        }
    }

    // Texture lane: a row of 6 known-colour patches above the gradient and normal-map probes.
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
    constexpr float kPatchY = 1.5f;
    constexpr float kPatchSpacing = 1.2f;
    for (size_t i = 0; i < kPatchColors.size(); ++i) {
        render::Material material;
        material.albedo = srgbToLinear(glm::vec4(kPatchColors[i].srgb, 1.0f));
        const auto materialIndex = static_cast<uint32_t>(scene->materials.size());
        scene->materials.push_back(material);

        const glm::vec3 position{kTextureLaneX + (static_cast<float>(i) - 2.5f) * kPatchSpacing,
                                 kPatchY, 0.0f};
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

    constexpr float kRampY = 0.0f;
    const glm::vec3 rampPosition{kTextureLaneX, kRampY, 0.0f};
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

    constexpr float kNormalProbeY = -1.5f;
    const glm::vec3 normalProbePosition{kTextureLaneX, kNormalProbeY, 0.0f};
    scene->objects.push_back({.name = "material-lab normal probe",
                              .position = normalProbePosition,
                              .meshIndex = unitQuadMeshIndex,
                              .materialIndex = normalMaterialIndex});
    expandAabb(normalProbePosition, glm::vec3(0.5f, 0.5f, 0.0f));

    // Depth probes: three 0.5-unit cubes on the camera's forward axis at known distances. Lateral
    // offsets are world-space numbers sized per-probe (accounting for each cube's nearest-corner
    // amplification) so each lands in its own tangent-space band alongside the grid's -- see the
    // file-level comment above for the derivation and the corresponding test in
    // Tests/EngineSceneTests.cpp that verifies it in screen space.
    render::Material depthProbeMaterial; // default albedo/roughness/fresnel
    const auto depthProbeMaterialIndex = static_cast<uint32_t>(scene->materials.size());
    scene->materials.push_back(depthProbeMaterial);

    struct DepthProbe {
        const char* name;
        float distance;
        float lateralOffset;
    };
    constexpr std::array<DepthProbe, 3> kDepthProbes = {{
        {"near", 2.0f, -0.4f},
        {"mid", 10.0f, 1.0f},
        {"far", 40.0f, 7.0f},
    }};
    for (const DepthProbe& probe : kDepthProbes) {
        const glm::vec3 position{kDepthLaneX + probe.lateralOffset, 0.0f,
                                 kCameraDistance - probe.distance};
        scene->objects.push_back({.name = std::string("material-lab depth probe ") + probe.name,
                                  .position = position,
                                  .scale = glm::vec3(0.5f),
                                  .meshIndex = cubeMeshIndex,
                                  .materialIndex = depthProbeMaterialIndex});
        expandAabb(position, glm::vec3(0.25f));
    }

    // Mip-filtering probe: a 64x64 1-texel checkerboard, its full chain baked in memory by the
    // same deterministic box filter Tools/TextureBake bakes to disk (Source/Engine/TextureBake.h)
    // -- proof that a GPU test's minified read converges to mid-gray because mips came from
    // filtering, not point-picking (a point-picked mip of a 1-texel checkerboard reads solid black
    // or solid white instead). Positioned behind initialCamera's default view (world Z beyond the
    // camera itself, which looks toward -Z) so it never appears in the default frame;
    // Tests/EngineSceneTests.cpp's mip-check test supplies its own camera on the +Z side to see it.
    const std::vector<uint8_t> checkerPixels = makeCheckerboardPixels();
    const BakedMipChain checkerChain = bakeMips(checkerPixels, 64, 64, BakeMode::Srgb);
    auto checkerTexture = device.createTexture({.width = checkerChain.width,
                                                .height = checkerChain.height,
                                                .format = rhi::Format::RGBA8Unorm_sRGB,
                                                .mipLevels = checkerChain.mipLevels,
                                                .sampled = true,
                                                .label = "MaterialLab.mipCheckerboard"},
                                               checkerChain.mips);
    if (!checkerTexture) {
        return std::unexpected(uploadFailure(std::move(checkerTexture.error())));
    }
    rhi::Texture* checkerTexturePtr = checkerTexture->get();
    scene->textures.push_back(std::move(*checkerTexture));

    render::Material checkerMaterial;
    checkerMaterial.albedo = srgbToLinear(glm::vec4(1.0f));
    checkerMaterial.diffuse = checkerTexturePtr;
    const auto checkerMaterialIndex = static_cast<uint32_t>(scene->materials.size());
    scene->materials.push_back(checkerMaterial);

    constexpr float kMipProbeZ = kCameraDistance + 15.0f;
    const glm::vec3 mipProbePosition{0.0f, 0.0f, kMipProbeZ};
    scene->objects.push_back({.name = "material-lab mip probe",
                              .position = mipProbePosition,
                              .meshIndex = unitQuadMeshIndex,
                              .materialIndex = checkerMaterialIndex});
    expandAabb(mipProbePosition, glm::vec3(0.5f, 0.5f, 0.0f));

    const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
    scene->boundingSphere = glm::vec4(center, glm::length(aabbMax - center));

    if (auto sky = attachStudioEnvironment(device, *scene, "MaterialLab"); !sky) {
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
