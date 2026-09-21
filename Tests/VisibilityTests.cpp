#include "Engine/Types/Camera.h"
#include "Render/DrawSubmission.h"
#include "Render/SceneView.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("visibility extracts five inward guarded reversed infinite planes",
          "[render][visibility]") {
    Camera camera;
    camera.position = {0, 0, 0};
    camera.yaw = 0;
    camera.pitch = 0;
    camera.fovY = glm::radians(90.0f);
    camera.nearZ = 1.0f;
    const auto planes = extractFrustumPlanes(camera.projectionMatrix(1.0f));
    REQUIRE(planes.valid);
    for (const auto& plane : planes.planes)
        REQUIRE(glm::length(glm::vec3(plane)) == Catch::Approx(1.0f));
    REQUIRE(planes.planes[4].z == Catch::Approx(-1.0f));
    REQUIRE(planes.planes[4].w == Catch::Approx(-1.0f + kVisibilityGuardWorldUnits));
    InstanceRow row;
    auto state = [&](glm::vec3 minimum, glm::vec3 maximum) {
        row.worldBoundsMin = minimum;
        row.worldBoundsMax = maximum;
        return classifyInstance(planes, row, 7).state;
    };
    REQUIRE(state({-0.1f, -0.1f, -3}, {0.1f, 0.1f, -2}) == VisibilityState::Visible);
    REQUIRE(state({-1, -1, -1000000}, {1, 1, -999999}) == VisibilityState::Visible);
    REQUIRE(state({-4, -0.1f, -2}, {-3, 0.1f, -1.5f}) == VisibilityState::Rejected);
    REQUIRE(state({3, -0.1f, -2}, {4, 0.1f, -1.5f}) == VisibilityState::Rejected);
    REQUIRE(state({-0.1f, -4, -2}, {0.1f, -3, -1.5f}) == VisibilityState::Rejected);
    REQUIRE(state({-0.1f, 3, -2}, {0.1f, 4, -1.5f}) == VisibilityState::Rejected);
    REQUIRE(state({-0.1f, -0.1f, -0.5f}, {0.1f, 0.1f, 0.5f}) == VisibilityState::Rejected);
    REQUIRE(state({-4, -4, -4}, {4, 4, 4}) == VisibilityState::Visible);
    for (const auto& center : {glm::vec3{-2, 0, -2}, glm::vec3{2, 0, -2}, glm::vec3{0, -2, -2},
                               glm::vec3{0, 2, -2}, glm::vec3{0, 0, -1}})
        REQUIRE(state(center - glm::vec3(0.1f), center + glm::vec3(0.1f)) ==
                VisibilityState::Visible);
    REQUIRE(state({0, 0, -0.9995f}, {0, 0, -0.9995f}) == VisibilityState::Visible);
    row.flags = kInstanceBoundsUnreliable;
    REQUIRE(classifyInstance(planes, row, 7).reason == VisibilityReason::UnreliableBounds);
    row.model[0][0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(classifyInstance(planes, row, 7).reason == VisibilityReason::NonFiniteTransform);
    REQUIRE(classifyInstance(planes, row, 7, false).reason == VisibilityReason::Disabled);
    REQUIRE(classifyInstance(planes, row, 7, true, true).reason == VisibilityReason::ViewUnculled);
}

//======================================================================================================================
TEST_CASE("draw submission preserves sparse rows and separates batched argument offsets",
          "[render][submission]") {
    std::vector<InstanceRow> rows(8);
    std::vector<DrawItem> items(4);
    const std::array<uint32_t, 4> slots{7, 2, 5, 1};
    for (uint32_t i = 0; i < 4; ++i) {
        items[i].instanceRow = slots[i];
        items[i].mesh.firstIndex = 9;
        items[i].mesh.indexCount = 6;
        rows[slots[i]].materialRow = i == 3 ? 2 : 1;
        rows[slots[i]].meshRow = 3;
    }
    SceneView view;
    view.items = items;
    view.tables.instanceRows = rows;
    VisibilityResult scene;
    scene.visibleItems = {0, 1, 2};
    VisibilityResult shadow;
    shadow.visibleItems = {0, 1, 2, 3};
    for (const auto mode : {SubmissionMode::Direct, SubmissionMode::Indirect}) {
        const auto output = buildDrawSubmission(view, scene, shadow, mode);
        REQUIRE(output.rows == std::vector<uint32_t>{7, 2, 5, 7, 2, 5, 1});
        REQUIRE(output.arguments.size() == 7);
        REQUIRE(output.shadow.firstEntry == 3);
        for (uint32_t i = 0; i < 7; ++i) {
            REQUIRE(output.arguments[i].firstInstance == i);
            REQUIRE(output.arguments[i].instanceCount == 1);
            REQUIRE(output.arguments[i].indexCount == 6);
            REQUIRE(output.arguments[i].firstIndex == 9);
            REQUIRE(output.arguments[i].baseVertex == 0);
        }
    }
    auto batch = buildDrawSubmission(view, scene, shadow, SubmissionMode::Batched);
    REQUIRE(batch.rows == std::vector<uint32_t>{7, 2, 5, 7, 2, 5, 1});
    REQUIRE(batch.scene.runs.size() == 1);
    REQUIRE(batch.shadow.runs.size() == 2);
    REQUIRE(batch.shadow.runs[0].firstEntry == 3);
    REQUIRE(batch.shadow.runs[0].argumentIndex == 1);
    REQUIRE(batch.arguments[1].firstInstance == 3);
    REQUIRE(batch.arguments[1].instanceCount == 3);
    REQUIRE(batch.arguments[2].firstInstance == 6);
    items[1].doubleSided = true;
    batch = buildDrawSubmission(view, scene, shadow, SubmissionMode::Batched);
    REQUIRE(batch.scene.runs.size() == 2);
    scene.visibleItems.clear();
    shadow.visibleItems.clear();
    const auto empty = buildDrawSubmission(view, scene, shadow, SubmissionMode::Batched);
    REQUIRE(empty.rows.empty());
    REQUIRE(empty.arguments.empty());
}
