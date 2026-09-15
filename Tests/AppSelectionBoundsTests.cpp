//----------------------------------------------------------------------------------------------------------------------
/// @file AppSelectionBoundsTests.cpp
/// @brief Tests reliable transformed bounds and camera framing across object scales.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "App/Model/SelectionBounds.h"

#include <cmath>
#include <limits>

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("selection bounds transform all corners under rotation and negative scale", "[app]") {
    scene::SceneObject object;
    scene::Scene scene;
    auto mesh = render::makeCube();
    for (auto& vertex : mesh.vertices) {
        vertex.px *= 2.0f;
        vertex.py *= 4.0f;
    }
    object.mesh = scene.addMesh(std::move(mesh), "selection.bounds");
    object.position = {10, 20, 30};
    object.eulerDegrees = {0, 0, 90};
    object.scale = {-2, 3, 4};
    const auto bounds = objectWorldBounds(scene, object);
    REQUIRE(bounds.has_value());
    REQUIRE(bounds->minimum.x == Catch::Approx(4));
    REQUIRE(bounds->minimum.y == Catch::Approx(18));
    REQUIRE(bounds->minimum.z == Catch::Approx(28));
    REQUIRE(bounds->maximum.x == Catch::Approx(16));
    REQUIRE(bounds->maximum.y == Catch::Approx(22));
    REQUIRE(bounds->maximum.z == Catch::Approx(32));
}

//======================================================================================================================
TEST_CASE("selection framing contains tiny and large geometry in portrait and wide viewports",
          "[app]") {
    for (const float size : {0.00001f, 100000.0f}) {
        for (const float aspect : {0.5f, 2.0f}) {
            render::Camera camera;
            camera.yaw = 0.7f;
            camera.pitch = 0.2f;
            const render::Aabb bounds{.minimum = glm::vec3(-size), .maximum = glm::vec3(size)};
            REQUIRE(frameSelection(camera, bounds, aspect));
            const auto matrix = camera.projectionMatrix(aspect) * camera.viewMatrix();
            for (unsigned corner = 0; corner < 8; ++corner) {
                const glm::vec3 world((corner & 1) ? size : -size, (corner & 2) ? size : -size,
                                      (corner & 4) ? size : -size);
                const glm::vec4 clip = matrix * glm::vec4(world, 1.0f);
                REQUIRE(clip.w > 0.0f);
                const glm::vec3 projected = glm::vec3(clip) / clip.w;
                REQUIRE(std::abs(projected.x) < 1.0f);
                REQUIRE(std::abs(projected.y) < 1.0f);
                REQUIRE(projected.z > 0.0f);
                REQUIRE(projected.z < 1.0f);
            }
            REQUIRE(camera.yaw == 0.7f);
            REQUIRE(camera.pitch == 0.2f);
            REQUIRE(camera.moveSpeed > 0.0f);
        }
    }
}

//======================================================================================================================
TEST_CASE("missing and invalid selection bounds remain unavailable without moving the camera",
          "[app]") {
    scene::Scene scene;
    scene.objects.emplace_back();
    const EditorSelection selection{
        .sceneId = scene::SceneId{0}, .subject = EditorSubject::Object, .index = 0};
    REQUIRE_FALSE(selectedObjectBounds(scene, selection));
    scene.objects[0].mesh = scene.addMesh({}, "selection.empty");
    REQUIRE_FALSE(selectedObjectBounds(scene, selection));
    scene.objects[0].mesh = scene.addMesh(render::makeCube(), "selection.cube");
    scene.objects[0].position.x = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(selectedObjectBounds(scene, selection));
    render::Camera camera;
    camera.position = {1, 2, 3};
    REQUIRE_FALSE(frameSelection(camera, *scene.meshBounds(scene.objects[0].mesh), 0.0f));
    REQUIRE(camera.position == glm::vec3(1, 2, 3));
}
