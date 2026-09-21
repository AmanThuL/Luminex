//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLightTableTests.cpp
/// @brief Tests the paced local light table's identities, dirty tracking, growth and capacity.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"
#include <rojoRHI/RHI.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <vector>

using namespace lmx;

namespace {
//======================================================================================================================
render::LocalLight testLight(float x = 0.0f) {
    return {.type = render::LocalLightType::Point,
            .position = {x, 0.0f, 0.0f},
            .colour = {1.0f, 1.0f, 1.0f},
            .intensity = 1.0f,
            .range = 5.0f};
}

//======================================================================================================================
render::LightRow readLightRow(rojoRHI::Buffer& buffer, uint32_t slot) {
    std::vector<render::LightRow> rows(slot + 1);
    buffer.readback(rows.data(), rows.size() * sizeof(render::LightRow));
    return rows[slot];
}

//======================================================================================================================
void prepareLights(rojoRHI::Device& device, scene::Scene& scene) {
    device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()).has_value());
    device.endFrame(nullptr);
    device.waitIdle();
}
} // namespace

//======================================================================================================================
TEST_CASE("Local light handles reject foreign stores and stale generations", "[scene][light]") {
    scene::Scene first;
    scene::Scene second;
    const auto a = first.addLight(testLight());
    REQUIRE(a.has_value());
    const auto b = first.addLight(testLight(1.0f));
    REQUIRE(b.has_value());
    REQUIRE(*a != *b);
    REQUIRE(first.light(*a));
    REQUIRE_FALSE(second.light(*a));
    REQUIRE_FALSE(first.light({}));
    REQUIRE_FALSE(first.removeLight({}));
    REQUIRE_FALSE(second.removeLight(*a));
    REQUIRE(first.removeLight(*a));
    REQUIRE_FALSE(first.light(*a));
    REQUIRE_FALSE(first.removeLight(*a));
}

//======================================================================================================================
TEST_CASE("Removing a local light reuses its slot with a new generation and stable neighbours",
          "[scene][light]") {
    scene::Scene scene;
    const auto a = scene.addLight(testLight());
    REQUIRE(a.has_value());
    const auto b = scene.addLight(testLight(1.0f));
    REQUIRE(b.has_value());
    REQUIRE(scene.removeLight(*a));
    const auto replacement = scene.addLight(testLight(2.0f));
    REQUIRE(replacement.has_value());
    REQUIRE(replacement->slot == a->slot);
    REQUIRE(replacement->generation != a->generation);
    REQUIRE_FALSE(scene.light(*a));
    REQUIRE(scene.light(*replacement)->position.x == 2.0f);
    REQUIRE(scene.light(*b)->position.x == 1.0f);
    const auto live = scene.localLights();
    REQUIRE(live.size() == 2);
    REQUIRE(std::ranges::find(live, *b) != live.end());
    REQUIRE(std::ranges::find(live, *replacement) != live.end());
}

//======================================================================================================================
TEST_CASE("Adding a local light beyond the frozen capacity fails", "[scene][light]") {
    scene::Scene scene;
    for (uint32_t i = 0; i < render::kMaxLocalLights; ++i) {
        auto light = testLight(static_cast<float>(i));
        light.enabled = false;
        const auto id = scene.addLight(light);
        INFO(i);
        REQUIRE(id.has_value());
    }
    REQUIRE(scene.localLights().size() == render::kMaxLocalLights);
    const auto overflow = scene.addLight(testLight());
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code == rojoRHI::ErrorCode::InvalidDesc);
}

//======================================================================================================================
TEST_CASE("Adding or updating an invalid local light fails without asserting", "[scene][light]") {
    scene::Scene scene;
    const auto invalid = scene.addLight({.range = -1.0f});
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error().code == rojoRHI::ErrorCode::InvalidDesc);
    const auto id = scene.addLight(testLight());
    REQUIRE(id.has_value());
    const auto badUpdate = scene.updateLight(*id, {.range = -1.0f});
    REQUIRE_FALSE(badUpdate.has_value());
    // The failed update must not have mutated the stored light.
    REQUIRE(scene.light(*id)->range == 5.0f);
    const auto staleUpdate = scene.updateLight({}, testLight());
    REQUIRE_FALSE(staleUpdate.has_value());
}

//======================================================================================================================
TEST_CASE("Local light edits and removals leave scene coverage unchanged", "[scene][light]") {
    scene::Scene scene;
    const auto mesh = scene.addMesh(render::makeCube(), "lmx.test.light.coverage.mesh");
    const auto material = scene.addMaterial({});
    scene.addObject({.mesh = mesh, .material = material});
    const auto before = scene.coverageEpoch();
    const auto id = scene.addLight(testLight());
    REQUIRE(id.has_value());
    REQUIRE(scene.coverageEpoch() == before);
    REQUIRE(scene.updateLight(*id, testLight(3.0f)).has_value());
    REQUIRE(scene.coverageEpoch() == before);
    REQUIRE(scene.removeLight(*id));
    REQUIRE(scene.coverageEpoch() == before);
}

