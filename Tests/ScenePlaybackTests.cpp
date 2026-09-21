#include "EngineSceneTestSupport.h"
#include "Render/SceneViewBuilder.h"

//======================================================================================================================
// A Scene with no IBL attached must still publish a renderable view. Empty objects make the
// forwarding observable without constructing a GPU device -- and a bare Scene is exactly the case
// where all three IBL pointers are null, which the renderer's fallbacks are what make legal.
TEST_CASE("Scene::view forwards a missing IBL set as null rather than fabricating one", "[scene]") {
    Scene scene;

    std::vector<lmx::engine::DrawItem> items;
    const render::SceneView view =
        render::buildSceneView(scene, items, render::ShadowFilter::PCF, /*wireframe=*/false);

    REQUIRE(view.irradiance == nullptr);
    REQUIRE(view.prefilteredEnv == nullptr);
    REQUIRE(view.dfgLut == nullptr);
}

//======================================================================================================================
TEST_CASE("scene IDs are stable and reject unknown input", "[scene]") {
    REQUIRE(lmx::scenes::sceneIdString(*lmx::scenes::parseSceneId("sponza")) == "sponza");
    REQUIRE(lmx::scenes::sceneIdString(*lmx::scenes::parseSceneId("damaged-helmet")) ==
            "damaged-helmet");
    REQUIRE(lmx::scenes::sceneIdString(*lmx::scenes::parseSceneId("material-lab")) ==
            "material-lab");
    REQUIRE(lmx::scenes::sceneIdString(*lmx::scenes::parseSceneId("milk-truck")) == "milk-truck");
    REQUIRE(lmx::scenes::sceneIdString(*lmx::scenes::parseSceneId("temporal-lab")) ==
            "temporal-lab");
    REQUIRE_FALSE(lmx::scenes::parseSceneId("3"));
    REQUIRE_FALSE(lmx::scenes::parseSceneId("Sponza"));
    REQUIRE(lmx::scenes::sceneIdString(lmx::scenes::defaultSceneId()) == "sponza");
}

//======================================================================================================================
TEST_CASE("Scene::followCameraTrack samples the scene clock while preserving camera lens and speed",
          "[scene]") {
    Scene scene;
    scene.animation.cameraTrack = {
        CameraKey{.time = 0.0, .position = {0.0f, 2.0f, 4.0f}, .yaw = -0.4f, .pitch = 0.2f},
        CameraKey{.time = 4.0, .position = {8.0f, 6.0f, 0.0f}, .yaw = 0.4f, .pitch = -0.2f}};
    scene.animationTime = 1.0;
    lmx::engine::Camera camera;
    camera.fovY = 0.9f;
    camera.nearZ = 0.3f;
    camera.farZ = 700.0f;
    camera.moveSpeed = 8.0f;

    scene.followCameraTrack(camera);

    REQUIRE(near3(camera.position, {2.0f, 3.0f, 3.0f}));
    REQUIRE(camera.yaw == Catch::Approx(-0.2f));
    REQUIRE(camera.pitch == Catch::Approx(0.1f));
    REQUIRE(camera.fovY == 0.9f);
    REQUIRE(camera.nearZ == 0.3f);
    REQUIRE(camera.farZ == 700.0f);
    REQUIRE(camera.moveSpeed == 8.0f);
    REQUIRE(scene.animationTime == 1.0);
}

//======================================================================================================================
TEST_CASE("cameraFromScene copies authored pose and lens while keeping the default movement speed",
          "[scene]") {
    const SceneCamera authored{.position = {4.0f, 5.0f, 6.0f},
                               .yaw = 0.7f,
                               .pitch = -0.2f,
                               .fovY = 1.1f,
                               .nearZ = 0.4f,
                               .farZ = 900.0f};
    const lmx::engine::Camera camera = cameraFromScene(authored);

    REQUIRE(near3(camera.position, authored.position));
    REQUIRE(camera.yaw == authored.yaw);
    REQUIRE(camera.pitch == authored.pitch);
    REQUIRE(camera.fovY == authored.fovY);
    REQUIRE(camera.nearZ == authored.nearZ);
    REQUIRE(camera.farZ == authored.farZ);
    REQUIRE(camera.moveSpeed == lmx::engine::Camera{}.moveSpeed);
}

