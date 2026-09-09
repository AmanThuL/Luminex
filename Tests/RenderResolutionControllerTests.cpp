#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Render/ResolutionController.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace lmx::render;
using Catch::Approx;

namespace {

//======================================================================================================================
// Declares frame, then observes it at `gpuMilliseconds`; returns observe()'s result.
bool declareAndObserve(ResolutionController& controller, uint64_t frame, double gpuMilliseconds) {
    controller.declared(frame);
    return controller.observe(frame, gpuMilliseconds);
}

// The App's own per-frame order, with the real pipeline's latency: the frame that retired
// kPipelineLatency frames ago is observed first, then this frame is declared.
constexpr uint64_t kPipelineLatency = 4;

//======================================================================================================================
// Runs one such frame; answers whether that observation changed the scale.
bool pumpFrame(ResolutionController& controller, uint64_t frame, double gpuMilliseconds) {
    bool changed = false;
    if (frame >= kPipelineLatency) {
        changed = controller.observe(frame - kPipelineLatency, gpuMilliseconds);
    }
    controller.declared(frame);
    return changed;
}

} // namespace

//======================================================================================================================
TEST_CASE("a constant 20ms series changes nothing on the first judged sample",
          "[render][resolution]") {
    ResolutionController controller;
    CHECK_FALSE(declareAndObserve(controller, 0, 20.0));
    CHECK(controller.scale() == 1.0f);
}

//======================================================================================================================
TEST_CASE("a constant 20ms series steps down on the second judged sample", "[render][resolution]") {
    ResolutionController controller;
    declareAndObserve(controller, 0, 20.0);
    const bool changed = declareAndObserve(controller, 1, 20.0);
    CHECK(changed);
    CHECK(controller.scale() == Approx(0.95f));
}

//======================================================================================================================
TEST_CASE("a step opens a settle window of settleFrames declared frames", "[render][resolution]") {
    ResolutionController controller;
    declareAndObserve(controller, 0, 20.0);
    declareAndObserve(controller, 1, 20.0); // steps to 0.95, changedAtSequence = 2

    // Frames declared while sequence <= changedAtSequence + settleFrames (6) are not judged, even
    // though they keep measuring over budget. Sequences after this step are 3..8 for six more
    // declared frames; none of them may change the scale.
    for (uint64_t frame = 2; frame < 8; ++frame) {
        const bool changed = declareAndObserve(controller, frame, 20.0);
        CHECK_FALSE(changed);
        CHECK(controller.scale() == Approx(0.95f));
    }
}

//======================================================================================================================
TEST_CASE("a constant t = 20 * scale^2 series settles within one maxStep of the analytic scale "
          "and never below minScale",
          "[render][resolution]") {
    ResolutionController controller;
    const float target = 13.6f;
    const float expected = std::sqrt(target / 20.0f);

    uint64_t frame = 0;
    for (int i = 0; i < 500; ++i) {
        const double t = 20.0 * static_cast<double>(controller.scale()) *
                         static_cast<double>(controller.scale());
        declareAndObserve(controller, frame, t);
        ++frame;
        CHECK(controller.scale() >= controller.settings().minScale);
    }
    CHECK(controller.scale() == Approx(expected).margin(0.05f + 1e-4f));
}

//======================================================================================================================
TEST_CASE("a constant 10ms series from 0.7 steps up only on the twelfth judged sample",
          "[render][resolution]") {
    ResolutionController controller;
    controller.reset(0.7f);

    uint64_t frame = 0;
    for (int i = 0; i < 11; ++i) {
        const bool changed = declareAndObserve(controller, frame, 10.0);
        CHECK_FALSE(changed);
        CHECK(controller.scale() == Approx(0.7f));
        ++frame;
    }

    const bool changed = declareAndObserve(controller, frame, 10.0);
    CHECK(changed);
    CHECK(controller.scale() > 0.7f);
    CHECK(controller.scale() <= 0.7f + 0.05f + 1e-4f);
    CHECK(controller.scale() <= 1.0f);
}

//======================================================================================================================
TEST_CASE("a series inside [13.6, 16] never changes", "[render][resolution]") {
    ResolutionController controller;
    for (uint64_t frame = 0; frame < 50; ++frame) {
        const bool changed = declareAndObserve(controller, frame, 14.5);
        CHECK_FALSE(changed);
    }
    CHECK(controller.scale() == 1.0f);
}

//======================================================================================================================
TEST_CASE("a series alternating 15 and 17 ms never changes", "[render][resolution]") {
    ResolutionController controller;
    for (uint64_t frame = 0; frame < 50; ++frame) {
        const double t = (frame % 2 == 0) ? 15.0 : 17.0;
        const bool changed = declareAndObserve(controller, frame, t);
        CHECK_FALSE(changed);
    }
    CHECK(controller.scale() == 1.0f);
}

