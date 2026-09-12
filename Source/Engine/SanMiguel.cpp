//----------------------------------------------------------------------------------------------------------------------
/// @file SanMiguel.cpp
/// @brief Loads the masked courtyard scene and authors its deterministic comparison camera rail.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <filesystem>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
AssetResult<std::unique_ptr<Scene>> loadSanMiguelScene(rhi::Device& device) {
    std::filesystem::path directory = std::filesystem::current_path();
    std::filesystem::path asset;
    for (int i = 0; i < 8; ++i) {
        const auto candidate = directory / "Assets/Fetched/SanMiguel/SanMiguel.gltf";
        if (std::filesystem::is_regular_file(candidate)) {
            asset = candidate;
            break;
        }
        if (directory == directory.parent_path()) {
            break;
        }
        directory = directory.parent_path();
    }
    if (asset.empty()) {
        return std::unexpected(AssetError{
            AssetErrorCode::NotFound, "San Miguel assets missing; run xmake setup --san-miguel"});
    }
    auto result = loadGltfScene(device, asset.string(), "SanMiguel");
    if (!result) {
        return std::unexpected(result.error());
    }
    Scene& scene = **result;
    scene.name = "San Miguel";
    constexpr double kDuration = 12.0;
    constexpr glm::vec3 kStart{8.0f, 1.8f, 12.0f};
    constexpr glm::vec3 kEnd{11.0f, 1.8f, 8.0f};
    constexpr glm::vec3 kTarget{7.0f, 2.5f, 2.0f};
    // A closed eased dolly gives repeatable parallax and newly revealed foliage without a loop cut.
    const auto keyCount = static_cast<size_t>(kDuration * kAnimationBakeRate) + 1;
    scene.animation.cameraTrack.reserve(keyCount);
    for (size_t i = 0; i < keyCount; ++i) {
        const double time = static_cast<double>(i) / kAnimationBakeRate;
        const float phase = static_cast<float>(time / kDuration) * glm::two_pi<float>();
        const float blend = (1.0f - std::cos(phase)) * 0.5f;
        const glm::vec3 position = glm::mix(kStart, kEnd, blend);
        const glm::vec3 direction = glm::normalize(kTarget - position);
        scene.animation.cameraTrack.push_back({.time = time,
                                               .position = position,
                                               .yaw = std::atan2(direction.x, -direction.z),
                                               .pitch = std::asin(direction.y)});
    }
    scene.animation.duration = kDuration;
    scene.animation.loop = true;
    const auto& first = scene.animation.cameraTrack.front();
    scene.initialCamera = {.position = first.position,
                           .yaw = first.yaw,
                           .pitch = first.pitch,
                           .fovY = glm::radians(55.0f),
                           .nearZ = 0.05f,
                           .farZ = 500.0f};
    scene.resetMotion();
    return result;
}

} // namespace lmx::engine
