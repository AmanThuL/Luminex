//----------------------------------------------------------------------------------------------------------------------
/// @file Transform.cpp
/// @brief Implements shared translate-rotate-scale composition and validation.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Math/Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace lmx {

//======================================================================================================================
// The one translate-rotate-scale composition SceneObject::modelMatrix and decomposeTransform's
// round-trip check both go through, so the two can never disagree about the order.
glm::mat4 composeTransform(const DecomposedTransform& transform) {
    glm::mat4 model = glm::translate(glm::mat4{1.0f}, transform.position);
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(transform.eulerDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, transform.scale);
}

//======================================================================================================================
std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world) {
    constexpr float kTolerance = 1e-4f;
    if (std::abs(world[0][3]) > kTolerance || std::abs(world[1][3]) > kTolerance ||
        std::abs(world[2][3]) > kTolerance || std::abs(world[3][3] - 1.0f) > kTolerance) {
        return std::nullopt; // a projective row is not a translate-rotate-scale pose
    }

    glm::vec3 columns[3] = {glm::vec3(world[0]), glm::vec3(world[1]), glm::vec3(world[2])};
    glm::vec3 scale{glm::length(columns[0]), glm::length(columns[1]), glm::length(columns[2])};
    // A mirroring basis has no rotation of its own; glm::decompose's convention -- which this
    // extraction has always followed -- puts the flip in all three scale components at once.
    if (glm::dot(columns[0], glm::cross(columns[1], columns[2])) < 0.0f) {
        scale = -scale;
        for (glm::vec3& column : columns) {
            column = -column;
        }
    }

    glm::mat3 basis{1.0f};
    int degenerate = 0;
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(columns[i]);
        if (length > kTolerance) {
            basis[i] = columns[i] / length;
        } else {
            ++degenerate;
        }
    }
    if (degenerate == 1) {
        // Two axes still fix the frame; the third is their right-handed completion. A zero scale
        // on one axis is a legitimate authored pose -- glm::decompose would divide by that axis's
        // length and return NaN rather than reporting a failure, which is why this does not use it.
        for (int i = 0; i < 3; ++i) {
            if (glm::length(columns[i]) <= kTolerance) {
                basis[i] = glm::cross(basis[(i + 1) % 3], basis[(i + 2) % 3]);
            }
        }
    } else if (degenerate == 2) {
        return std::nullopt; // one surviving axis fixes no rotation
    }

    // Extraction must match SceneObject's Y-X-Z composition order to round-trip compound rotation.
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    glm::extractEulerAngleYXZ(glm::mat4(basis), yaw, pitch, roll);
    const DecomposedTransform decomposed{.position = glm::vec3(world[3]),
                                         .eulerDegrees = glm::degrees(glm::vec3(pitch, yaw, roll)),
                                         .scale = scale};

    // Proving the factorisation by recomposing it is what makes this a decision rather than a
    // guess: shear, which no translate-rotate-scale chain can produce, fails here instead of being
    // silently orthogonalised, and a NaN fails the comparison rather than escaping into a caller.
    const glm::mat4 recomposed = composeTransform(decomposed);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float scaled = kTolerance * std::max(1.0f, std::abs(world[col][row]));
            if (!(std::abs(recomposed[col][row] - world[col][row]) <= scaled)) {
                return std::nullopt;
            }
        }
    }
    return decomposed;
}

} // namespace lmx
