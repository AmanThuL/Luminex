//----------------------------------------------------------------------------------------------------------------------
/// @file LightTrackerTests.cpp
/// @brief Runs local-light modes, light lists and diagnostics through the tracked renderer.
//----------------------------------------------------------------------------------------------------------------------

#include "Support/SceneTableTestSupport.h"
#include "Support/TrackerFrameSupport.h"

#include "Render/Graph/GraphDump.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace render = lmx::render;
namespace engine = lmx::engine;

constexpr auto kOff = engine::LocalLightMode::Off;
constexpr auto kDirect = engine::LocalLightMode::Direct;
constexpr auto kClustered = engine::LocalLightMode::Clustered;
constexpr std::array kLightPasses{"lmx.pass.light.reset", "lmx.pass.light.count",
                                  "lmx.pass.light.scan", "lmx.pass.light.fill"};

//======================================================================================================================
std::optional<uint32_t> passIndex(const render::CompiledFrameRecord& record,
                                  std::string_view label) {
    const auto found = std::ranges::find(record.debug.passes, label, &render::DebugPass::label);
    if (found == record.debug.passes.end())
        return std::nullopt;
    return static_cast<uint32_t>(found - record.debug.passes.begin());
}

//======================================================================================================================
bool scheduled(const render::CompiledFrameRecord& record, std::string_view label) {
    const auto pass = passIndex(record, label);
    return pass && std::ranges::find(record.debug.schedule.passes, *pass) !=
                       record.debug.schedule.passes.end();
}

//======================================================================================================================
// The light-list passes run exactly on a clustered frame with a live light, and never otherwise.
void requireLightPasses(const render::CompiledFrameRecord& record, bool clustered) {
    for (const auto* label : kLightPasses) {
        INFO(label);
        REQUIRE(scheduled(record, label) == clustered);
    }
    REQUIRE(scheduled(record, "lmx.pass.scene"));
}

//======================================================================================================================
void lightLabFrame(render::SceneView& view) {
    view.temporal.enabled = false;
    view.bloomEnabled = false;
    view.classifyMode = render::ClassifyMode::Cpu;
    view.occlusionEnabled = false;
    REQUIRE(view.tables.liveLightCount > 0);
    REQUIRE(view.tables.lights != nullptr);
}

} // namespace

//======================================================================================================================
// Every switch reuses a paced light slot or scene table while earlier frames are still in flight:
// no frame waits for another, so a stale declaration of the rows, the grid or the lists would
// reach the tracker as an unordered access.
TEST_CASE("local-light modes reach the scene across paced frames under the tracker",
          "[render][device][tracker][light]") {
    constexpr std::array kModes{kDirect, kClustered, kOff, kClustered, kDirect, kOff};
    uint32_t inspected = 0;
    lmx::test::runTrackedFrames(
        "light-lab", {.frames = static_cast<uint32_t>(kModes.size()),
                      .perFrame =
                          [&](render::SceneView& view, uint32_t frame) {
                              lightLabFrame(view);
                              view.localLightMode = kModes[frame];
                          },
                      .inspect =
                          [&](const render::CompiledFrameRecord& record, uint32_t frame) {
                              requireLightPasses(record, kModes[frame] == kClustered);
                          },
                      .inspectRenderer =
                          [&](render::Renderer& renderer, uint32_t frame) {
                              const auto& status = renderer.lightingStatus();
                              REQUIRE(status.frameNumber == uint64_t{frame} + 1);
                              REQUIRE(status.requested == kModes[frame]);
                              REQUIRE(status.effective == kModes[frame]);
                              REQUIRE_FALSE(status.isRetired);
                              ++inspected;
                          }});
#ifndef NDEBUG
    REQUIRE(inspected == kModes.size());
#endif
}

