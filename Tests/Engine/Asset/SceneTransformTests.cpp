#include "Support/EngineSceneTestSupport.h"

//======================================================================================================================
TEST_CASE("sampleRigidTrack interpolates translation and scale linearly between keys", "[asset]") {
    RigidTrack track;
    track.keys.push_back({.time = 0.0,
                          .translation = glm::vec3(0.0f, 0.0f, 0.0f),
                          .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                          .scale = glm::vec3(1.0f)});
    track.keys.push_back({.time = 2.0,
                          .translation = glm::vec3(4.0f, -2.0f, 6.0f),
                          .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                          .scale = glm::vec3(3.0f)});

    const glm::mat4 mid = sampleRigidTrack(track, 1.0);
    REQUIRE(near3(glm::vec3(mid[3]), glm::vec3(2.0f, -1.0f, 3.0f)));
    REQUIRE(matricesNear(
        mid,
        glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, -1.0f, 3.0f)), glm::vec3(2.0f)),
        1e-5f));
}

//======================================================================================================================
TEST_CASE("sampleRigidTrack slerps a quarter turn to its half angle", "[asset]") {
    RigidTrack track;
    track.keys.push_back({.time = 0.0,
                          .translation = glm::vec3(0.0f),
                          .rotation = glm::angleAxis(0.0f, glm::vec3(0.0f, 1.0f, 0.0f)),
                          .scale = glm::vec3(1.0f)});
    track.keys.push_back(
        {.time = 1.0,
         .translation = glm::vec3(0.0f),
         .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
         .scale = glm::vec3(1.0f)});

    // Halfway along a quarter turn about +Y is exactly 45 degrees: +X maps to (cos45, 0, -sin45).
    const glm::mat4 half = sampleRigidTrack(track, 0.5);
    const glm::vec3 axis = glm::vec3(half * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    REQUIRE(near3(axis, glm::vec3(std::sqrt(0.5f), 0.0f, -std::sqrt(0.5f))));
}

//======================================================================================================================
TEST_CASE("sampleRigidTrack holds the previous key when the track steps", "[asset]") {
    RigidTrack track;
    track.step = true;
    track.keys.push_back({.time = 0.0, .translation = glm::vec3(0.0f)});
    track.keys.push_back({.time = 1.0, .translation = glm::vec3(10.0f, 0.0f, 0.0f)});

    REQUIRE(near3(glm::vec3(sampleRigidTrack(track, 0.999)[3]), glm::vec3(0.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(track, 1.0)[3]), glm::vec3(10.0f, 0.0f, 0.0f)));
}

//======================================================================================================================
TEST_CASE("sampleRigidTrack clamps before the first key and after the last", "[asset]") {
    RigidTrack track;
    track.keys.push_back({.time = 1.0, .translation = glm::vec3(-5.0f, 0.0f, 0.0f)});
    track.keys.push_back({.time = 3.0, .translation = glm::vec3(5.0f, 0.0f, 0.0f)});

    REQUIRE(near3(glm::vec3(sampleRigidTrack(track, -100.0)[3]), glm::vec3(-5.0f, 0.0f, 0.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(track, 100.0)[3]), glm::vec3(5.0f, 0.0f, 0.0f)));
}

//======================================================================================================================
TEST_CASE("sampleCameraTrack interpolates position and angles linearly and clamps at the ends",
          "[asset]") {
    const std::array<CameraKey, 2> keys = {
        CameraKey{
            .time = 0.0, .position = glm::vec3(0.0f, 3.0f, 10.0f), .yaw = 0.0f, .pitch = -0.2f},
        CameraKey{
            .time = 4.0, .position = glm::vec3(2.0f, 3.0f, 8.0f), .yaw = 0.4f, .pitch = 0.2f}};

    const CameraKey mid = sampleCameraTrack(keys, 2.0);
    REQUIRE(near3(mid.position, glm::vec3(1.0f, 3.0f, 9.0f)));
    REQUIRE(mid.yaw == Catch::Approx(0.2f));
    REQUIRE(mid.pitch == Catch::Approx(0.0f).margin(1e-6));

    REQUIRE(near3(sampleCameraTrack(keys, -1.0).position, keys[0].position));
    REQUIRE(near3(sampleCameraTrack(keys, 9.0).position, keys[1].position));
}

//======================================================================================================================
TEST_CASE("sampleEmissiveTrack holds the last key at or before the sample time", "[asset]") {
    EmissiveTrack track;
    track.keys.push_back({.time = 0.0, .strength = 0.0f});
    track.keys.push_back({.time = 3.0, .strength = 4.0f});
    track.keys.push_back({.time = 6.0, .strength = 0.0f});

    REQUIRE(sampleEmissiveTrack(track, 0.0) == Catch::Approx(0.0f));
    REQUIRE(sampleEmissiveTrack(track, 1.0) == Catch::Approx(0.0f));
    REQUIRE(sampleEmissiveTrack(track, 2.999) == Catch::Approx(0.0f));
    REQUIRE(sampleEmissiveTrack(track, 3.0) == Catch::Approx(4.0f));
    REQUIRE(sampleEmissiveTrack(track, 4.0) == Catch::Approx(4.0f));
    REQUIRE(sampleEmissiveTrack(track, 5.999) == Catch::Approx(4.0f));
    REQUIRE(sampleEmissiveTrack(track, 6.0) == Catch::Approx(0.0f));
}

//======================================================================================================================
TEST_CASE("sampleEmissiveTrack clamps before the first key and after the last", "[asset]") {
    EmissiveTrack track;
    track.keys.push_back({.time = 1.0, .strength = 2.0f});
    track.keys.push_back({.time = 3.0, .strength = 5.0f});

    REQUIRE(sampleEmissiveTrack(track, -100.0) == Catch::Approx(2.0f));
    REQUIRE(sampleEmissiveTrack(track, 100.0) == Catch::Approx(5.0f));
}