//======================================================================================================================
TEST_CASE("Scene::commitFrame promotes the current model while preserving the stable draw row",
          "[scene]") {
    Scene scene = makeMotionTestScene();
    std::vector<lmx::engine::DrawItem> items;

    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].instanceRow == scene.objects[0].id.slot);
    REQUIRE(matricesNear(scene.objects[0].modelMatrix(), scene.objects[0].previousModel, 1e-6f));

    const glm::mat4 first = scene.objects[0].modelMatrix();
    scene.commitFrame();
    scene.objects[0].position = glm::vec3(4.0f, 0.0f, 0.0f);

    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(matricesNear(scene.objects[0].previousModel, first, 1e-6f));
    REQUIRE(near3(glm::vec3(scene.objects[0].modelMatrix()[3]), glm::vec3(4.0f, 0.0f, 0.0f)));
    REQUIRE(scene.objects[0].motionClass == lmx::engine::MotionClass::Rigid);
}

//======================================================================================================================
TEST_CASE("Scene::resetMotion collapses an object's motion to its current pose", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.objects[0].position = glm::vec3(9.0f, 0.0f, 0.0f);
    scene.resetMotion();

    std::vector<lmx::engine::DrawItem> items;
    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(matricesNear(scene.objects[0].modelMatrix(), scene.objects[0].previousModel, 1e-6f));
    REQUIRE(near3(glm::vec3(scene.objects[0].previousModel[3]), glm::vec3(9.0f, 0.0f, 0.0f)));
}

//======================================================================================================================
TEST_CASE("Scene keeps an object's declared motion class beside its stable identity", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.objects[0].motionClass = lmx::engine::MotionClass::Invalid;

    std::vector<lmx::engine::DrawItem> items;
    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(scene.objects[0].motionClass == lmx::engine::MotionClass::Invalid);
}

//======================================================================================================================
TEST_CASE("Scene::advanceAnimation wraps at the clip duration only while looping", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.animation.duration = 2.0;
    scene.animation.loop = true;

    scene.advanceAnimation(1.5);
    REQUIRE(scene.animationTime == Catch::Approx(1.5));
    scene.advanceAnimation(1.0);
    REQUIRE(scene.animationTime == Catch::Approx(0.5));

    scene.animation.loop = false;
    scene.animationTime = 0.0;
    scene.advanceAnimation(1.5);
    scene.advanceAnimation(1.0);
    REQUIRE(scene.animationTime == Catch::Approx(2.5));
}

//======================================================================================================================
TEST_CASE("Scene::animate writes each track's sampled pose into its object", "[scene]") {
    Scene scene = makeMotionTestScene();
    RigidTrack track;
    track.objectIndex = 0;
    track.keys.push_back({.time = 0.0,
                          .translation = glm::vec3(0.0f),
                          .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                          .scale = glm::vec3(1.0f)});
    track.keys.push_back(
        {.time = 1.0,
         .translation = glm::vec3(0.0f, 2.0f, 0.0f),
         .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
         .scale = glm::vec3(2.0f)});
    scene.animation.tracks.push_back(track);
    scene.animation.duration = 1.0;

    scene.animate(1.0);
    REQUIRE(near3(scene.objects[0].position, glm::vec3(0.0f, 2.0f, 0.0f)));
    REQUIRE(near3(scene.objects[0].scale, glm::vec3(2.0f)));
    REQUIRE(scene.objects[0].eulerDegrees.y == Catch::Approx(90.0f).margin(1e-3));
    REQUIRE(matricesNear(scene.objects[0].modelMatrix(), sampleRigidTrack(track, 1.0), 1e-4f));
}

//======================================================================================================================
TEST_CASE("Scene::animate writes an object's sampled emissive strength independently of its "
          "shared authored emissive colour",
          "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.material(scene.objects[0].material).emissive = glm::vec3(1.0f, 0.8f, 0.3f);

    EmissiveTrack track;
    track.objectIndex = 0;
    track.keys.push_back({.time = 0.0, .strength = 0.0f});
    track.keys.push_back({.time = 3.0, .strength = 4.0f});
    scene.animation.emissiveTracks.push_back(track);

    scene.animate(3.0);
    REQUIRE(scene.objects[0].emissiveStrength == Catch::Approx(4.0f));

    std::vector<lmx::engine::DrawItem> items;
    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(near3(scene.material(scene.objects[0].material).emissive *
                      scene.objects[0].emissiveStrength,
                  glm::vec3(4.0f, 3.2f, 1.2f)));

    // The authored colour on the material itself is never overwritten.
    REQUIRE(near3(scene.material(scene.objects[0].material).emissive, glm::vec3(1.0f, 0.8f, 0.3f)));
}

