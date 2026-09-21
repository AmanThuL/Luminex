#include "Engine/Catalog/CatalogScenes.h"
#include "EngineSceneTestSupport.h"

//======================================================================================================================
TEST_CASE("loadTemporalLabScene places its diagnostics at the documented world positions",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadTemporalLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* floor = findObject(**scene, "temporal-lab floor");
    REQUIRE(floor != nullptr);
    REQUIRE(near3(floor->position, glm::vec3(0.0f)));
    REQUIRE(near3(floor->scale, glm::vec3(1.0f)));

    const SceneObject* rotating = findObject(**scene, "temporal-lab rotating cube");
    REQUIRE(rotating != nullptr);
    REQUIRE(near3(rotating->position, glm::vec3(-3.0f, 1.0f, 0.0f)));

    const SceneObject* reference = findObject(**scene, "temporal-lab reference cube");
    REQUIRE(reference != nullptr);
    REQUIRE(near3(reference->position, glm::vec3(3.0f, 1.0f, 0.0f)));

    const SceneObject* invalid = findObject(**scene, "temporal-lab invalid cube");
    REQUIRE(invalid != nullptr);
    REQUIRE(near3(invalid->position, glm::vec3(0.0f, 1.0f, 4.0f)));
    REQUIRE(invalid->motionClass == lmx::engine::MotionClass::Invalid);

    for (int i = 0; i < 5; ++i) {
        const SceneObject* pole = findObject(**scene, "temporal-lab pole " + std::to_string(i));
        REQUIRE(pole != nullptr);
        REQUIRE(
            near3(pole->position, glm::vec3(-1.0f + 0.5f * static_cast<float>(i), 1.5f, -4.0f)));
        REQUIRE(near3(pole->scale, glm::vec3(0.05f, 3.0f, 0.05f)));
    }

    const SceneObject* sign = findObject(**scene, "temporal-lab emissive sign");
    REQUIRE(sign != nullptr);
    REQUIRE(near3(sign->position, glm::vec3(0.0f, 2.5f, -6.0f)));

    // Everything except the floor, the reference cube and the invalid cube is tracked, and no
    // track ever drives the object the motion sentinel belongs to.
    REQUIRE((*scene)->animation.tracks.size() == 7);
    REQUIRE((*scene)->animation.duration == Catch::Approx(24.0));
    REQUIRE((*scene)->animation.loop);
    for (const RigidTrack& track : (*scene)->animation.tracks) {
        REQUIRE((*scene)->objects[track.objectIndex].motionClass ==
                lmx::engine::MotionClass::Rigid);
        REQUIRE(track.keys.size() == 24 * 60 + 1);
    }
}

