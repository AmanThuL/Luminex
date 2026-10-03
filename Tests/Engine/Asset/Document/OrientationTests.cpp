#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Document/SceneDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/constants.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>

using namespace lmx::asset;

//======================================================================================================================
TEST_CASE("directional strengths round trip exactly through power-of-two intensity",
          "[asset][scene-document][orientation]") {
    std::mt19937 random(73521);
    for (size_t i = 0; i < 10000; ++i) {
        glm::vec3 strength;
        for (int axis = 0; axis < 3; ++axis) {
            uint32_t bits = random() & 0x7fffffffu;
            if (bits >= 0x7f800000u)
                bits = 0;
            strength[axis] = std::bit_cast<float>(bits);
        }
        const auto encoded = encodeStrength(strength);
        REQUIRE(decodeStrength(encoded) == strength);
        for (int axis = 0; axis < 3; ++axis)
            REQUIRE(std::bit_cast<uint32_t>(decodeStrength(encoded)[axis]) ==
                    std::bit_cast<uint32_t>(strength[axis]));
        REQUIRE(encoded.intensity >=
                static_cast<double>(glm::max(strength.x, glm::max(strength.y, strength.z))));
        int exponent = 0;
        REQUIRE(std::frexp(encoded.intensity, &exponent) == 0.5);
        REQUIRE(glm::all(glm::greaterThanEqual(encoded.colour, glm::dvec3(0.0))));
        REQUIRE(glm::all(glm::lessThanEqual(encoded.colour, glm::dvec3(1.0))));
    }
}

//======================================================================================================================
TEST_CASE("orientation search returns only exact camera or direction matches",
          "[asset][scene-document][orientation]") {
    for (const auto angles : {glm::vec2(0.0f), glm::vec2(0.35f, -0.04f), glm::vec2(-1.2f, 0.1f),
                              glm::vec2(3.5f, -0.04f)}) {
        const auto rotation = exactRotationForCamera(angles.x, angles.y, angles.x);
        REQUIRE(rotation);
        const auto actual = cameraAnglesForRotation(*rotation, angles.x);
        REQUIRE(actual == angles);
    }
    for (const auto direction : {glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                 glm::normalize(glm::vec3(0.0f, -1.0f, 0.6f)),
                                 glm::normalize(glm::vec3(0.0f, -1.0f, -0.6f))}) {
        const auto rotation = exactRotationForDirection(direction);
        REQUIRE(rotation);
        REQUIRE(directionForRotation(*rotation) == direction);
    }
    REQUIRE_FALSE(exactRotationForDirection(glm::vec3(1.0f, 2.0f, 3.0f)));
    REQUIRE_FALSE(exactRotationForDirection(glm::vec3(0.0f)));
    REQUIRE_FALSE(exactRotationForCamera(0.0f, 2.0f, 0.0f));
    REQUIRE_FALSE(exactRotationForCamera(std::numeric_limits<float>::infinity(), 0.0f, 0.0f));
    REQUIRE(unwrapYaw(7.0f, 0.5f) == 7.0f + std::remainder(0.5f - 7.0f, glm::two_pi<float>()));
}

//======================================================================================================================
TEST_CASE("unsuccessful neighbourhood searches never substitute approximate results",
          "[asset][scene-document][orientation]") {
    std::mt19937 random(183);
    std::uniform_real_distribution<float> yaw(-20.0f, 20.0f);
    std::uniform_real_distribution<float> pitch(-1.4f, 1.4f);
    size_t misses = 0;
    for (size_t i = 0; i < 200; ++i) {
        const float y = yaw(random);
        const float p = pitch(random);
        const auto result = exactRotationForCamera(y, p, y);
        if (result)
            REQUIRE(cameraAnglesForRotation(*result, y) == glm::vec2(y, p));
        else
            ++misses;
    }
    REQUIRE(misses > 0);
}

//======================================================================================================================
TEST_CASE("object orientation decodes the editor YXZ convention without scale decomposition",
          "[asset][scene-document][orientation][ux6-content]") {
    const auto compound = eulerDegreesForRotation(glm::quat(.5f, .5f, .5f, .5f));
    const glm::vec3 expected{0, 90, 90};
    for (int k = 0; k < 3; ++k)
        REQUIRE(std::bit_cast<uint32_t>(compound[k]) == std::bit_cast<uint32_t>(expected[k]));
    REQUIRE(eulerDegreesForRotation(glm::quat(1, 0, 0, 0)) == glm::vec3(0));
    REQUIRE(eulerDegreesForRotation(glm::quat(0, 0, 1, 0)) == glm::vec3(0, 180, 0));
    REQUIRE(eulerDegreesForRotation(glm::quat(0, 0, 0, 1)) == glm::vec3(0, 0, 180));
}

