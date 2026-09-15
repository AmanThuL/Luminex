#include "Render/SceneView.h"
#include "Render/VisibilityTables.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("GPU candidate tables preserve sparse identities and canonical run order",
          "[render][visibility][submission]") {
    std::vector<InstanceRow> rows(9);
    std::vector<DrawItem> items(4);
    const std::array<uint32_t, 4> slots{8, 2, 6, 1};
    for (uint32_t i = 0; i < items.size(); ++i) {
        items[i].instanceRow = slots[i];
        rows[slots[i]].materialRow = i % 2;
    }
    VisibilityResult all;
    all.visibleItems = {0, 1, 2, 3};
    SceneView view;
    view.items = items;
    view.tables.instanceRows = rows;
    for (auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        view.submission = mode;
        auto prepared = buildDrawSubmission(view, all, all, mode);
        const auto tables = buildVisibilityTables(view, prepared, {});
        std::vector<uint32_t> actual;
        for (const auto& candidate : tables.candidates)
            actual.push_back(candidate.instanceRow);
        const auto expected = mode == SubmissionMode::Indirect
                                  ? std::vector<uint32_t>{8, 2, 6, 1, 8, 2, 6, 1}
                                  : std::vector<uint32_t>{8, 6, 2, 1, 8, 6, 2, 1};
        REQUIRE(actual == expected);
        REQUIRE(tables.views[0].firstCandidate == 0);
        REQUIRE(tables.views[1].firstCandidate == 4);
        REQUIRE(tables.views[1].candidateCount == 4);
        REQUIRE(tables.views[1].flags == 3);
        REQUIRE(tables.runs.size() == (mode == SubmissionMode::Indirect ? 8 : 4));
        REQUIRE(tables.chunks.size() == (mode == SubmissionMode::Indirect ? 2 : 4));
        for (uint32_t i = 0; i < tables.runs.size(); ++i) {
            const auto& run = tables.runs[i];
            REQUIRE(run.argumentIndex == i);
            REQUIRE(run.firstSlot == run.firstCandidate);
            REQUIRE(tables.candidates[run.firstCandidate].run == i);
        }
    }
}

//======================================================================================================================
TEST_CASE("GPU dense chunks include a carry beyond 256 chunks and empty views",
          "[render][visibility][submission]") {
    constexpr uint32_t count = 256 * 256 + 1;
    std::vector<InstanceRow> rows(count);
    std::vector<DrawItem> items(count);
    VisibilityResult all;
    all.visibleItems.resize(count);
    std::iota(all.visibleItems.begin(), all.visibleItems.end(), 0);
    for (uint32_t i = 0; i < count; ++i)
        items[i].instanceRow = i;
    SceneView view;
    view.items = items;
    view.tables.instanceRows = rows;
    view.submission = SubmissionMode::Batched;
    const auto prepared = buildDrawSubmission(view, all, all, view.submission);
    const auto tables = buildVisibilityTables(view, prepared, {});
    REQUIRE(tables.runs.size() == 2);
    REQUIRE(tables.views[0].chunkCount == 257);
    REQUIRE(tables.chunks[256].firstCandidate == 65536);
    REQUIRE(tables.chunks[256].candidateCount == 1);
    REQUIRE(tables.chunks[257].firstCandidate == count);
    REQUIRE(tables.chunks[257].run == 1);
    const auto empty = buildVisibilityTables({}, {}, {});
    REQUIRE(empty.candidates.empty());
    REQUIRE(empty.runs.empty());
    REQUIRE(empty.chunks.empty());
}

//======================================================================================================================
TEST_CASE("ordered fp32 visibility distinguishes one ulp around a plane", "[render][visibility]") {
    FrustumPlanes planes;
    planes.valid = true;
    planes.planes.fill({1, 0, 0, -1});
    InstanceRow row;
    row.worldBoundsMin = row.worldBoundsMax = {std::nextafter(1.0f, 0.0f), 0, 0};
    REQUIRE(classifyInstance(planes, row, 0).state == VisibilityState::Rejected);
    row.worldBoundsMin.x = row.worldBoundsMax.x = std::nextafter(1.0f, 2.0f);
    REQUIRE(classifyInstance(planes, row, 0).state == VisibilityState::Visible);
}

//======================================================================================================================
TEST_CASE("ordered visibility cancellation cannot contract into an FMA", "[render][visibility]") {
    FrustumPlanes planes;
    planes.valid = true;
    planes.planes.fill({-0.1f, 1.0f, 0, 0});
    REQUIRE(std::fma(-0.1f, 10.0f, 1.0f) < 0.0f);
    InstanceRow row;
    row.worldBoundsMin = row.worldBoundsMax = {10, 1, 0};
    REQUIRE(classifyInstance(planes, row, 0).state == VisibilityState::Visible);
    row.worldBoundsMin.x = row.worldBoundsMax.x = std::nextafter(10.0f, 11.0f);
    REQUIRE(classifyInstance(planes, row, 0).state == VisibilityState::Rejected);
    row.worldBoundsMin.x = row.worldBoundsMax.x = std::nextafter(10.0f, 9.0f);
    REQUIRE(classifyInstance(planes, row, 0).state == VisibilityState::Visible);
}
