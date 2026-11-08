#include "Support/TrackerFrameSupport.h"

#include <catch2/catch_test_macros.hpp>

namespace {
//======================================================================================================================
void activeScene(lmx::render::SceneView& view) {
    view.temporal.enabled = false;
    view.localLightMode = lmx::engine::LocalLightMode::Off;
    view.bloomEnabled = false;
    view.classifyMode = lmx::render::ClassifyMode::Cpu;
    view.occlusionEnabled = false;
    REQUIRE_FALSE(view.items.empty());
    REQUIRE(view.tables.vertices != nullptr);
    REQUIRE(view.tables.instances != nullptr);
    REQUIRE(view.tables.materials != nullptr);
}
} // namespace

//======================================================================================================================
TEST_CASE("scene tracker changes direct indirect and batched submission", "[device][tracker]") {
    lmx::test::runTrackedFrames(
        "material-lab",
        {.frames = 6, .perFrame = [](auto& view, uint32_t frame) {
             activeScene(view);
             // Keep the shared mesh/material depth lane in the draw list so
             // Batched reaches a real multi-instance run, beyond its mode flag.
             view.visibilityEnabled = false;
             bool sharedRun = false;
             for (size_t left = 0; left < view.items.size(); ++left) {
                 const auto& a = view.tables.instanceRows[view.items[left].instanceRow];
                 for (size_t right = left + 1; right < view.items.size(); ++right) {
                     const auto& b = view.tables.instanceRows[view.items[right].instanceRow];
                     sharedRun |= a.meshRow == b.meshRow && a.materialRow == b.materialRow &&
                                  (a.flags & lmx::engine::kInstanceDisabled) == 0 &&
                                  (b.flags & lmx::engine::kInstanceDisabled) == 0;
                 }
             }
             REQUIRE(sharedRun);
             const std::array modes{lmx::render::SubmissionMode::Direct,
                                    lmx::render::SubmissionMode::Indirect,
                                    lmx::render::SubmissionMode::Batched};
             view.submission = modes[frame % modes.size()];
         }});
}

//======================================================================================================================
TEST_CASE("scene tracker toggles wireframe", "[device][tracker]") {
    lmx::test::runTrackedFrames("material-lab", {.perFrame = [](auto& view, uint32_t frame) {
                                    activeScene(view);
                                    view.wireframe = frame % 2 != 0;
                                }});
}

//======================================================================================================================
TEST_CASE("scene tracker toggles CPU culling", "[device][tracker]") {
    lmx::test::runTrackedFrames("material-lab", {.perFrame = [](auto& view, uint32_t frame) {
                                    activeScene(view);
                                    view.visibilityEnabled = frame % 2 == 0;
                                }});
}

//======================================================================================================================
TEST_CASE("scene tracker reads Sponza material and IBL views", "[device][tracker]") {
    lmx::test::runTrackedFrames("sponza", {.perFrame = [](auto& view, uint32_t) {
                                    activeScene(view);
                                    REQUIRE(view.skyCubemap != nullptr);
                                    REQUIRE(view.irradiance != nullptr);
                                    REQUIRE(view.prefilteredEnv != nullptr);
                                    REQUIRE(view.dfgLut != nullptr);
                                    REQUIRE(std::ranges::any_of(view.items, [](const auto& item) {
                                        return item.diffuse != nullptr;
                                    }));
                                }});
}