//======================================================================================================================
TEST_CASE("an emissive-only clip participates in automatic playback", "[scene]") {
    Scene scene = makeMotionTestScene();
    REQUIRE_FALSE(hasAnimationTracks(scene.animation));
    constexpr double kStep = 1.0 / kAnimationBakeRate;
    scene.animation.duration = 1.0;
    scene.animation.emissiveTracks.push_back(
        {.objectIndex = 0,
         .keys = {{.time = 0.0, .strength = 0.0f}, {.time = kStep, .strength = 4.0f}}});
    scene.animate(0.0);
    REQUIRE(scene.objects[0].emissiveStrength == 0.0f);

    // Both App playback paths use this query, including when no object or camera moves.
    REQUIRE(hasAnimationTracks(scene.animation));
    scene.advanceAnimation(kStep);
    scene.animate(scene.animationTime);
    REQUIRE(scene.objects[0].emissiveStrength == 4.0f);
}

//======================================================================================================================
TEST_CASE("Scene leaves emissive untouched when an object has no emissive track", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.material(scene.objects[0].material).emissive = glm::vec3(0.5f, 0.5f, 0.5f);

    std::vector<lmx::engine::DrawItem> items;
    render::buildSceneView(scene, items, render::ShadowFilter::PCF, false);
    REQUIRE(near3(scene.material(scene.objects[0].material).emissive *
                      scene.objects[0].emissiveStrength,
                  glm::vec3(0.5f, 0.5f, 0.5f)));
}

//======================================================================================================================
TEST_CASE("sampleOrbit is periodic and closed-form about its centre", "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(1.0f, 2.0f, 3.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 5.0f,
                          .phase = 0.0f,
                          .period = 8.0f};

    const glm::vec3 start = sampleOrbit(track, 0.0f);
    const glm::vec3 quarter = sampleOrbit(track, 2.0f); // period / 4
    const glm::vec3 full = sampleOrbit(track, 8.0f);    // period

    // At t == 0 the sample sits `radius` from the centre, along the deterministic basis's first
    // axis: axis is +Z here, so world up (+Y) is the reference and u is world +X.
    REQUIRE(near3(start - track.centre, glm::vec3(track.radius, 0.0f, 0.0f)));
    REQUIRE(Catch::Approx(glm::length(quarter - track.centre)).margin(1e-4f) == track.radius);
    REQUIRE(Catch::Approx(glm::dot(quarter - track.centre, start - track.centre)).margin(1e-3f) ==
            0.0f);
    // A full revolution returns to the starting position.
    REQUIRE(near3(full, start));
}

//======================================================================================================================
TEST_CASE("sampleOrbit holds position fixed when the period is nonpositive", "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(0.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 2.0f,
                          .phase = glm::half_pi<float>(),
                          .period = 0.0f};

    const glm::vec3 early = sampleOrbit(track, 0.0f);
    const glm::vec3 later = sampleOrbit(track, 100.0f);
    REQUIRE(near3(early, later));
}

//======================================================================================================================
TEST_CASE("sampleOrbit wraps far-future seconds onto the same angle as their reduced phase",
          "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(1.0f, 2.0f, 3.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 5.0f,
                          .phase = 0.0f,
                          .period = 8.0f};

    // 1000 whole periods plus a quarter turn should land exactly where a bare quarter turn does;
    // without wrapping seconds onto one period first, float precision at this magnitude coarsens
    // the angle well past a tight tolerance.
    const glm::vec3 nearPosition = sampleOrbit(track, 2.0f); // period / 4
    const glm::vec3 farPosition = sampleOrbit(track, 1000.0f * track.period + 2.0f);
    REQUIRE(near3(nearPosition, farPosition));
}

