//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalLab.cpp
/// @brief Builds the deterministic temporal-diagnostic scene.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Asset/GeometryGenerator.h"
#include "Asset/SceneAnimation.h"
#include "Asset/TextureBake.h"
#include "Core/Color.h"
#include "Scene/SceneEnvironment.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::scene {

namespace {

// Every motion below is periodic, and the clip is their common period: 24 s is a whole number of
// cube turns (4 s), sphere orbits (6 s), pole oscillations (2 s) and camera loops (8 s), so the
// clip's last key reproduces its first exactly and looping introduces no discontinuity.
constexpr double kClipDuration = 24.0;
constexpr size_t kKeyCount = static_cast<size_t>(kClipDuration * asset::kAnimationBakeRate) + 1;

constexpr float kFloorHalfExtent = 10.0f;
constexpr uint32_t kCheckerSquares = 8;
constexpr uint32_t kCheckerTexelsPerSquare = 64;
constexpr uint32_t kCheckerSize = kCheckerSquares * kCheckerTexelsPerSquare;
constexpr glm::vec3 kRotatingCubePosition{-3.0f, 1.0f, 0.0f};
constexpr double kCubeTurnSeconds = 4.0;
constexpr glm::vec3 kReferenceCubePosition{3.0f, 1.0f, 0.0f};
constexpr float kOrbitRadius = 2.0f;
constexpr float kOrbitSphereRadius = 0.4f;
constexpr double kOrbitSeconds = 6.0;
constexpr float kPoleX[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
constexpr float kPoleWidth = 0.05f;
constexpr float kPoleHeight = 3.0f;
constexpr float kPoleZ = -4.0f;
constexpr float kPoleSwing = 0.5f;
constexpr double kPoleSwingSeconds = 2.0;
constexpr glm::vec3 kInvalidCubePosition{0.0f, 1.0f, 4.0f};
constexpr glm::vec3 kSignPosition{0.0f, 2.5f, -6.0f};
constexpr glm::vec2 kSignSize{1.5f, 0.75f};
constexpr glm::vec3 kSignEmissive{1.0f, 0.8f, 0.3f};
constexpr double kSignPeriodSeconds = 6.0;
constexpr float kSignStrengthOn = 4.0f;
constexpr glm::vec3 kCameraStart{0.0f, 3.0f, 10.0f};
constexpr glm::vec3 kCameraEnd{2.0f, 3.0f, 8.0f};
constexpr float kCameraYawDrift = 0.1f;
constexpr float kCameraPitch = -0.15f;
constexpr double kCameraLoopSeconds = 8.0;

//======================================================================================================================
asset::AssetError uploadFailure(rojoRHI::Error error) {
    return asset::AssetError{asset::AssetErrorCode::UploadFailed, std::move(error.message)};
}

//======================================================================================================================
// A black-and-white checker of kCheckerSquares squares per axis, each kCheckerTexelsPerSquare
// texels wide so the square edges stay crisp under the floor's heavy magnification. Stretched
// across the floor's 0..1 UVs it draws 8 by 8 squares of 2.5 world units, the high-frequency
// ground pattern reprojection error is easiest to read on.
std::vector<uint8_t> makeCheckerPixels() {
    std::vector<uint8_t> pixels(size_t{kCheckerSize} * kCheckerSize * 4);
    for (uint32_t y = 0; y < kCheckerSize; ++y) {
        for (uint32_t x = 0; x < kCheckerSize; ++x) {
            const uint8_t value =
                ((x / kCheckerTexelsPerSquare + y / kCheckerTexelsPerSquare) % 2 == 0) ? 235 : 40;
            const size_t offset = (size_t{y} * kCheckerSize + x) * 4;
            pixels[offset + 0] = pixels[offset + 1] = pixels[offset + 2] = value;
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

//======================================================================================================================
// Bakes one closed-form pose into a looping track at the shared key rate. Keys are authored rather
// than evaluated per frame so a TemporalLab draw and a glTF-backed draw take the same sampling
// path.
template <typename Pose>
asset::RigidTrack makeTrack(uint32_t objectIndex, Pose pose) {
    asset::RigidTrack track;
    track.objectIndex = objectIndex;
    track.keys.reserve(kKeyCount);
    for (size_t i = 0; i < kKeyCount; ++i) {
        const double time = static_cast<double>(i) / asset::kAnimationBakeRate;
        asset::RigidKey key = pose(time);
        key.time = time;
        track.keys.push_back(key);
    }
    return track;
}

//======================================================================================================================
// Phase in radians of a motion whose period is `seconds`, at `time` seconds.
float phase(double time, double seconds) {
    return static_cast<float>(2.0 * glm::pi<double>() * time / seconds);
}

} // namespace

//======================================================================================================================
// Deterministic temporal diagnostics with no randomness and no fetched assets. Every motion is a
// looping rigid track baked at kAnimationBakeRate over a kClipDuration-second clip; nothing here
// deforms, so every draw except the flagged one reprojects exactly.
//
//   Floor: a 20x20 XZ plane centred on the origin at y = 0, textured with an 8x8 black-and-white
//   checker stretched across its 0..1 UVs -- 2.5 world units per square. Static.
//
//   Rotating cube: a unit cube at (-3, 1, 0) spinning about +Y, one turn per 4 s. Its faces move
//   while its centre does not, which separates rotation-only motion from translation.
//
//   Reference cube and orbit sphere: a static unit cube at (3, 1, 0) with a 0.4-radius sphere
//   orbiting it in the XZ plane at radius 2, one orbit per 6 s -- world position
//   (3 + 2*cos(2*pi*t/6), 1, 2*sin(2*pi*t/6)). A moving object against a still one at the same
//   depth is the pair a reprojection view is read against.
//
//   Poles: five 0.05-wide, 3-tall boxes at z = -4, resting at x in {-1, -0.5, 0, 0.5, 1} and all
//   oscillating together by 0.5*sin(2*pi*t/2) in x, one cycle per 2 s. Thin geometry crossing a
//   pixel in one frame is what disocclusion and thin-feature failures show up on; the 0.5 spacing
//   keeps neighbours from ever meeting.
//
//   Invalid cube: a unit cube at (0, 1, 4), static, flagged render::MotionClass::Invalid. It is
//   the scene's control for the motion sentinel -- geometry that must never be reprojected even
//   though it holds perfectly still.
//
//   Emissive sign: a static 1.5x0.75 quad at (0, 2.5, -6) facing +Z, dark grey base colour with an
//   emissive strength that flashes 0/4 every 3 s (period 6 s, dividing the clip). Its geometry
//   never moves -- only its emissive strength is animated -- so it is a manual-QA reference for
//   temporal reconstruction against an exposure-driving light source.
//
//   Camera track: an eased there-and-back over 8 s -- out to (2, 3, 8) by t = 4 and home to
//   (0, 3, 10) by t = 8, so the loop closes without a cut. Position eases by
//   (1 - cos(2*pi*t/8))/2, giving both endpoints zero velocity; yaw drifts 0.1*sin(2*pi*t/8)
//   radians and pitch holds at -0.15 radians, which frames the floor and every probe.
//   initialCamera is the track's first key: (0, 3, 10), yaw 0, pitch -0.15, 45 degree vertical
//   FOV.
asset::AssetResult<std::unique_ptr<Scene>> loadTemporalLabScene(rojoRHI::Device& device) {
    auto scene = std::make_unique<Scene>();
    scene->name = "TemporalLab";

    // makeGrid rather than render::makePlane: the checker needs the 0..1 UVs only the grid
    // generator authors.
    const MeshId planeMeshIndex = scene->addMesh(
        render::fromGeo(asset::makeGrid(kFloorHalfExtent * 2.0f, kFloorHalfExtent * 2.0f, 2, 2)),
        "TemporalLab.floorMesh");

    const MeshId cubeMeshIndex = scene->addMesh(render::makeCube(), "TemporalLab.cubeMesh");

    const MeshId sphereMeshIndex = scene->addMesh(
        render::fromGeo(asset::makeSphere(kOrbitSphereRadius, 32, 32)), "TemporalLab.sphereMesh");

    // A flat quad in the mesh's own XZ plane; the sign object rotates it 90 degrees about X so its
    // +Y face normal becomes +Z, standing it upright to face the camera.
    const MeshId signMeshIndex = scene->addMesh(
        render::fromGeo(asset::makeGrid(kSignSize.x, kSignSize.y, 2, 2)), "TemporalLab.signMesh");

    const std::vector<uint8_t> checkerPixels = makeCheckerPixels();
    const asset::BakedMipChain checkerChain =
        asset::bakeMips(checkerPixels, kCheckerSize, kCheckerSize, asset::BakeMode::Srgb);
    auto checkerTexture = device.createTexture({.width = checkerChain.width,
                                                .height = checkerChain.height,
                                                .format = rojoRHI::Format::RGBA8Unorm_sRGB,
                                                .mipLevels = checkerChain.mipLevels,
                                                .sampled = true,
                                                .label = "TemporalLab.checker"},
                                               checkerChain.mips);
    if (!checkerTexture) {
        return std::unexpected(uploadFailure(std::move(checkerTexture.error())));
    }
    const TextureId checkerTexturePtr = scene->addTexture(std::move(*checkerTexture));

    const auto addMaterial = [&](const glm::vec3& srgb, float roughness,
                                 std::optional<TextureId> diffuse,
                                 const glm::vec3& emissive = glm::vec3(0.0f)) -> MaterialId {
        MaterialRecord material;
        material.albedo = glm::vec4(srgbToLinear(srgb), 1.0f);
        material.roughness = roughness;
        material.metallic = 0.0f;
        material.diffuse = diffuse;
        material.emissive = emissive;
        const MaterialId index = scene->addMaterial(material);
        return index;
    };
    const MaterialId floorMaterial = addMaterial(glm::vec3(1.0f), 0.8f, checkerTexturePtr);
    const MaterialId rotatingMaterial =
        addMaterial(glm::vec3(0.85f, 0.25f, 0.2f), 0.5f, std::nullopt);
    const MaterialId referenceMaterial =
        addMaterial(glm::vec3(0.25f, 0.5f, 0.85f), 0.5f, std::nullopt);
    const MaterialId orbitMaterial = addMaterial(glm::vec3(0.95f, 0.8f, 0.2f), 0.3f, std::nullopt);
    const MaterialId poleMaterial = addMaterial(glm::vec3(0.9f), 0.6f, std::nullopt);
    const MaterialId invalidMaterial =
        addMaterial(glm::vec3(0.5f, 0.15f, 0.6f), 0.5f, std::nullopt);
    const MaterialId signMaterial = addMaterial(glm::vec3(0.2f), 0.9f, std::nullopt, kSignEmissive);

    glm::vec3 aabbMin{std::numeric_limits<float>::max()};
    glm::vec3 aabbMax{std::numeric_limits<float>::lowest()};
    const auto expandAabb = [&](const glm::vec3& center, const glm::vec3& halfExtent) {
        aabbMin = glm::min(aabbMin, center - halfExtent);
        aabbMax = glm::max(aabbMax, center + halfExtent);
    };

    const auto addObject = [&](std::string name, const glm::vec3& position, const glm::vec3& scale,
                               MeshId meshIndex, MaterialId materialIndex,
                               render::MotionClass motionClass) -> uint32_t {
        const auto index = static_cast<uint32_t>(scene->objects.size());
        scene->addObject({.name = std::move(name),
                          .position = position,
                          .scale = scale,
                          .mesh = meshIndex,
                          .material = materialIndex,
                          .motionClass = motionClass});
        return index;
    };

    addObject("temporal-lab floor", glm::vec3(0.0f), glm::vec3(1.0f), planeMeshIndex, floorMaterial,
              render::MotionClass::Rigid);
    expandAabb(glm::vec3(0.0f), glm::vec3(kFloorHalfExtent, 0.0f, kFloorHalfExtent));

    const uint32_t rotatingCube =
        addObject("temporal-lab rotating cube", kRotatingCubePosition, glm::vec3(1.0f),
                  cubeMeshIndex, rotatingMaterial, render::MotionClass::Rigid);
    expandAabb(kRotatingCubePosition, glm::vec3(0.87f)); // a unit cube's half diagonal when spun

    addObject("temporal-lab reference cube", kReferenceCubePosition, glm::vec3(1.0f), cubeMeshIndex,
              referenceMaterial, render::MotionClass::Rigid);
    expandAabb(kReferenceCubePosition, glm::vec3(0.5f));

    const glm::vec3 orbitStart = kReferenceCubePosition + glm::vec3(kOrbitRadius, 0.0f, 0.0f);
    const uint32_t orbitSphere =
        addObject("temporal-lab orbit sphere", orbitStart, glm::vec3(1.0f), sphereMeshIndex,
                  orbitMaterial, render::MotionClass::Rigid);
    expandAabb(kReferenceCubePosition, glm::vec3(kOrbitRadius + kOrbitSphereRadius));

    std::array<uint32_t, std::size(kPoleX)> poles{};
    const glm::vec3 poleScale{kPoleWidth, kPoleHeight, kPoleWidth};
    for (size_t i = 0; i < std::size(kPoleX); ++i) {
        const glm::vec3 rest{kPoleX[i], kPoleHeight * 0.5f, kPoleZ};
        poles[i] = addObject("temporal-lab pole " + std::to_string(i), rest, poleScale,
                             cubeMeshIndex, poleMaterial, render::MotionClass::Rigid);
        expandAabb(
            rest, glm::vec3(kPoleWidth * 0.5f + kPoleSwing, kPoleHeight * 0.5f, kPoleWidth * 0.5f));
    }

    // The one draw whose history must not be reprojected, so the sentinel path has a subject even
    // in a frame where nothing else is invalid.
    addObject("temporal-lab invalid cube", kInvalidCubePosition, glm::vec3(1.0f), cubeMeshIndex,
              invalidMaterial, render::MotionClass::Invalid);
    expandAabb(kInvalidCubePosition, glm::vec3(0.5f));

    // The flashing sign: static geometry (no rigid track), driven only by an EmissiveTrack.
    const auto signIndex = static_cast<uint32_t>(scene->objects.size());
    scene->addObject({.name = "temporal-lab emissive sign",
                      .position = kSignPosition,
                      .eulerDegrees = glm::vec3(90.0f, 0.0f, 0.0f),
                      .scale = glm::vec3(1.0f),
                      .mesh = signMeshIndex,
                      .material = signMaterial,
                      .motionClass = render::MotionClass::Rigid});
    expandAabb(kSignPosition, glm::vec3(kSignSize.x * 0.5f, kSignSize.y * 0.5f, 0.0f));

    scene->animation.tracks.push_back(makeTrack(rotatingCube, [](double time) {
        return asset::RigidKey{
            .translation = kRotatingCubePosition,
            .rotation = glm::angleAxis(phase(time, kCubeTurnSeconds), glm::vec3(0.0f, 1.0f, 0.0f)),
            .scale = glm::vec3(1.0f)};
    }));
    scene->animation.tracks.push_back(makeTrack(orbitSphere, [](double time) {
        const float angle = phase(time, kOrbitSeconds);
        return asset::RigidKey{.translation = kReferenceCubePosition +
                                              glm::vec3(kOrbitRadius * std::cos(angle), 0.0f,
                                                        kOrbitRadius * std::sin(angle)),
                               .scale = glm::vec3(1.0f)};
    }));
    for (size_t i = 0; i < std::size(kPoleX); ++i) {
        const float restX = kPoleX[i];
        scene->animation.tracks.push_back(makeTrack(poles[i], [restX, poleScale](double time) {
            return asset::RigidKey{
                .translation =
                    glm::vec3(restX + kPoleSwing * std::sin(phase(time, kPoleSwingSeconds)),
                              kPoleHeight * 0.5f, kPoleZ),
                .scale = poleScale};
        }));
    }

    // The sign alternates off/on strength every half period, dividing the 24 s clip evenly so the
    // loop closes on a key.
    asset::EmissiveTrack signTrack;
    signTrack.objectIndex = signIndex;
    const double signStepSeconds = kSignPeriodSeconds * 0.5;
    for (double time = 0.0; time <= kClipDuration; time += signStepSeconds) {
        const bool on = (signTrack.keys.size() % 2) != 0;
        signTrack.keys.push_back({.time = time, .strength = on ? kSignStrengthOn : 0.0f});
    }
    scene->animation.emissiveTracks.push_back(std::move(signTrack));

    scene->animation.cameraTrack.reserve(kKeyCount);
    for (size_t i = 0; i < kKeyCount; ++i) {
        const double time = static_cast<double>(i) / asset::kAnimationBakeRate;
        const float angle = phase(time, kCameraLoopSeconds);
        const float eased = (1.0f - std::cos(angle)) * 0.5f;
        scene->animation.cameraTrack.push_back(
            {.time = time,
             .position = glm::mix(kCameraStart, kCameraEnd, eased),
             .yaw = kCameraYawDrift * std::sin(angle),
             .pitch = kCameraPitch});
    }
    scene->animation.duration = kClipDuration;
    scene->animation.loop = true;
    scene->animate(0.0);
    scene->resetMotion();

    const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
    scene->boundingSphere = glm::vec4(center, glm::length(aabbMax - center));

    if (auto sky = attachNeutralEnvironment(device, *scene, "TemporalLab"); !sky) {
        return std::unexpected(sky.error());
    }

    const asset::CameraKey first = scene->animation.cameraTrack.front();
    scene->initialCamera.position = first.position;
    scene->initialCamera.yaw = first.yaw;
    scene->initialCamera.pitch = first.pitch;
    scene->initialCamera.fovY = glm::radians(45.0f);
    scene->initialCamera.nearZ = 0.05f;
    scene->initialCamera.farZ = 200.0f;

    if (auto finalized = scene->finalize(device); !finalized) {
        return std::unexpected(uploadFailure(std::move(finalized.error())));
    }
    return scene;
}

} // namespace lmx::scene
