#include "Engine/View/Camera.h"
#include "Render/Passes/Scene/DrawSubmission.h"
#include "Render/Renderer/SceneView.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <vector>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("visibility extracts five inward guarded reversed infinite planes",
          "[render][visibility]") {
    lmx::engine::Camera camera;
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
    lmx::engine::InstanceRow row;
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
    row.flags = lmx::engine::kInstanceBoundsUnreliable;
    REQUIRE(classifyInstance(planes, row, 7).reason == VisibilityReason::UnreliableBounds);
    row.model[0][0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(classifyInstance(planes, row, 7).reason == VisibilityReason::NonFiniteTransform);
    REQUIRE(classifyInstance(planes, row, 7, false).reason == VisibilityReason::CullingOff);
    REQUIRE(classifyInstance(planes, row, 7, true, true).reason == VisibilityReason::ViewUnculled);
}

//======================================================================================================================
TEST_CASE("draw submission preserves sparse rows and separates batched argument offsets",
          "[render][submission]") {
    std::vector<lmx::engine::InstanceRow> rows(8);
    std::vector<lmx::engine::DrawItem> items(4);
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
    REQUIRE(sizeof(rojoRHI::DrawIndexedIndirectArgs) == 24);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, drawId) == 0);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, indexCount) == 4);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, instanceCount) == 8);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, firstIndex) == 12);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, baseVertex) == 16);
    REQUIRE(offsetof(rojoRHI::DrawIndexedIndirectArgs, firstInstance) == 20);
    for (const auto mode : {SubmissionMode::Direct, SubmissionMode::Indirect}) {
        const auto output = buildDrawSubmission(view, scene, shadow, mode);
        REQUIRE(output.rows == std::vector<uint32_t>{7, 2, 5, 7, 2, 5, 1});
        REQUIRE(output.arguments.size() == 7);
        REQUIRE(output.shadow.firstEntry == 3);
        for (uint32_t i = 0; i < 7; ++i) {
            REQUIRE(output.arguments[i].drawId == i);
            REQUIRE(output.arguments[i].firstInstance == 0);
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
    REQUIRE(batch.arguments[0].drawId == 0);
    REQUIRE(batch.arguments[0].instanceCount == 3);
    REQUIRE(batch.arguments[1].drawId == 3);
    REQUIRE(batch.arguments[1].firstInstance == 0);
    REQUIRE(batch.arguments[1].instanceCount == 3);
    REQUIRE(batch.arguments[2].drawId == 6);
    REQUIRE(batch.arguments[2].instanceCount == 1);
    for (const auto& args : batch.arguments) {
        REQUIRE(args.firstInstance == 0);
        REQUIRE(args.baseVertex == 0);
    }
    items[1].doubleSided = true;
    batch = buildDrawSubmission(view, scene, shadow, SubmissionMode::Batched);
    REQUIRE(batch.scene.runs.size() == 2);
    scene.visibleItems.clear();
    shadow.visibleItems.clear();
    const auto empty = buildDrawSubmission(view, scene, shadow, SubmissionMode::Batched);
    REQUIRE(empty.rows.empty());
    REQUIRE(empty.arguments.empty());
}

namespace {
// Items i of pipeline group groups[i] (0 opaque, alternating sides; 1 masked single-sided; 2 masked
// double-sided), each with its own instance row, material and mesh, so batched runs stay one item.
struct GroupedItems {
    std::vector<lmx::engine::InstanceRow> rows;
    std::vector<lmx::engine::DrawItem> items;
    SceneView view;
    VisibilityResult all;
};

//======================================================================================================================
// Fills `scene` in place, so its view keeps borrowing the rows and items it owns.
void populate(GroupedItems& scene, const std::vector<uint32_t>& groups) {
    auto& [rows, items, view, all] = scene;
    rows.assign(groups.size(), {});
    items.assign(groups.size(), {});
    for (uint32_t i = 0; i < groups.size(); ++i) {
        items[i].instanceRow = i;
        items[i].mesh.firstIndex = 3 * i;
        items[i].mesh.indexCount = 3;
        items[i].alphaMode =
            groups[i] == 0 ? lmx::engine::AlphaMode::Opaque : lmx::engine::AlphaMode::Mask;
        items[i].doubleSided = groups[i] == 2 || (groups[i] == 0 && i % 2 == 1);
        rows[i].materialRow = i;
        rows[i].meshRow = i;
        all.visibleItems.push_back(i);
    }
    view.items = items;
    view.tables.instanceRows = rows;
}

//======================================================================================================================
// Every run of a list names its item's instance row, mesh and the argument record at its index.
void requireRunsMatchArguments(const PreparedSubmission& output, const DrawList& list,
                               const GroupedItems& scene) {
    for (const auto& run : list.runs) {
        const auto& args = output.arguments[run.argumentIndex];
        REQUIRE(args.drawId == run.firstEntry);
        REQUIRE(args.instanceCount == run.instanceCount);
        REQUIRE(args.firstIndex == scene.items[run.itemIndex].mesh.firstIndex);
        REQUIRE(output.rows[run.firstEntry] == scene.items[run.itemIndex].instanceRow);
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("draw submission keeps scene groups below the counted threshold in visibility order",
          "[render][submission]") {
    // A light scene: 38 runs over all three groups, and one group exactly one run short.
    for (const uint32_t opaque : {20u, kCountedDrawMinRuns - 1}) {
        std::vector<uint32_t> groups;
        for (uint32_t i = 0; i < opaque + 18; ++i)
            groups.push_back(i < opaque ? 0u : 1u + i % 2);
        std::rotate(groups.begin(), groups.begin() + 7, groups.end());
        GroupedItems scene;
        populate(scene, groups);
        for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
            const auto output = buildDrawSubmission(scene.view, scene.all, scene.all, mode);
            REQUIRE(output.scene.countedDraws.empty());
            REQUIRE(output.shadow.countedDraws.empty());
            // Untouched, both lists keep the builder's order: visibility order when indirect, the
            // batching key's when batched.
            REQUIRE(output.scene.runs.size() == groups.size());
            REQUIRE(output.shadow.runs.size() == groups.size());
            for (uint32_t i = 0; i < groups.size(); ++i) {
                REQUIRE(output.scene.runs[i].itemIndex == output.shadow.runs[i].itemIndex);
                if (mode == SubmissionMode::Indirect)
                    REQUIRE(output.scene.runs[i].itemIndex == i);
                REQUIRE(output.scene.runs[i].argumentIndex == i);
                REQUIRE(output.shadow.runs[i].argumentIndex == groups.size() + i);
            }
            requireRunsMatchArguments(output, output.scene, scene);
            requireRunsMatchArguments(output, output.shadow, scene);
        }
    }
}

//======================================================================================================================
TEST_CASE("draw submission draws scene groups at the threshold as leading counted draws",
          "[render][submission]") {
    // VisibilityLab's pattern: opaque, opaque, masked, masked double-sided. The opaque group
    // reaches the threshold exactly, the masked groups stay below it.
    const uint32_t count = 2 * kCountedDrawMinRuns;
    std::vector<uint32_t> groups;
    for (uint32_t i = 0; i < count; ++i)
        groups.push_back(i % 4 < 2 ? 0u : i % 4 - 1);
    GroupedItems scene;
    populate(scene, groups);
    for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        const auto output = buildDrawSubmission(scene.view, scene.all, scene.all, mode);
        REQUIRE(output.scene.countedDraws == std::vector<uint32_t>{kCountedDrawMinRuns});
        REQUIRE(output.shadow.countedDraws.empty());
        // The opaque runs lead in visibility order, both sides together; the rest keep theirs.
        std::vector<uint32_t> expected;
        for (uint32_t pass : {0u, 1u})
            for (uint32_t i = 0; i < count; ++i)
                if ((groups[i] == 0) == (pass == 0))
                    expected.push_back(i);
        REQUIRE(output.scene.runs.size() == count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t item = output.scene.runs[i].itemIndex;
            REQUIRE((sceneDrawGroup(scene.items[item]) == 0) == (i < kCountedDrawMinRuns));
            REQUIRE(output.scene.runs[i].argumentIndex == i);
            REQUIRE(output.scene.runs[i].firstEntry == i);
            REQUIRE(output.shadow.runs[i].argumentIndex == count + i);
            if (mode == SubmissionMode::Indirect) {
                REQUIRE(item == expected[i]);
                REQUIRE(output.shadow.runs[i].itemIndex == i);
            }
        }
        requireRunsMatchArguments(output, output.scene, scene);
        requireRunsMatchArguments(output, output.shadow, scene);
    }
    // Direct submission never draws counted.
    const auto direct =
        buildDrawSubmission(scene.view, scene.all, scene.all, SubmissionMode::Direct);
    REQUIRE(direct.scene.countedDraws.empty());
    REQUIRE(direct.scene.runs[2].itemIndex == 2);
    // Every group at the threshold: three counted draws in group order, nothing after them.
    std::vector<uint32_t> three;
    for (uint32_t i = 0; i < 3 * kCountedDrawMinRuns; ++i)
        three.push_back(2 - i % 3);
    GroupedItems all;
    populate(all, three);
    const auto output = buildDrawSubmission(all.view, all.all, all.all, SubmissionMode::Indirect);
    REQUIRE(output.scene.countedDraws == std::vector<uint32_t>(3, kCountedDrawMinRuns));
    for (uint32_t i = 0; i < output.scene.runs.size(); ++i)
        REQUIRE(sceneDrawGroup(all.items[output.scene.runs[i].itemIndex]) ==
                i / kCountedDrawMinRuns);
    requireRunsMatchArguments(output, output.scene, all);
}

//======================================================================================================================
TEST_CASE("draw submission splits counted draws evenly at maxDrawIndirectCount",
          "[render][submission]") {
    for (const uint32_t count :
         {kCountedDrawMaxRecords, kCountedDrawMaxRecords + 1, 2 * kCountedDrawMaxRecords + 5}) {
        std::vector<uint32_t> groups(count, 0u);
        groups.push_back(1); // one masked run, drawn lone after the counted draws
        GroupedItems scene;
        populate(scene, groups);
        VisibilityResult none;
        const auto output =
            buildDrawSubmission(scene.view, scene.all, none, SubmissionMode::Indirect);
        const auto& draws = output.scene.countedDraws;
        const uint32_t parts = (count + kCountedDrawMaxRecords - 1) / kCountedDrawMaxRecords;
        REQUIRE(draws.size() == parts);
        uint32_t covered = 0;
        for (const uint32_t draw : draws) {
            REQUIRE(draw <= kCountedDrawMaxRecords);
            REQUIRE(draw >= count / parts);
            covered += draw;
        }
        REQUIRE(covered == count);
        REQUIRE(output.scene.runs.back().itemIndex == count);
        REQUIRE(output.scene.runs.back().argumentIndex == count);
    }
    std::vector<uint32_t> odd(kCountedDrawMaxRecords + 1, 0u);
    GroupedItems scene;
    populate(scene, odd);
    const auto output = buildDrawSubmission(scene.view, scene.all, {}, SubmissionMode::Indirect);
    REQUIRE(output.scene.countedDraws ==
            std::vector<uint32_t>{kCountedDrawMaxRecords / 2 + 1, kCountedDrawMaxRecords / 2});
}

//======================================================================================================================
TEST_CASE("authored-off rows precede every bypass and retain identity without contributing counts",
          "[render][visibility][disabled-instance]") {
    std::array<lmx::engine::InstanceRow, 4> rows;
    std::array<lmx::engine::DrawItem, 4> items;
    FrustumPlanes planes;
    planes.valid = true;
    planes.planes.fill({1, 0, 0, -1});
    for (uint32_t i = 0; i < rows.size(); ++i) {
        rows[i].worldBoundsMin = rows[i].worldBoundsMax = {i == 3 ? 0.0f : 2.0f, 0, 0};
        items[i].instanceRow = i;
        items[i].mesh.indexCount = 6;
    }
    rows[0].flags = lmx::engine::kInstanceDisabled;
    rows[1].flags = lmx::engine::kInstanceDisabled | lmx::engine::kInstanceBoundsUnreliable;
    rows[1].model[0][0] = std::numeric_limits<float>::quiet_NaN();
    SceneView view;
    view.items = items;
    view.tables.instanceRows = rows;
    for (bool culling : {false, true}) {
        for (bool unculled : {false, true}) {
            const auto result = classifyView(planes, items, view.tables, culling, unculled);
            REQUIRE(result.candidates.size() == 4);
            REQUIRE(result.disabled == 2);
            for (uint32_t i : {0u, 1u}) {
                REQUIRE(result.candidates[i].instanceRow == i);
                REQUIRE(result.candidates[i].state == VisibilityState::Rejected);
                REQUIRE(result.candidates[i].reason == VisibilityReason::AuthoredOff);
            }
            const bool bypass = !culling || unculled;
            REQUIRE(result.visible == (bypass ? 0 : 1));
            REQUIRE(result.rejected == (bypass ? 0 : 1));
            REQUIRE(result.visibleItems ==
                    (bypass ? std::vector<uint32_t>{2, 3} : std::vector<uint32_t>{2}));
            REQUIRE(result.bypassed[static_cast<size_t>(VisibilityReason::CullingOff)] ==
                    (!culling ? 2 : 0));
            REQUIRE(result.bypassed[static_cast<size_t>(VisibilityReason::ViewUnculled)] ==
                    (culling && unculled ? 2 : 0));
            const auto shadow = classifyView(planes, items, view.tables, culling, true);
            for (auto mode :
                 {SubmissionMode::Direct, SubmissionMode::Indirect, SubmissionMode::Batched}) {
                const auto submission = buildDrawSubmission(view, result, shadow, mode);
                for (uint32_t row : submission.rows)
                    REQUIRE(row >= 2);
                uint32_t submitted = 0;
                for (const auto& args : submission.arguments)
                    submitted += args.instanceCount;
                REQUIRE(submitted == result.visibleItems.size() + 2);
            }
        }
    }
}
