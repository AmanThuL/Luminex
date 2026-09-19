//----------------------------------------------------------------------------------------------------------------------
/// @file LightLab.cpp
/// @brief Builds LightLab's material field, scalable light population, and looping camera rail.
//----------------------------------------------------------------------------------------------------------------------

#include "Scene/LightLab.h"

#include "Scene/Scene.h"

#include "Asset/GeometryGenerator.h"
#include "Core/Assert.h"
#include "Core/Color.h"
#include "Render/Mesh.h"
#include "Scene/SceneEnvironment.h"

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace lmx::scene {

namespace {

// Material field: a fixed, n-independent strip of pillars and spheres spanning the material
// range, in front of a matte floor. Column c in [0, kFieldColumnCount) sweeps roughness across
// the low half and repeats it metallic across the high half, so both dielectric and metal
// responses are represented at every roughness step.
constexpr uint32_t kFieldColumnCount = 8;
constexpr float kFieldSpacing = 2.5f;
constexpr float kPillarHalfHeight = 1.6f;
constexpr float kPillarHalfWidth = 0.6f;
constexpr float kSphereRadius = 0.9f;
constexpr float kPillarRowZ = 3.5f;
constexpr float kSphereRowZ = -3.5f;
constexpr float kFloorHalfExtent = kLightLabGridHalfExtent + 12.0f;

// Light population.
// Keep the floor inside every light's range as density grows.
constexpr float kMinLightHeightFraction = 0.35f;
constexpr float kMaxLightHeightFraction = 0.65f;
constexpr float kPileHeight = 5.5f;
constexpr float kSpotInnerCone = 12.0f * glm::pi<float>() / 180.0f;
constexpr float kSpotOuterCone = 30.0f * glm::pi<float>() / 180.0f;
constexpr float kSpotTilt = 0.3f; // Deterministic tilt magnitude away from straight down.
constexpr float kLightIntensity = 25.0f;
constexpr float kOrbitRadiusFraction = 0.3f; // Of the grid spacing at the requested n.
// An irrational-feeling stride keeps successive orbiting lights' phases from lining up, without
// needing a second RNG stream.
constexpr float kOrbitPhaseStep = 0.73f;
constexpr float kPileRange = 18.0f; // Large enough that every pile light shares its froxels.
constexpr float kLightSurfaceClearance = 0.25f;

//======================================================================================================================
// Keep each entire horizontal orbit outside the material field. A punctual emitter crossing a
// solid surface jumps from the back hemisphere into the inverse-square near field. Moving its
// centre out of the row's expanded bounds prevents that authoring singularity without changing
// attenuation, range, intensity, height, or the orbit's speed.
glm::vec3 clearMaterialField(glm::vec3 centre, float orbitRadius) {
    const float margin = orbitRadius + kLightSurfaceClearance;
    const float halfSpan = float(kFieldColumnCount - 1) * kFieldSpacing * 0.5f;
    const auto clearRow = [&](float rowZ, float halfWidth, float height) {
        if (centre.y > height + kLightSurfaceClearance ||
            std::abs(centre.x) > halfSpan + halfWidth + margin) {
            return;
        }
        const float extent = halfWidth + margin;
        if (std::abs(centre.z - rowZ) < extent) {
            centre.z = rowZ + (centre.z < rowZ ? -extent : extent);
        }
    };
    clearRow(kPillarRowZ, kPillarHalfWidth, 2.0f * kPillarHalfHeight);
    clearRow(kSphereRowZ, kSphereRadius, 2.0f * kSphereRadius);
    return centre;
}

//======================================================================================================================
uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

//======================================================================================================================
// Signed jitter in [-0.5, 0.5].
float jitter(uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xffffu) / 65535.0f - 0.5f;
}

//======================================================================================================================
// Unsigned jitter in [0, 1].
float unitJitter(uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xffffu) / 65535.0f;
}

//======================================================================================================================
// The grid side length lightLabLights and lightLabTracks both derive n's jittered positions from;
// factored out so the two stay in exact agreement.
uint32_t gridSide(uint32_t n) {
    return static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(n))));
}

