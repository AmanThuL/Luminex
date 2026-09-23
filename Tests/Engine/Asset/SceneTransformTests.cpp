#include "Support/EngineSceneTestSupport.h"

//======================================================================================================================
TEST_CASE("decomposeTransform's rotation extraction matches modelMatrix's Y*X*Z composition "
          "order for a compound rotation",
          "[scene]") {
    SceneObject original;
    original.position = {1.0f, 2.0f, 3.0f};
    original.eulerDegrees = {35.0f, 40.0f, 25.0f}; // x, y, z -- all three axes, none of them zero
    original.scale = {2.0f, 0.5f, 1.5f};           // non-uniform, exercising decompose's scale too
    const glm::mat4 world = original.modelMatrix();

    const std::optional<lmx::DecomposedTransform> decomposed = lmx::decomposeTransform(world);
    REQUIRE(decomposed.has_value());
    REQUIRE(near3(decomposed->position, original.position));
    REQUIRE(near3(decomposed->scale, original.scale));

    SceneObject reconstructed;
    reconstructed.position = decomposed->position;
    reconstructed.eulerDegrees = decomposed->eulerDegrees;
    reconstructed.scale = decomposed->scale;
    const glm::mat4 roundTripped = reconstructed.modelMatrix();

    INFO("original eulerDegrees: (" + std::to_string(original.eulerDegrees.x) + ", " +
         std::to_string(original.eulerDegrees.y) + ", " + std::to_string(original.eulerDegrees.z) +
         ")");
    INFO("recovered eulerDegrees: (" + std::to_string(decomposed->eulerDegrees.x) + ", " +
         std::to_string(decomposed->eulerDegrees.y) + ", " +
         std::to_string(decomposed->eulerDegrees.z) + ")");
    REQUIRE(matricesNear(world, roundTripped, 1e-4f));
}

//======================================================================================================================
TEST_CASE("decomposeTransform still round-trips both fetched assets' single-axis node "
          "transforms",
          "[scene]") {
    // Sponza: a uniform scale, no rotation.
    {
        const glm::mat4 world = glm::scale(glm::mat4(1.0f), glm::vec3(0.008f));
        const std::optional<lmx::DecomposedTransform> decomposed = lmx::decomposeTransform(world);
        REQUIRE(decomposed.has_value());
        SceneObject reconstructed;
        reconstructed.position = decomposed->position;
        reconstructed.eulerDegrees = decomposed->eulerDegrees;
        reconstructed.scale = decomposed->scale;
        REQUIRE(matricesNear(world, reconstructed.modelMatrix(), 1e-5f));
    }
    // DamagedHelmet: a single 90-degree rotation about X, no translation or scale.
    {
        SceneObject source;
        source.eulerDegrees = {90.0f, 0.0f, 0.0f};
        const glm::mat4 world = source.modelMatrix();
        const std::optional<lmx::DecomposedTransform> decomposed = lmx::decomposeTransform(world);
        REQUIRE(decomposed.has_value());
        SceneObject reconstructed;
        reconstructed.position = decomposed->position;
        reconstructed.eulerDegrees = decomposed->eulerDegrees;
        reconstructed.scale = decomposed->scale;
        REQUIRE(matricesNear(world, reconstructed.modelMatrix(), 1e-4f));
    }
}

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

//======================================================================================================================
// A zero scale is a legitimate authored pose -- glTF conformance clips collapse an object to
// nothing and back -- and both the loader's validation and Scene::animate's write-back go through
// decomposeTransform, so it has to survive the round trip rather than be rejected or return NaN.
TEST_CASE("decomposeTransform round-trips a zero-scale pose", "[asset]") {
    RigidTrack track;
    track.step = true;
    track.keys.push_back(
        {.time = 0.0,
         .translation = glm::vec3(2.0f, 1.0f, -3.0f),
         .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
         .scale = glm::vec3(1.0f)});
    track.keys.push_back({.time = 1.0,
                          .translation = glm::vec3(2.0f, 1.0f, -3.0f),
                          .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                          .scale = glm::vec3(0.0f)});

    const glm::mat4 collapsed = sampleRigidTrack(track, 1.0);
    const auto decomposed = lmx::decomposeTransform(collapsed);
    REQUIRE(decomposed.has_value());
    REQUIRE(near3(decomposed->scale, glm::vec3(0.0f)));
    SceneObject object;
    object.position = decomposed->position;
    object.eulerDegrees = decomposed->eulerDegrees;
    object.scale = decomposed->scale;
    REQUIRE(matricesNear(object.modelMatrix(), collapsed, 1e-5f));
}

//======================================================================================================================
TEST_CASE("decomposeTransform rejects a sheared transform instead of orthogonalising it",
          "[asset]") {
    glm::mat4 sheared{1.0f};
    sheared[1][0] = 0.5f; // Y basis leans into X: no translate-rotate-scale chain produces this
    REQUIRE_FALSE(lmx::decomposeTransform(sheared).has_value());
}
