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