//======================================================================================================================
std::array<glm::vec3, 4> lightPalette() {
    return {
        srgbToLinear(glm::vec3(1.00f, 0.86f, 0.66f)), srgbToLinear(glm::vec3(0.55f, 0.76f, 1.00f)),
        srgbToLinear(glm::vec3(1.00f, 0.55f, 0.35f)), srgbToLinear(glm::vec3(0.72f, 0.48f, 1.00f))};
}

//======================================================================================================================
asset::AssetError uploadFailure(rojoRHI::Error error) {
    return asset::AssetError{asset::AssetErrorCode::UploadFailed, std::move(error.message)};
}

//======================================================================================================================
// Adds the fixed pillar/sphere/floor field spanning the material range; returns the scene's field
// bounding box so the caller can fit a bounding sphere around the renderable geometry.
void addMaterialField(Scene& scene, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    MaterialRecord floorMaterial;
    floorMaterial.albedo = srgbToLinear(glm::vec4(0.5f, 0.5f, 0.52f, 1.0f));
    floorMaterial.roughness = 0.85f;
    floorMaterial.metallic = 0.0f;
    const MaterialId floorMaterialId = scene.addMaterial(floorMaterial);
    const MeshId floorMesh = scene.addMesh(render::makePlane(kFloorHalfExtent), "LightLab.floor");
    scene.addObject({.name = "Floor", .mesh = floorMesh, .material = floorMaterialId});
    boundsMin = glm::min(boundsMin, glm::vec3(-kFloorHalfExtent, 0.0f, -kFloorHalfExtent));
    boundsMax = glm::max(boundsMax, glm::vec3(kFloorHalfExtent, 0.0f, kFloorHalfExtent));

    const MeshId pillarMesh = scene.addMesh(render::makeCube(), "LightLab.pillar");
    const MeshId sphereMesh =
        scene.addMesh(render::fromGeo(asset::makeSphere(kSphereRadius, 24, 16)), "LightLab.sphere");
    const std::array<glm::vec3, 4> palette = lightPalette();

    std::array<MaterialId, kFieldColumnCount> materials;
    for (uint32_t c = 0; c < kFieldColumnCount; ++c) {
        MaterialRecord material;
        material.albedo = glm::vec4(palette[c % palette.size()], 1.0f);
        const uint32_t roughnessIndex = c % (kFieldColumnCount / 2);
        material.roughness = 0.08f + static_cast<float>(roughnessIndex) * (1.0f - 0.08f) /
                                         static_cast<float>(kFieldColumnCount / 2 - 1);
        material.metallic = c < kFieldColumnCount / 2 ? 0.0f : 1.0f;
        materials[c] = scene.addMaterial(material);
    }

    const float span = static_cast<float>(kFieldColumnCount - 1) * kFieldSpacing;
    for (uint32_t c = 0; c < kFieldColumnCount; ++c) {
        const float x = static_cast<float>(c) * kFieldSpacing - span * 0.5f;
        const glm::vec3 pillarPosition(x, kPillarHalfHeight, kPillarRowZ);
        scene.addObject({.name = "Pillar " + std::to_string(c),
                         .position = pillarPosition,
                         .scale = glm::vec3(kPillarHalfWidth * 2.0f, kPillarHalfHeight * 2.0f,
                                            kPillarHalfWidth * 2.0f),
                         .mesh = pillarMesh,
                         .material = materials[c]});
        boundsMin =
            glm::min(boundsMin, pillarPosition - glm::vec3(kPillarHalfWidth, kPillarHalfHeight,
                                                           kPillarHalfWidth));
        boundsMax =
            glm::max(boundsMax, pillarPosition + glm::vec3(kPillarHalfWidth, kPillarHalfHeight,
                                                           kPillarHalfWidth));

        const glm::vec3 spherePosition(x, kSphereRadius, kSphereRowZ);
        scene.addObject({.name = "Sphere " + std::to_string(c),
                         .position = spherePosition,
                         .mesh = sphereMesh,
                         .material = materials[kFieldColumnCount - 1 - c]});
        boundsMin = glm::min(boundsMin, spherePosition - glm::vec3(kSphereRadius));
        boundsMax = glm::max(boundsMax, spherePosition + glm::vec3(kSphereRadius));
    }
}

} // namespace