//======================================================================================================================
TEST_CASE("loadTemporalLabScene's tracks close their loop and hit their documented periods",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadTemporalLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const auto trackFor = [&](std::string_view name) -> const RigidTrack& {
        const SceneObject* object = findObject(**scene, name);
        REQUIRE(object != nullptr);
        const auto index = static_cast<uint32_t>(object - (*scene)->objects.data());
        for (const RigidTrack& track : (*scene)->animation.tracks) {
            if (track.objectIndex == index) {
                return track;
            }
        }
        FAIL("no track for " + std::string(name));
        return (*scene)->animation.tracks.front();
    };

    // The cube turns once per 4 s: a quarter turn maps +X onto -Z.
    const RigidTrack& rotating = trackFor("temporal-lab rotating cube");
    const glm::vec3 turned =
        glm::vec3(sampleRigidTrack(rotating, 1.0) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    REQUIRE(near3(turned, glm::vec3(0.0f, 0.0f, -1.0f)));
    REQUIRE(matricesNear(sampleRigidTrack(rotating, 0.0), sampleRigidTrack(rotating, 24.0), 1e-4f));

    // The sphere orbits the reference cube once per 6 s at radius 2.
    const RigidTrack& orbit = trackFor("temporal-lab orbit sphere");
    REQUIRE(near3(glm::vec3(sampleRigidTrack(orbit, 0.0)[3]), glm::vec3(5.0f, 1.0f, 0.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(orbit, 1.5)[3]), glm::vec3(3.0f, 1.0f, 2.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(orbit, 3.0)[3]), glm::vec3(1.0f, 1.0f, 0.0f)));

    // The poles swing 0.5 in x once per 2 s, together.
    const RigidTrack& pole = trackFor("temporal-lab pole 0");
    REQUIRE(near3(glm::vec3(sampleRigidTrack(pole, 0.0)[3]), glm::vec3(-1.0f, 1.5f, -4.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(pole, 0.5)[3]), glm::vec3(-0.5f, 1.5f, -4.0f)));
    REQUIRE(near3(glm::vec3(sampleRigidTrack(pole, 1.5)[3]), glm::vec3(-1.5f, 1.5f, -4.0f)));

    // The camera loops every 8 s and opens on its own first key.
    const std::vector<CameraKey>& cameraTrack = (*scene)->animation.cameraTrack;
    REQUIRE(cameraTrack.size() == 24 * 60 + 1);
    REQUIRE(near3(cameraTrack.front().position, (*scene)->initialCamera.position));
    REQUIRE(cameraTrack.front().yaw == (*scene)->initialCamera.yaw);
    REQUIRE(cameraTrack.front().pitch == (*scene)->initialCamera.pitch);
    REQUIRE(near3(sampleCameraTrack(cameraTrack, 0.0).position, glm::vec3(0.0f, 3.0f, 10.0f)));
    REQUIRE(near3(sampleCameraTrack(cameraTrack, 4.0).position, glm::vec3(2.0f, 3.0f, 8.0f)));
    REQUIRE(near3(sampleCameraTrack(cameraTrack, 8.0).position, glm::vec3(0.0f, 3.0f, 10.0f)));
    REQUIRE(std::abs(sampleCameraTrack(cameraTrack, 2.0).yaw) == Catch::Approx(0.1f).margin(1e-3));

    // The sign flashes strength 0/4 every 3 s, a period of 6 s dividing the 24 s clip.
    const SceneObject* sign = findObject(**scene, "temporal-lab emissive sign");
    REQUIRE(sign != nullptr);
    const auto signIndex = static_cast<uint32_t>(sign - (*scene)->objects.data());
    const EmissiveTrack* signTrack = nullptr;
    for (const EmissiveTrack& track : (*scene)->animation.emissiveTracks) {
        if (track.objectIndex == signIndex) {
            signTrack = &track;
        }
    }
    REQUIRE(signTrack != nullptr);
    (*scene)->animate(1.0);
    REQUIRE((*scene)->objects[signIndex].emissiveStrength == Catch::Approx(0.0f));
    (*scene)->animate(4.0);
    REQUIRE((*scene)->objects[signIndex].emissiveStrength == Catch::Approx(4.0f));
    (*scene)->animate(6.0);
    REQUIRE((*scene)->objects[signIndex].emissiveStrength == Catch::Approx(0.0f));
}

//======================================================================================================================
// A pure projection check (no rendering): the initial camera frames every probe in a 1280x720
// viewport, the moving objects do not overlap the still ones they are read against, and the poles
// stay separated on screen across their whole swing.
TEST_CASE("loadTemporalLabScene frames its probes and keeps its poles separated", "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = loadTemporalLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    const lmx::engine::Camera camera = cameraFrom((*scene)->initialCamera);
    const auto boxAt = [&](const glm::vec3& center, const glm::vec3& halfExtent) {
        return projectAabbToFrame(camera, kWidth, kHeight, center, halfExtent);
    };
    const auto insideViewport = [&](const ScreenBox& box) {
        return box.minX >= 0.0f && box.maxX <= static_cast<float>(kWidth) && box.minY >= 0.0f &&
               box.maxY <= static_cast<float>(kHeight);
    };

    const ScreenBox rotatingBox =
        boxAt(glm::vec3(-3.0f, 1.0f, 0.0f), glm::vec3(0.71f, 0.5f, 0.71f));
    const ScreenBox referenceBox = boxAt(glm::vec3(3.0f, 1.0f, 0.0f), glm::vec3(0.5f));
    const ScreenBox invalidBox = boxAt(glm::vec3(0.0f, 1.0f, 4.0f), glm::vec3(0.5f));
    // The orbit's near and far extremes both have to stay in frame.
    const ScreenBox orbitNear = boxAt(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.4f));
    const ScreenBox orbitFar = boxAt(glm::vec3(5.0f, 1.0f, 0.0f), glm::vec3(0.4f));
    for (const ScreenBox& box : {rotatingBox, referenceBox, invalidBox, orbitNear, orbitFar}) {
        INFO("box: x[" + std::to_string(box.minX) + "," + std::to_string(box.maxX) + "] y[" +
             std::to_string(box.minY) + "," + std::to_string(box.maxY) + "]");
        REQUIRE(insideViewport(box));
    }
    REQUIRE(disjoint(rotatingBox, referenceBox));
    REQUIRE(disjoint(rotatingBox, invalidBox));
    REQUIRE(disjoint(invalidBox, referenceBox));
    REQUIRE(disjoint(referenceBox, orbitFar));

    for (float swing : {-0.5f, 0.0f, 0.5f}) {
        std::vector<ScreenBox> poleBoxes;
        for (int i = 0; i < 5; ++i) {
            const glm::vec3 center{-1.0f + 0.5f * static_cast<float>(i) + swing, 1.5f, -4.0f};
            poleBoxes.push_back(boxAt(center, glm::vec3(0.025f, 1.5f, 0.025f)));
            INFO("pole " + std::to_string(i) + " at swing " + std::to_string(swing));
            REQUIRE(insideViewport(poleBoxes.back()));
        }
        for (size_t i = 1; i < poleBoxes.size(); ++i) {
            INFO("poles " + std::to_string(i - 1) + " and " + std::to_string(i) + " at swing " +
                 std::to_string(swing));
            REQUIRE(disjoint(poleBoxes[i - 1], poleBoxes[i]));
        }
    }
}