//======================================================================================================================
TEST_CASE("Prepared local light edits and removals leave scene coverage unchanged",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto mesh = scene.addMesh(render::makeCube(), "lmx.test.light.preparedCoverage.mesh");
    const auto material = scene.addMaterial({});
    scene.addObject({.mesh = mesh, .material = material});
    REQUIRE(scene.finalize(**device).has_value());
    prepareLights(**device, scene);
    const auto before = scene.coverageEpoch();
    const auto id = scene.addLight(testLight());
    REQUIRE(id.has_value());
    prepareLights(**device, scene);
    REQUIRE(scene.coverageEpoch() == before);
    REQUIRE(scene.updateLight(*id, testLight(3.0f)).has_value());
    prepareLights(**device, scene);
    REQUIRE(scene.coverageEpoch() == before);
    REQUIRE(scene.removeLight(*id));
    prepareLights(**device, scene);
    REQUIRE(scene.coverageEpoch() == before);
}

//======================================================================================================================
TEST_CASE("A light orbit track's animation index freezes at finalize and never regrows across "
          "repeated post-finalize add/remove",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto a = scene.addLight(testLight(0.0f));
    REQUIRE(a.has_value());
    const auto b = scene.addLight(testLight(1.0f));
    REQUIRE(b.has_value());
    REQUIRE(scene.animationLightId(0) == *a);
    REQUIRE(scene.animationLightId(1) == *b);
    REQUIRE_FALSE(scene.animationLightId(2).has_value());

    REQUIRE(scene.finalize(**device).has_value());

    // A light added after finalize -- e.g. a runtime pile addition -- gets no animation index: it
    // is static by contract, and the frozen list must not grow to make room for it.
    auto c = scene.addLight(testLight(2.0f));
    REQUIRE(c.has_value());
    REQUIRE_FALSE(scene.animationLightId(2).has_value());
    REQUIRE(scene.animationLightId(0) == *a);
    REQUIRE(scene.animationLightId(1) == *b);

    // Repeated post-finalize add/remove cycles must never grow the frozen list either.
    for (int i = 0; i < 100; ++i) {
        REQUIRE(scene.removeLight(*c));
        c = scene.addLight(testLight(2.0f));
        REQUIRE(c.has_value());
    }
    REQUIRE_FALSE(scene.animationLightId(2).has_value());
    REQUIRE_FALSE(scene.animationLightId(3).has_value());
    REQUIRE(scene.animationLightId(0) == *a);
    REQUIRE(scene.animationLightId(1) == *b);
}

//======================================================================================================================
TEST_CASE("A zero-light scene reports no light buffer or rows", "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto mesh = scene.addMesh(render::makeCube(), "lmx.test.light.empty.mesh");
    const auto material = scene.addMaterial({});
    scene.addObject({.mesh = mesh, .material = material});
    REQUIRE(scene.finalize(**device).has_value());
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
    const auto tables = scene.tables();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    REQUIRE(tables.lights == nullptr);
    REQUIRE(tables.lightRowCount == 0);
    REQUIRE(tables.lightCapacity == 0);
    REQUIRE(scene.tableStats().lightCount == 0);
    REQUIRE(scene.tableStats().lightCapacity == 0);
}

//======================================================================================================================
TEST_CASE("A removed local light's row zeroes range and reports a live row count and span",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto a = scene.addLight(testLight(1.0f));
    REQUIRE(a.has_value());
    const auto b = scene.addLight(testLight(2.0f));
    REQUIRE(b.has_value());
    REQUIRE(scene.finalize(**device).has_value());
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
    auto tables = scene.tables();
    REQUIRE(tables.lights != nullptr);
    REQUIRE(tables.lightRowCount == 2);
    REQUIRE(tables.lightRows.size() == 2);
    REQUIRE(tables.lightRows[a->slot].range > 0.0f);
    REQUIRE(tables.liveLightCount == 2);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    REQUIRE(scene.removeLight(*a));
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
    tables = scene.tables();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    REQUIRE(tables.lightRows[a->slot].range == 0.0f);
    REQUIRE(tables.lightRows[b->slot].range > 0.0f);
    REQUIRE(tables.liveLightCount == 1);
    render::LightRow row = readLightRow(*tables.lights, a->slot);
    REQUIRE(row.range == 0.0f);
    REQUIRE(scene.removeLight(*b));
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
    tables = scene.tables();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    REQUIRE(tables.lights != nullptr);
    REQUIRE(tables.lightRowCount == 2);
    REQUIRE(tables.liveLightCount == 0);
}