//======================================================================================================================
TEST_CASE("Scene::animate moves an orbit-tracked light's position without disturbing its other "
          "authored fields or coverageEpoch",
          "[scene]") {
    Scene scene = makeMotionTestScene();
    const auto lightId = scene.addLight({.type = lmx::engine::LocalLightType::Point,
                                         .position = glm::vec3(99.0f, 99.0f, 99.0f),
                                         .colour = glm::vec3(0.2f, 0.4f, 0.6f),
                                         .intensity = 3.0f,
                                         .range = 12.0f});
    REQUIRE(lightId.has_value());
    scene.animation.lightTracks.push_back({.light = 0,
                                           .centre = glm::vec3(0.0f),
                                           .axis = glm::vec3(0.0f, 1.0f, 0.0f),
                                           .radius = 4.0f,
                                           .phase = 0.0f,
                                           .period = 4.0f});
    const uint64_t epochBefore = scene.coverageEpoch();

    scene.animate(1.0); // period / 4

    const lmx::engine::LocalLight* moved = scene.light(*lightId);
    REQUIRE(moved != nullptr);
    // axis is +Y here, which is nearly parallel to the fallback reference: the basis falls back to
    // world +X, so u is world +Z and v is world +X; a quarter turn lands on +radius * v.
    REQUIRE(near3(moved->position, glm::vec3(4.0f, 0.0f, 0.0f)));
    REQUIRE(near3(moved->colour, glm::vec3(0.2f, 0.4f, 0.6f)));
    REQUIRE(moved->intensity == Catch::Approx(3.0f));
    REQUIRE(moved->range == Catch::Approx(12.0f));
    REQUIRE(scene.coverageEpoch() == epochBefore);
}

//======================================================================================================================
TEST_CASE("Scene::animate skips a light orbit track whose light was removed without disturbing "
          "another live orbit-tracked light",
          "[scene]") {
    Scene scene = makeMotionTestScene();
    const auto removedId = scene.addLight({.type = lmx::engine::LocalLightType::Point,
                                           .position = glm::vec3(0.0f),
                                           .colour = glm::vec3(1.0f),
                                           .intensity = 1.0f,
                                           .range = 5.0f});
    REQUIRE(removedId.has_value());
    const auto liveId = scene.addLight({.type = lmx::engine::LocalLightType::Point,
                                        .position = glm::vec3(0.0f),
                                        .colour = glm::vec3(1.0f),
                                        .intensity = 1.0f,
                                        .range = 5.0f});
    REQUIRE(liveId.has_value());
    scene.animation.lightTracks.push_back({.light = 0,
                                           .centre = glm::vec3(0.0f),
                                           .axis = glm::vec3(0.0f, 1.0f, 0.0f),
                                           .radius = 1.0f,
                                           .phase = 0.0f,
                                           .period = 1.0f});
    scene.animation.lightTracks.push_back({.light = 1,
                                           .centre = glm::vec3(0.0f),
                                           .axis = glm::vec3(0.0f, 1.0f, 0.0f),
                                           .radius = 1.0f,
                                           .phase = 0.0f,
                                           .period = 1.0f});
    REQUIRE(scene.removeLight(*removedId));

    scene.animate(0.5); // Must not assert or crash with the first track's light already gone.
    REQUIRE(scene.light(*removedId) == nullptr);
    // The second track's light is still live and must have moved -- a broad catch inside the
    // loop could otherwise silently skip every track, not just the removed one's.
    REQUIRE_FALSE(near3(scene.light(*liveId)->position, glm::vec3(0.0f)));
}

//======================================================================================================================
TEST_CASE("Scene::animate accepts a track that collapses an object to zero scale", "[scene]") {
    Scene scene = makeMotionTestScene();
    RigidTrack track;
    track.objectIndex = 0;
    track.keys.push_back(
        {.time = 0.0, .translation = glm::vec3(1.0f, 0.0f, 0.0f), .scale = glm::vec3(1.0f)});
    track.keys.push_back(
        {.time = 1.0, .translation = glm::vec3(1.0f, 0.0f, 0.0f), .scale = glm::vec3(0.0f)});
    scene.animation.tracks.push_back(track);
    scene.animation.duration = 1.0;

    scene.animate(1.0);
    REQUIRE(near3(scene.objects[0].scale, glm::vec3(0.0f)));
    REQUIRE(matricesNear(scene.objects[0].modelMatrix(), sampleRigidTrack(track, 1.0), 1e-5f));

    scene.animate(0.0);
    REQUIRE(near3(scene.objects[0].scale, glm::vec3(1.0f)));
}
