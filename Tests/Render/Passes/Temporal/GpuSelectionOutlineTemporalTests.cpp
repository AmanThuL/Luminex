#include "Support/SelectionOutlineTestSupport.h"

//======================================================================================================================
TEST_CASE(
    "selection presentation leaves ordinary display scene color and temporal history untouched",
    "[gpu][selection-outline][temporal]") {
    auto gpu = createTestGpu();
    TransientPool referencePool(*gpu);
    TransientPool outlinedPool(*gpu);
    auto reference = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(reference);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.historyQuad");
    REQUIRE(mesh);
    auto item = outlineItem(*mesh);
    auto view = outlineView(std::span{&item, 1});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    for (uint32_t index = 0; index < 4; ++index) {
        INFO("declared frame " << index);
        const auto plain = outlineFrame(*gpu, referencePool, **reference, nullptr, view);
        const auto selected = outlineFrame(*gpu, outlinedPool, **renderer, outline->get(), view);
        REQUIRE(selectionPasses(plain.record) == 0);
        REQUIRE(selectionPasses(selected.record) == 3);
        REQUIRE(selected.display == plain.display);
        REQUIRE(selected.scene == plain.scene);
        REQUIRE(selected.history == plain.history);
        REQUIRE(changedPixels(selected) > 0);
        REQUIRE((*renderer)->temporalStatus().historyAge ==
                (*reference)->temporalStatus().historyAge);
    }
}