//======================================================================================================================
glm::vec3 lightLabPilePosition() {
    return glm::vec3(0.0f, kPileHeight, 0.0f);
}

//======================================================================================================================
float lightLabRange(uint32_t n) {
    LMX_ASSERT(n >= 1, "lightLabRange requires n >= 1");
    return kLightLabReferenceRange *
           std::sqrt(static_cast<float>(kLightLabReferenceLightCount) / static_cast<float>(n));
}

//======================================================================================================================
std::vector<render::LocalLight> lightLabLights(uint32_t n, uint32_t pile) {
    LMX_ASSERT(n >= 1 && static_cast<uint64_t>(n) + pile <= render::kMaxLocalLights,
               "LightLab requires 1..kMaxLocalLights total lights");
    std::vector<render::LocalLight> lights;
    lights.reserve(static_cast<size_t>(n) + pile);

    const float range = lightLabRange(n);
    const uint32_t side = gridSide(n);
    const float spacing = (2.0f * kLightLabGridHalfExtent) / static_cast<float>(side);
    const std::array<glm::vec3, 4> palette = lightPalette();

    uint32_t seed = 0x4c494748u; // "LIGH", a fixed non-zero xorshift seed.
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t column = i % side;
        const uint32_t row = i / side;
        const float x =
            (static_cast<float>(column) - static_cast<float>(side - 1) * 0.5f) * spacing +
            jitter(seed) * spacing * 0.3f;
        const float z = (static_cast<float>(row) - static_cast<float>(side - 1) * 0.5f) * spacing +
                        jitter(seed) * spacing * 0.3f;
        const float y =
            range * (kMinLightHeightFraction +
                     unitJitter(seed) * (kMaxLightHeightFraction - kMinLightHeightFraction));

        render::LocalLight light;
        const float orbitRadius = i % 4 == 2 ? spacing * kOrbitRadiusFraction : 0.0f;
        light.position = clearMaterialField(glm::vec3(x, y, z), orbitRadius);
        light.colour = palette[i % palette.size()];
        // Height scales with range; scale intensity by range squared to preserve floor irradiance.
        light.intensity = kLightIntensity * (static_cast<float>(kLightLabReferenceLightCount) /
                                             static_cast<float>(n));
        light.range = range;
        if (i % 4 == 1) {
            light.type = render::LocalLightType::Spot;
            const float tiltX = jitter(seed) * kSpotTilt;
            const float tiltZ = jitter(seed) * kSpotTilt;
            light.direction = glm::normalize(glm::vec3(tiltX, -1.0f, tiltZ));
            light.innerCone = kSpotInnerCone;
            light.outerCone = kSpotOuterCone;
        } else {
            light.type = render::LocalLightType::Point;
        }
        lights.push_back(light);
    }

    for (uint32_t p = 0; p < pile; ++p) {
        render::LocalLight light;
        light.type = render::LocalLightType::Point;
        light.position = lightLabPilePosition();
        light.colour = palette[p % palette.size()];
        light.intensity = kLightIntensity;
        light.range = kPileRange;
        lights.push_back(light);
    }
    return lights;
}

//======================================================================================================================
std::vector<asset::LightOrbitTrack> lightLabTracks(uint32_t n, uint32_t pile) {
    const std::vector<render::LocalLight> lights = lightLabLights(n, pile);
    const uint32_t side = gridSide(n);
    const float spacing = (2.0f * kLightLabGridHalfExtent) / static_cast<float>(side);

    std::vector<asset::LightOrbitTrack> tracks;
    for (uint32_t i = 0; i < n; ++i) {
        if (i % 4 != 2) {
            continue;
        }
        asset::LightOrbitTrack track;
        track.light = i;
        track.centre = lights[i].position;
        track.axis = glm::vec3(0.0f, 1.0f, 0.0f);
        track.radius = spacing * kOrbitRadiusFraction;
        track.phase = std::fmod(static_cast<float>(i) * kOrbitPhaseStep, glm::two_pi<float>());
        track.period = kLightLabOrbitPeriod;
        tracks.push_back(track);
    }
    return tracks;
}

