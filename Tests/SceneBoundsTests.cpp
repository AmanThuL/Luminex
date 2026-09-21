//----------------------------------------------------------------------------------------------------------------------
/// @file SceneBoundsTests.cpp
/// @brief Tests shared mesh bounds, canonical uploaded world rows, and static dirty convergence.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Math/Aabb.h"
#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Scene/Scene.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <limits>

using namespace lmx;

//======================================================================================================================
TEST_CASE("mesh bounds derive from real geometry and retain planar surfaces", "[scene][bounds]") {
    engine::Scene scene;
    const auto cube = scene.addMesh(engine::makeCube(), "bounds.cube");
    const auto plane = scene.addMesh(engine::fromGeo(asset::makeGrid(8, 4, 2, 2)), "bounds.plane");
    REQUIRE(scene.meshBounds(cube));
    REQUIRE(scene.meshBounds(cube)->minimum == glm::vec3(-0.5f));
    REQUIRE(scene.meshBounds(cube)->maximum == glm::vec3(0.5f));
    REQUIRE(scene.meshBounds(plane)->minimum == glm::vec3(-4, 0, -2));
    REQUIRE(scene.meshBounds(plane)->maximum == glm::vec3(4, 0, 2));
    const auto* row = scene.tryMesh(plane);
    REQUIRE(row->boundsMin == scene.meshBounds(plane)->minimum);
    REQUIRE(row->boundsMax == scene.meshBounds(plane)->maximum);
    REQUIRE_FALSE(scene.meshBounds(engine::MeshId{}));

    auto degenerate = engine::makeCube();
    for (auto& vertex : degenerate.vertices) {
        vertex.py = vertex.pz = 0;
    }
    REQUIRE_FALSE(scene.meshBounds(scene.addMesh(std::move(degenerate), "bounds.line")));
    REQUIRE_FALSE(scene.meshBounds(scene.addMesh({}, "bounds.empty")));
    auto nonfinite = engine::makeCube();
    nonfinite.vertices[0].px = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(scene.meshBounds(scene.addMesh(std::move(nonfinite), "bounds.nonfinite")));
}

//======================================================================================================================
TEST_CASE("shared bounds reject invalid and overflowing transforms conservatively",
          "[render][bounds]") {
    const lmx::Aabb bounds{{-1, -2, -3}, {1, 2, 3}};
    REQUIRE(lmx::transformAabb(glm::mat4(1), bounds));
    REQUIRE_FALSE(lmx::transformAabb(glm::mat4(1), {{2, 0, 0}, {1, 1, 1}}));
    glm::mat4 invalid(1);
    invalid[3][3] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(lmx::transformAabb(invalid, bounds));
    glm::mat4 overflow(std::numeric_limits<float>::max());
    REQUIRE_FALSE(lmx::transformAabb(overflow, bounds));
}

//======================================================================================================================
TEST_CASE("prepared bounds are canonical uploaded rows and static slots converge",
          "[gpu][scene][bounds]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    engine::Scene scene;
    const auto mesh = scene.addMesh(engine::makeCube(), "bounds.upload.cube");
    const auto empty = scene.addMesh({}, "bounds.upload.empty");
    const auto material = scene.addMaterial({});
    scene.addObject({.name = "transformed",
                     .position = {10, 20, 30},
                     .eulerDegrees = {0, 0, 90},
                     .scale = {-2, 3, 4},
                     .mesh = mesh,
                     .material = material});
    scene.addObject({.name = "unreliable", .mesh = empty, .material = material});
    REQUIRE(scene.finalize(**device));
    for (uint32_t frame = 0; frame < 4; ++frame) {
        (*device)->beginFrame();
        REQUIRE(scene.prepareFrame((*device)->frameNumber()));
        if (frame == 3) {
            REQUIRE(scene.tableStats().rowsWritten == 0);
            REQUIRE(scene.tableStats().bytesWritten == 0);
        }
        (*device)->endFrame(nullptr);
    }
    (*device)->waitIdle();
    const auto tables = scene.tables();
    REQUIRE(tables.instanceRows.size() == 2);
    REQUIRE(tables.instanceCapacity >= 2);
    std::array<engine::InstanceRow, 2> uploaded;
    tables.instances->readback(uploaded.data(), sizeof(uploaded));
    REQUIRE(std::memcmp(uploaded.data(), tables.instanceRows.data(), sizeof(uploaded)) == 0);
    REQUIRE(uploaded[0].worldBoundsMin.x == Catch::Approx(8.5f));
    REQUIRE(uploaded[0].worldBoundsMin.y == Catch::Approx(19.0f));
    REQUIRE(uploaded[0].worldBoundsMax.z == Catch::Approx(32.0f));
    REQUIRE((uploaded[0].flags & engine::kInstanceBoundsUnreliable) == 0);
    REQUIRE((uploaded[1].flags & engine::kInstanceBoundsUnreliable) != 0);
    scene.objects[0].position.x += 5;
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()));
    REQUIRE(scene.tables().instanceRows[0].worldBoundsMin.x == Catch::Approx(13.5f));
    REQUIRE(scene.tableStats().rowsWritten == 1);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    scene.objects[0].position.x = std::numeric_limits<float>::infinity();
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()));
    REQUIRE((scene.tables().instanceRows[0].flags & engine::kInstanceBoundsUnreliable) != 0);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("bulk instance creation still reuses the lowest removed slot", "[scene][bounds]") {
    engine::Scene scene;
    const auto mesh = scene.addMesh(engine::makeCube(), "bulk.cube");
    const auto material = scene.addMaterial({});
    std::vector<engine::InstanceId> ids;
    for (uint32_t i = 0; i < 128; ++i) {
        ids.push_back(scene.addObject({.mesh = mesh, .material = material}));
        REQUIRE(ids.back().slot == i);
    }
    for (uint32_t slot : {90u, 7u, 33u}) {
        scene.removeObject(ids[slot]);
    }
    for (uint32_t slot : {7u, 33u, 90u}) {
        const auto replacement = scene.addObject({.mesh = mesh, .material = material});
        REQUIRE(replacement.slot == slot);
        REQUIRE(replacement.generation == ids[slot].generation + 1);
        REQUIRE_FALSE(scene.tryObject(ids[slot]));
    }
    REQUIRE(scene.addObject({.mesh = mesh, .material = material}).slot == 128);
}
