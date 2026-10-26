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
TEST_CASE("shadow tracker toggles PCF and PCSS", "[device][tracker]") {
    lmx::test::runTrackedFrames(
        "material-lab", {.perFrame = [](auto& view, uint32_t frame) {
            activeScene(view);
            view.lights[0] = {.strength = glm::vec3(0.8f),
                              .direction = glm::normalize(glm::vec3{1, -1, 1})};
            view.shadowCaster = 0;
            view.shadowFilter =
                frame % 2 == 0 ? lmx::render::ShadowFilter::PCF : lmx::render::ShadowFilter::PCSS;
        }});
}

//======================================================================================================================
TEST_CASE("shadow tracker toggles the directional caster", "[device][tracker]") {
    lmx::test::runTrackedFrames("material-lab", {.perFrame = [](auto& view, uint32_t frame) {
                                    activeScene(view);
                                    for (auto& light : view.lights)
                                        light = {.strength = glm::vec3(0.8f),
                                                 .direction = glm::normalize(glm::vec3{1, -1, 1})};
                                    constexpr std::array<int32_t, 4> casters{0, 1, -1, 2};
                                    view.shadowCaster = casters[frame];
                                    if (frame == 3)
                                        view.lights[2].strength = glm::vec3(0);
                                }});
}