//======================================================================================================================
// A TU-local scene, since disabling every light needs the scene itself: two clustered frames fill
// light slots, then every light is disabled while the requested mode keeps changing. The rows stay
// allocated at their high-water count, but no frame imports them, declares a light pass or reads a
// light buffer from the scene pass.
TEST_CASE("zero-live high-water light rows declare no consumers under the tracker",
          "[render][device][tracker][light]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    constexpr std::array kModes{kClustered, kClustered, kDirect, kClustered,
                                kOff,       kClustered, kDirect};
    constexpr uint32_t kLiveFrames = 2;
    auto gpu = createTestGpu({.validation = true});
    auto renderer = render::Renderer::create(*gpu, 64, 64);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto cube = lmx::test::fixtureMesh(gpu->device(), engine::makeCube(), "lmx.test.light.cube");
    REQUIRE(cube);
    const std::array<lmx::test::FixtureDrawItem, 1> items{{{.mesh = &*cube}}};
    std::array<engine::LocalLight, 4> lights{};
    for (uint32_t index = 0; index < lights.size(); ++index) {
        lights[index].position = {static_cast<float>(index) - 1.5f, 0.0f, 1.5f};
        lights[index].range = 4.0f;
    }
    lmx::test::FixtureSceneView view;
    view.items = items;
    view.localLights = lights;
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    view.temporal.enabled = false;
    view.bloomEnabled = false;
    engine::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    render::TransientPool pool(*gpu);
    for (uint32_t ordinal = 0; ordinal < kModes.size(); ++ordinal) {
        CAPTURE(ordinal);
        const bool live = ordinal < kLiveFrames;
        for (auto& light : lights)
            light.enabled = live;
        view.localLightMode = kModes[ordinal];
        auto frame = gpu->beginFrame();
        const render::SceneView prepared = lmx::test::prepareSceneView(view, *gpu);
        REQUIRE(prepared.tables.lightRowCount == lights.size());
        REQUIRE(prepared.tables.liveLightCount == (live ? lights.size() : 0));
        pool.beginFrame();
        render::RenderGraph graph(pool);
        const auto display = (*renderer)->declarePasses(graph, frame.commands, camera, prepared);
        if (!display)
            gpu->submitAndEndFrame(frame);
        INFO(errorOf(display));
        REQUIRE(display);
        graph.exportTexture(*display);
        const auto record = graph.compileFrame(frame.number);
        if (!record)
            gpu->submitAndEndFrame(frame);
        REQUIRE(record);
        graph.execute(frame.commands, frame.number);
        gpu->submitAndEndFrame(frame);
        requireLightPasses(*record, live && kModes[ordinal] == kClustered);
        const auto dump = render::dumpCompiledFrame(*record);
        REQUIRE((dump.find("lmx.scene.lights") != std::string::npos) == live);
        const auto& status = (*renderer)->lightingStatus();
        REQUIRE(status.requested == kModes[ordinal]);
        REQUIRE(status.effective == (live ? kModes[ordinal] : kOff));
        REQUIRE(status.liveLightCount == (live ? lights.size() : 0));
    }
    gpu->waitIdle();
    const auto drained = (*renderer)->drainLightingAfterIdle();
    INFO(errorOf(drained));
    REQUIRE(drained);
    const auto retired = (*renderer)->takeRetiredLighting();
    REQUIRE(retired.size() == kModes.size());
    for (uint32_t ordinal = 0; ordinal < retired.size(); ++ordinal) {
        CAPTURE(ordinal);
        REQUIRE(retired[ordinal].frameNumber == uint64_t{ordinal} + 1);
        REQUIRE(retired[ordinal].isRetired);
        // Only a live clustered frame builds lists; the Null backend runs no kernel, so the totals
        // of those frames are not asserted here.
        const bool clustered = ordinal < kLiveFrames && kModes[ordinal] == kClustered;
        REQUIRE(retired[ordinal].effective == (ordinal < kLiveFrames ? kModes[ordinal] : kOff));
        if (!clustered)
            REQUIRE(retired[ordinal].counters.assigned == 0);
    }
#endif
}

//======================================================================================================================
// Checked frames capture the grid and the lists on every frame, and the renderer joins each
// retired readback with the CPU mirror of the declaration it belongs to.
TEST_CASE("checked light lists retain declaration identities under the tracker",
          "[render][device][tracker][light]") {
    constexpr uint32_t kFrames = 6;
    std::vector<render::LightingStatus> retired;
    lmx::test::runTrackedFrames(
        "light-lab", {.frames = kFrames,
                      .perFrame =
                          [](render::SceneView& view, uint32_t frame) {
                              lightLabFrame(view);
                              view.localLightMode = kClustered;
                              view.lightCheck = true;
                              view.temporal.sceneGeneration = 100 + frame;
                          },
                      .inspect = [](const render::CompiledFrameRecord& record,
                                    uint32_t) { requireLightPasses(record, true); },
                      .inspectRenderer =
                          [&](render::Renderer& renderer, uint32_t frame) {
                              REQUIRE(renderer.lightingStatus().frameNumber == uint64_t{frame} + 1);
                              REQUIRE(renderer.lightingStatus().checkEnabled);
                              for (auto& status : renderer.takeRetiredLighting())
                                  retired.push_back(std::move(status));
                          }});
#ifndef NDEBUG
    // Each declaration retires when the frame three later declares, with its own identities. The
    // Null backend runs no kernel, so the list contents and the check verdict are not asserted;
    // the GPU cases in GpuRendererLightingTests.cpp hold them to the CPU mirror.
    REQUIRE(retired.size() == kFrames - 3);
    for (uint32_t index = 0; index < retired.size(); ++index) {
        CAPTURE(index);
        const auto& status = retired[index];
        REQUIRE(status.frameNumber == uint64_t{index} + 1);
        REQUIRE(status.sceneGeneration == 100 + index);
        REQUIRE(status.isRetired);
        REQUIRE(status.effective == kClustered);
        REQUIRE(status.checkFrame);
        REQUIRE(status.checkFrame->frameNumber == status.frameNumber);
        REQUIRE(status.checkFrame->params.rowCount > 0);
        REQUIRE(status.listBytes == status.counters.assigned * sizeof(uint32_t));
    }
#endif
}

//======================================================================================================================
// Each view draws over a separate display source and reads only what that view's shader loads;
// switching views, and back to the plain scene, leaves no consumer of the previous view behind.
TEST_CASE("light diagnostics switch without stale consumers under the tracker",
          "[render][device][tracker][light]") {
    constexpr std::array kViews{engine::LightDebugView::Count,  engine::LightDebugView::Overflow,
                                engine::LightDebugView::Missed, engine::LightDebugView::Off,
                                engine::LightDebugView::Missed, engine::LightDebugView::Count};
    lmx::test::runTrackedFrames(
        "light-lab",
        {.frames = static_cast<uint32_t>(kViews.size()),
         .perFrame =
             [&](render::SceneView& view, uint32_t frame) {
                 lightLabFrame(view);
                 view.localLightMode = kClustered;
                 view.lightDebugView = kViews[frame];
             },
         .inspect =
             [&](const render::CompiledFrameRecord& record, uint32_t frame) {
                 const bool debug = kViews[frame] != engine::LightDebugView::Off;
                 requireLightPasses(record, true);
                 REQUIRE(scheduled(record, "lmx.pass.light.debug") == debug);
                 REQUIRE(passIndex(record, "lmx.pass.light.debug").has_value() == debug);
             }});
}