//======================================================================================================================
TEST_CASE("object quaternion encoding returns only bitwise exact Euler preimages",
          "[asset][scene-document][orientation][ux6-mesh-export]") {
    for (const auto angles : {glm::vec3(0), glm::vec3(0, 30, 0), glm::vec3(15, 25, 35)}) {
        const auto rotation = exactRotationForEulerDegrees(angles);
        INFO(angles.x << "," << angles.y << "," << angles.z);
        CHECK(rotation);
        if (!rotation)
            continue;
        const auto decoded = eulerDegreesForRotation(*rotation);
        for (int k = 0; k < 3; ++k)
            CHECK(std::bit_cast<uint32_t>(decoded[k]) == std::bit_cast<uint32_t>(angles[k]));
    }
    CHECK_FALSE(exactRotationForEulerDegrees({0, 360, 0}));
    CHECK_FALSE(exactRotationForEulerDegrees({100, 0, 0}));
    CHECK_FALSE(exactRotationForEulerDegrees({0, std::numeric_limits<float>::infinity(), 0}));
    const glm::vec3 negativeZero{-0.0f, 0.0f, 0.0f};
    const auto negativeRotation = exactRotationForEulerDegrees(negativeZero);
    REQUIRE(negativeRotation);
    const auto negativeDecoded = eulerDegreesForRotation(*negativeRotation);
    for (int k = 0; k < 3; ++k)
        REQUIRE(std::bit_cast<uint32_t>(negativeDecoded[k]) ==
                std::bit_cast<uint32_t>(negativeZero[k]));
}

//======================================================================================================================
TEST_CASE("object zero signs survive quaternion and document round trips",
          "[asset][scene-document][orientation][ux6-signed-zero]") {
    const auto exact = [](glm::vec3 actual, glm::vec3 expected) {
        for (int k = 0; k < 3; ++k)
            REQUIRE(std::bit_cast<uint32_t>(actual[k]) == std::bit_cast<uint32_t>(expected[k]));
    };
    exact(eulerDegreesForRotation(glm::quat(1, 0, 0, 0)), glm::vec3(0));
    exact(eulerDegreesForRotation(glm::quat(1, -0.0f, 0, 0)), {-0.0f, 0, 0});
    exact(eulerDegreesForRotation(glm::quat(0, 0, 1, 0)), {0, 180, 0});
    exact(eulerDegreesForRotation(glm::quat(0, 0, 0, 1)), {0, 0, 180});
    exact(eulerDegreesForRotation(glm::quat(.5f, .5f, .5f, .5f)), {0, 90, 90});

    for (const auto expected : {glm::vec3(0), glm::vec3(-0.0f, 0, 0)}) {
        const auto rotation = exactRotationForEulerDegrees(expected);
        REQUIRE(rotation);
        exact(eulerDegreesForRotation(*rotation), expected);
        SceneDocument document;
        document.schemaVersion = kSceneDocumentSchema;
        document.name = "Signed zero orientation";
        document.nodes = {{.name = "Camera", .camera = 0},
                          {.name = "Object group", .rotation = *rotation}};
        document.rootNodes = {0, 1};
        document.cameras = {{.name = "Perspective"}};
        const std::filesystem::path root = "SceneDocuments/signed-zero-orientation";
        std::filesystem::create_directories(root);
        const auto path =
            root / (std::signbit(expected.x) ? "negative.scene.gltf" : "positive.scene.gltf");
        REQUIRE(saveSceneDocument(document, path));
        const auto restored = readSceneDocument(path);
        REQUIRE(restored);
        for (int k = 0; k < 4; ++k)
            REQUIRE(std::bit_cast<uint32_t>(restored->nodes[1].rotation[k]) ==
                    std::bit_cast<uint32_t>((*rotation)[k]));
        exact(eulerDegreesForRotation(restored->nodes[1].rotation), expected);
    }
}
