//----------------------------------------------------------------------------------------------------------------------
/// @file SponzaCameraTour.cpp
/// @brief Bakes an arc-length-paced Sponza flythrough with rounded corridor turns.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/SponzaCameraTour.h"

#include "Engine/Scene/Scene.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lmx::scenes {

namespace {

// The pinned mesh has floors at approximately Y=0/4.15. Corridor centres avoid its curtains,
// columns and vases; Z=0.6 passes beside the upper end columns. Only the open atrium joins levels:
// the asset has no connecting staircase, so this is a flythrough rather than a walking route.
constexpr std::array<glm::vec3, 16> kCorners{{
    {7.0f, 1.6f, 0.6f},
    {11.0f, 1.6f, 0.6f},
    {11.0f, 1.6f, -4.8f},
    {-12.0f, 1.6f, -4.8f},
    {-12.0f, 1.6f, 3.9f},
    {11.0f, 1.6f, 3.9f},
    {11.0f, 1.6f, 0.6f},
    {7.0f, 1.6f, 0.6f},
    {-7.0f, 5.8f, 0.6f},
    {-12.0f, 5.8f, 0.6f},
    {-12.0f, 5.8f, -4.8f},
    {11.0f, 5.8f, -4.8f},
    {11.0f, 5.8f, 3.9f},
    {-12.0f, 5.8f, 3.9f},
    {-12.0f, 5.8f, 0.6f},
    {-7.0f, 5.8f, 0.6f},
}};

//======================================================================================================================
std::vector<glm::vec3> roundedRoute() {
    constexpr size_t kCurveSteps = 64;
    std::vector<glm::vec3> points;
    for (size_t i = 0; i < kCorners.size(); ++i) {
        const auto p = kCorners[i];
        const auto previous = kCorners[(i + kCorners.size() - 1) % kCorners.size()];
        const auto next = kCorners[(i + 1) % kCorners.size()];
        const auto incoming = glm::normalize(p - previous);
        const auto outgoing = glm::normalize(next - p);
        const float radius =
            std::min({1.5f, glm::distance(p, previous) * 0.4f, glm::distance(p, next) * 0.4f});
        const auto a = p - incoming * radius;
        const auto d = p + outgoing * radius;
        const auto b = a + incoming * radius * 0.55f;
        const auto c = d - outgoing * radius * 0.55f;
        for (size_t step = 0; step <= kCurveSteps; ++step) {
            const float t = static_cast<float>(step) / kCurveSteps;
            const float u = 1.0f - t;
            points.push_back(u * u * u * a + 3.0f * u * u * t * b + 3.0f * u * t * t * c +
                             t * t * t * d);
        }
    }
    // Start on the lower straight, after the descending transition has levelled out.
    std::rotate(points.begin(), points.begin() + kCurveSteps, points.end());
    points.push_back(points.front());
    return points;
}

//======================================================================================================================
glm::vec3 atDistance(const std::vector<glm::vec3>& points, const std::vector<double>& distances,
                     double distance) {
    const double length = distances.back();
    distance = std::fmod(distance, length);
    if (distance < 0.0)
        distance += length;
    const auto end = std::upper_bound(distances.begin(), distances.end(), distance);
    const size_t next = static_cast<size_t>(end - distances.begin());
    const size_t previous = next - 1;
    const float t = static_cast<float>((distance - distances[previous]) /
                                       (distances[next] - distances[previous]));
    return glm::mix(points[previous], points[next], t);
}

} // namespace

//======================================================================================================================
void authorSponzaCameraTour(engine::Scene& scene) {
    const auto points = roundedRoute();
    std::vector<double> distances(points.size(), 0.0);
    for (size_t i = 1; i < points.size(); ++i)
        distances[i] = distances[i - 1] + glm::distance(points[i - 1], points[i]);

    auto& keys = scene.animation.cameraTrack;
    keys.clear();
    const size_t intervals =
        static_cast<size_t>(kSponzaCameraTourDuration * asset::kAnimationBakeRate);
    keys.reserve(intervals + 1);
    for (size_t i = 0; i < intervals; ++i) {
        const double distance = distances.back() * static_cast<double>(i) / intervals;
        const auto position = atDistance(points, distances, distance);
        // A symmetric look-ahead smooths orientation across straight/curve joins without aiming
        // sideways at corridor walls. Unwrapped yaw keeps linear key interpolation on the short
        // arc.
        const auto direction = glm::normalize(atDistance(points, distances, distance + 1.2) -
                                              atDistance(points, distances, distance - 1.2));
        float yaw = std::atan2(direction.x, -direction.z);
        if (!keys.empty())
            yaw = keys.back().yaw + std::remainder(yaw - keys.back().yaw, glm::two_pi<float>());
        keys.push_back({.time = static_cast<double>(i) / asset::kAnimationBakeRate,
                        .position = position,
                        .yaw = yaw,
                        .pitch = std::asin(direction.y) - 0.04f});
    }
    keys.push_back(keys.front());
    keys.back().time = kSponzaCameraTourDuration;
    scene.animation.duration = kSponzaCameraTourDuration;
    scene.animation.loop = true;
    scene.initialCamera.position = keys.front().position;
    scene.initialCamera.yaw = keys.front().yaw;
    scene.initialCamera.pitch = keys.front().pitch;
}

} // namespace lmx::scenes