//======================================================================================================================
TEST_CASE("Updating a local light marks its row dirty in every paced frame slot",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto id = scene.addLight(testLight(1.0f));
    REQUIRE(id.has_value());
    REQUIRE(scene.finalize(**device).has_value());
    // The row's creation dirties all three paced slots; four frames fully settle every slot (three
    // to clear each bit, one more to observe the resulting steady state).
    for (uint32_t frame = 0; frame < 4; ++frame) {
        (*device)->beginFrame();
        REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
    }
    REQUIRE(scene.tableStats().rowsWritten == 0);
    REQUIRE(scene.updateLight(*id, testLight(7.0f)).has_value());
    std::array<rojoRHI::Buffer*, 3> buffers{};
    for (uint32_t frame = 0; frame < 3; ++frame) {
        (*device)->beginFrame();
        REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
        const auto tables = scene.tables();
        // One light exists and only its single row changed, so exactly one row is written into
        // this frame's paced slot -- the brief's "one dirty row in three slots".
        REQUIRE(scene.tableStats().rowsWritten == 1);
        buffers[scene.tableStats().slot] = tables.lights;
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
    }
    for (auto* buffer : buffers) {
        REQUIRE(buffer != nullptr);
        render::LightRow row{};
        buffer->readback(&row, sizeof(row));
        REQUIRE(row.position.x == 7.0f);
    }
    (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()).has_value());
    REQUIRE(scene.tableStats().rowsWritten == 0);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("Local light table growth from 4 to 8 keeps stable rows and retires old buffers",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    scene::Scene scene;
    const auto first = scene.addLight(testLight(1.0f));
    REQUIRE(first.has_value());
    REQUIRE(scene.finalize(**device).has_value());
    prepareLights(**device, scene);
    REQUIRE(scene.tableStats().lightCapacity == 4);
    const auto* oldBuffer = scene.tables().lights;
    const uint64_t lastFrame = (*device)->frameNumber();
    while (scene.localLights().size() < 5) {
        const auto id = scene.addLight(testLight(3.0f));
        REQUIRE(id.has_value());
    }
    prepareLights(**device, scene);
    REQUIRE((*device)->frameNumber() == lastFrame + 1);
    REQUIRE(scene.tableStats().lightCapacity == 8);
    REQUIRE(scene.tableStats().growthEvents == 1);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    REQUIRE(scene.tables().lights != oldBuffer);
    REQUIRE(scene.light(*first));
    // The first light's row content, not just its identity, survives the buffer swap.
    const auto grownTables = scene.tables();
    REQUIRE(grownTables.lightRows[first->slot].position.x == 1.0f);
    REQUIRE(grownTables.lightRows[first->slot].range == 5.0f);
    const auto readBack = readLightRow(*grownTables.lights, first->slot);
    REQUIRE(readBack.position.x == 1.0f);
    REQUIRE(readBack.range == 5.0f);
    prepareLights(**device, scene);
    REQUIRE((*device)->frameNumber() == lastFrame + 2);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    prepareLights(**device, scene);
    REQUIRE((*device)->frameNumber() == lastFrame + 3);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 0);
}

//======================================================================================================================
TEST_CASE("Disabling a local light preserves its identity, edits and animation binding",
          "[scene][light]") {
    scene::Scene scene;
    auto authored = testLight(2.0f);
    const auto id = scene.addLight(authored);
    REQUIRE(id);
    REQUIRE(scene.enabledLightCount() == 1);
    authored.enabled = false;
    authored.intensity = 42.0f;
    REQUIRE(scene.updateLight(*id, authored));
    REQUIRE(scene.enabledLightCount() == 0);
    REQUIRE(scene.localLights().size() == 1);
    REQUIRE(scene.localLights().front() == *id);
    REQUIRE(scene.animationLightId(0) == id);
    REQUIRE(scene.light(*id)->intensity == 42.0f);
    REQUIRE_FALSE(scene.light(*id)->enabled);
    authored.enabled = true;
    REQUIRE(scene.updateLight(*id, authored));
    REQUIRE(scene.enabledLightCount() == 1);
    REQUIRE(scene.light(*id)->intensity == 42.0f);
    REQUIRE(scene.removeLight(*id));
    REQUIRE(scene.enabledLightCount() == 0);
    REQUIRE_FALSE(scene.updateLight(*id, authored));
}

//======================================================================================================================
TEST_CASE("Light enablement updates every paced slot without identity or allocation changes",
          "[gpu][scene][light]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scene::Scene scene;
    auto authored = testLight(3.0f);
    const auto id = scene.addLight(authored);
    REQUIRE(id);
    REQUIRE(scene.finalize(**device));
    for (int i = 0; i < 3; ++i)
        prepareLights(**device, scene);
    const auto capacity = scene.tables().lightCapacity;
    for (const bool enabled : {false, true}) {
        authored.enabled = enabled;
        REQUIRE(scene.updateLight(*id, authored));
        for (int i = 0; i < 3; ++i) {
            prepareLights(**device, scene);
            const auto tables = scene.tables();
            REQUIRE(tables.liveLightCount == (enabled ? 1u : 0u));
            REQUIRE(tables.lightCapacity == capacity);
            REQUIRE(scene.tableStats().lightCount == 1);
            REQUIRE(scene.localLights().front() == *id);
            const auto row = readLightRow(*tables.lights, id->slot);
            REQUIRE(row.range == (enabled ? authored.range : 0.0f));
            REQUIRE((row.boundRadius > 0.0f) == enabled);
        }
    }
}
