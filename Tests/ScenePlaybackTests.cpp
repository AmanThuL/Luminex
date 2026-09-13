#include "EngineSceneTestSupport.h"

//======================================================================================================================
// A Scene with no IBL attached must still publish a renderable view. Empty objects make the
// forwarding observable without constructing a GPU device -- and a bare Scene is exactly the case
// where all three IBL pointers are null, which the renderer's fallbacks are what make legal.
TEST_CASE("Scene::view forwards a missing IBL set as null rather than fabricating one", "[scene]") {
    Scene scene;

    std::vector<render::DrawItem> items;
    const render::SceneView view =
        scene.view(items, render::ShadowFilter::PCF, /*wireframe=*/false);

    REQUIRE(view.irradiance == nullptr);
    REQUIRE(view.prefilteredEnv == nullptr);
    REQUIRE(view.dfgLut == nullptr);
}

//======================================================================================================================
TEST_CASE("scene IDs are stable and reject unknown input", "[scene]") {
    REQUIRE(sceneIdString(*parseSceneId("sponza")) == "sponza");
    REQUIRE(sceneIdString(*parseSceneId("damaged-helmet")) == "damaged-helmet");
    REQUIRE(sceneIdString(*parseSceneId("material-lab")) == "material-lab");
    REQUIRE(sceneIdString(*parseSceneId("milk-truck")) == "milk-truck");
    REQUIRE(sceneIdString(*parseSceneId("temporal-lab")) == "temporal-lab");
    REQUIRE_FALSE(parseSceneId("3"));
    REQUIRE_FALSE(parseSceneId("Sponza"));
    REQUIRE(sceneIdString(defaultSceneId()) == "sponza");
}

//======================================================================================================================
TEST_CASE("Scene::followCameraTrack samples the scene clock while preserving camera lens and speed",
          "[scene]") {
    Scene scene;
    scene.animation.cameraTrack = {
        CameraKey{.time = 0.0, .position = {0.0f, 2.0f, 4.0f}, .yaw = -0.4f, .pitch = 0.2f},
        CameraKey{.time = 4.0, .position = {8.0f, 6.0f, 0.0f}, .yaw = 0.4f, .pitch = -0.2f}};
    scene.animationTime = 1.0;
    render::Camera camera;
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
    const render::Camera camera = cameraFromScene(authored);

    REQUIRE(near3(camera.position, authored.position));
    REQUIRE(camera.yaw == authored.yaw);
    REQUIRE(camera.pitch == authored.pitch);
    REQUIRE(camera.fovY == authored.fovY);
    REQUIRE(camera.nearZ == authored.nearZ);
    REQUIRE(camera.farZ == authored.farZ);
    REQUIRE(camera.moveSpeed == render::Camera{}.moveSpeed);
}

//======================================================================================================================
TEST_CASE("Scene::commitFrame promotes the current model and view reports both", "[scene]") {
    Scene scene = makeMotionTestScene();
    std::vector<render::DrawItem> items;

    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(items.size() == 1);
    REQUIRE(matricesNear(items[0].model, items[0].previousModel, 1e-6f));

    const glm::mat4 first = scene.objects[0].modelMatrix();
    scene.commitFrame();
    scene.objects[0].position = glm::vec3(4.0f, 0.0f, 0.0f);

    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(matricesNear(items[0].previousModel, first, 1e-6f));
    REQUIRE(near3(glm::vec3(items[0].model[3]), glm::vec3(4.0f, 0.0f, 0.0f)));
    REQUIRE(items[0].motionClass == render::MotionClass::Rigid);
}

//======================================================================================================================
TEST_CASE("Scene::resetMotion collapses an object's motion to its current pose", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.objects[0].position = glm::vec3(9.0f, 0.0f, 0.0f);
    scene.resetMotion();

    std::vector<render::DrawItem> items;
    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(matricesNear(items[0].model, items[0].previousModel, 1e-6f));
    REQUIRE(near3(glm::vec3(items[0].previousModel[3]), glm::vec3(9.0f, 0.0f, 0.0f)));
}

//======================================================================================================================
TEST_CASE("Scene::view forwards an object's declared motion class", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.objects[0].motionClass = render::MotionClass::Invalid;

    std::vector<render::DrawItem> items;
    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(items[0].motionClass == render::MotionClass::Invalid);
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
TEST_CASE("Scene::animate writes an object's sampled emissive strength, and view() multiplies it "
          "into the draw item's authored emissive colour",
          "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.materials[0].emissive = glm::vec3(1.0f, 0.8f, 0.3f);

    EmissiveTrack track;
    track.objectIndex = 0;
    track.keys.push_back({.time = 0.0, .strength = 0.0f});
    track.keys.push_back({.time = 3.0, .strength = 4.0f});
    scene.animation.emissiveTracks.push_back(track);

    scene.animate(3.0);
    REQUIRE(scene.objects[0].emissiveStrength == Catch::Approx(4.0f));

    std::vector<render::DrawItem> items;
    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(near3(items[0].material.emissive, glm::vec3(4.0f, 3.2f, 1.2f)));

    // The authored colour on the material itself is never overwritten.
    REQUIRE(near3(scene.materials[0].emissive, glm::vec3(1.0f, 0.8f, 0.3f)));
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
TEST_CASE("Scene::view leaves emissive untouched when an object has no emissive track", "[scene]") {
    Scene scene = makeMotionTestScene();
    scene.materials[0].emissive = glm::vec3(0.5f, 0.5f, 0.5f);

    std::vector<render::DrawItem> items;
    scene.view(items, render::ShadowFilter::PCF, false);
    REQUIRE(near3(items[0].material.emissive, glm::vec3(0.5f, 0.5f, 0.5f)));
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