//======================================================================================================================
std::vector<asset::CameraKey> lightLabCameraTrack() {
    std::vector<asset::CameraKey> keys;
    constexpr float kRailHeight = 12.0f;
    constexpr float kRailDepth = 4.0f;
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    const glm::vec3 closePoint(0.0f, 6.0f, 2.0f);
    const glm::vec3 farPoint(0.0f, kRailHeight, kRailDepth);
    const size_t keyCount =
        static_cast<size_t>(kLightLabRailDuration * asset::kAnimationBakeRate) + 1;
    keys.reserve(keyCount);
    for (size_t i = 0; i < keyCount; ++i) {
        const double time = static_cast<double>(i) / asset::kAnimationBakeRate;
        const float phase = static_cast<float>(time / kLightLabRailDuration) * glm::two_pi<float>();
        // A close pass near the pile point at the loop's midpoint, easing out to an overview of
        // the material field at its endpoints. Looking down at the floor keeps the dense
        // grid distributed across screen tiles instead of compressed into a distant band.
        const float fraction = (1.0f + std::cos(phase)) * 0.5f;
        const glm::vec3 position = glm::mix(closePoint, farPoint, fraction);
        const glm::vec3 direction = glm::normalize(target - position);
        keys.push_back({.time = time,
                        .position = position,
                        .yaw = std::atan2(direction.x, -direction.z),
                        .pitch = std::asin(direction.y)});
    }
    return keys;
}

//======================================================================================================================
asset::AssetResult<std::unique_ptr<Scene>>
loadLightLabScene(rojoRHI::Device& device, uint32_t lightCount, uint32_t pileCount) {
    if (lightCount == 0 || lightCount > render::kMaxLocalLights) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                 "LightLab lights must be 1.." +
                                                     std::to_string(render::kMaxLocalLights)});
    }
    if (static_cast<uint64_t>(lightCount) + pileCount > render::kMaxLocalLights) {
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Malformed, "LightLab lights plus light pile must not exceed " +
                                                  std::to_string(render::kMaxLocalLights)});
    }

    auto scene = std::make_unique<Scene>();
    scene->name = "LightLab";
    scene->lightLabGridCount = lightCount;

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    addMaterialField(*scene, boundsMin, boundsMax);

    for (const render::LocalLight& light : lightLabLights(lightCount, pileCount)) {
        const auto added = scene->addLight(light);
        LMX_ASSERT(added.has_value(), "LightLab authored an invalid light");
    }
    scene->animation.lightTracks = lightLabTracks(lightCount, pileCount);

    scene->animation.cameraTrack = lightLabCameraTrack();
    scene->animation.duration = kLightLabRailDuration;
    scene->animation.loop = true;

    scene->animate(0.0);
    scene->resetMotion();

    const glm::vec3 centre = (boundsMin + boundsMax) * 0.5f;
    scene->boundingSphere = glm::vec4(centre, glm::length(boundsMax - centre));
    const asset::CameraKey first = scene->animation.cameraTrack.front();
    scene->initialCamera = {.position = first.position,
                            .yaw = first.yaw,
                            .pitch = first.pitch,
                            .fovY = kLightLabCameraFovY,
                            .nearZ = kLightLabCameraNearZ,
                            .farZ = 300.0f};

    if (auto environment = attachNeutralEnvironment(device, *scene, "LightLab"); !environment) {
        return std::unexpected(environment.error());
    }
    if (auto finalized = scene->finalize(device); !finalized) {
        return std::unexpected(uploadFailure(std::move(finalized.error())));
    }
    return scene;
}

} // namespace lmx::scene