//======================================================================================================================
TEST_CASE("a sample whose frame was declared at an earlier scale is ignored",
          "[render][resolution]") {
    ResolutionController controller;
    controller.declared(0); // declared at scale 1.0

    // Force the current scale to differ from the entry's recorded scale by stepping down first,
    // then observe the stale frame -- it must be ignored and count for nothing.
    declareAndObserve(controller, 1, 20.0);
    declareAndObserve(controller, 2, 20.0); // steps to 0.95

    const float scaleBefore = controller.scale();
    const bool changed = controller.observe(0, 20.0);
    CHECK_FALSE(changed);
    CHECK(controller.scale() == scaleBefore);
}

//======================================================================================================================
TEST_CASE("reset seeds the scale and clears the counters", "[render][resolution]") {
    ResolutionController controller;
    controller.reset(0.8f);
    CHECK(controller.scale() == 0.8f);

    // A following single over-budget sample must not change it -- reset cleared the counters, and
    // overBudgetSamples (2) requires two consecutive judged samples.
    const bool changed = declareAndObserve(controller, 0, 20.0);
    CHECK_FALSE(changed);
    CHECK(controller.scale() == 0.8f);
}

//======================================================================================================================
TEST_CASE("setSettings clamps the current scale into the new range", "[render][resolution]") {
    ResolutionController controller;
    controller.reset(0.8f);

    ResolutionControllerSettings settings = controller.settings();
    settings.maxScale = 0.6f;
    controller.setSettings(settings);
    CHECK(controller.scale() == 0.6f);
}

//======================================================================================================================
TEST_CASE("a frame never declared is ignored", "[render][resolution]") {
    ResolutionController controller;
    const bool changed = controller.observe(42, 20.0);
    CHECK_FALSE(changed);
    CHECK(controller.scale() == 1.0f);
}

//======================================================================================================================
// A timestamp pair can retire out of order and report a negative duration. The sample is treated as
// zero rather than fed to a square root, which would return NaN and leave the scale NaN forever --
// a single such sample is well under target, so it counts as one under-target sample and moves
// nothing on its own.
TEST_CASE("a negative sample leaves the scale finite and unchanged", "[render][resolution]") {
    ResolutionController controller;
    controller.reset(0.8f);

    const bool changed = declareAndObserve(controller, 0, -5.0);
    CHECK_FALSE(changed);
    CHECK(std::isfinite(controller.scale()));
    CHECK(controller.scale() == 0.8f);
}

//======================================================================================================================
// The settle window counts frames declared after the change, not after the sample that caused it.
// Under the App's real latency the two differ: the frames already in flight when a late-arriving
// sample steps the scale would otherwise eat the window and let the next decision start early.
TEST_CASE("the settle window is anchored to the declaration count at the change",
          "[render][resolution]") {
    ResolutionController controller;
    std::vector<uint64_t> steppedOn;

    // Frames 0..4 are declared at scale 1.0; the observation of frame 1, taken just before frame 5
    // is declared, is the second over-budget sample and steps to 0.95.
    for (uint64_t frame = 0; frame <= 5; ++frame) {
        if (pumpFrame(controller, frame, 20.0)) {
            steppedOn.push_back(frame - kPipelineLatency);
        }
    }
    REQUIRE(steppedOn == std::vector<uint64_t>{1});
    REQUIRE(controller.scale() == Approx(0.95f));

    // Frame 4 was declared at the old scale and is still rejected on its own merits.
    CHECK_FALSE(controller.observe(4, 20.0));
    CHECK(controller.scale() == Approx(0.95f));

    // Frames 5..10 are the six newly declared frames the settle window covers: every one of them
    // measures over budget, and none of them may count towards the next decision. Frame 11 is the
    // first judged sample and frame 12 the second, so the next step lands on frame 12's
    // observation -- the frame after which the declaration count, not frame 1's sequence, put the
    // window's end.
    for (uint64_t frame = 6; frame <= 16; ++frame) {
        const bool changed = pumpFrame(controller, frame, 20.0);
        if (changed) {
            steppedOn.push_back(frame - kPipelineLatency);
        }
        const uint64_t observed = frame - kPipelineLatency;
        if (observed <= 10) {
            INFO("observation of frame " << observed);
            CHECK_FALSE(changed);
            CHECK(controller.scale() == Approx(0.95f));
        }
    }
    CHECK(steppedOn == std::vector<uint64_t>{1, 12});
    CHECK(controller.scale() == Approx(0.90f));
}
