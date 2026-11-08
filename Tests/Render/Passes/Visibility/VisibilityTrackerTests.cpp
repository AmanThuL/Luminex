//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityTrackerTests.cpp
/// @brief Runs GPU visibility classification and emission through the renderer under the tracker.
//----------------------------------------------------------------------------------------------------------------------

#include "Support/TrackerFrameSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace {

namespace render = lmx::render;
using Extent = std::optional<std::pair<uint32_t, uint32_t>>;

struct VisibilityFrame {
    render::ClassifyMode classifier;
    render::SubmissionMode submission;
    bool culling;
    bool empty;
    Extent resize;
};

constexpr auto kGpu = render::ClassifyMode::Gpu;
constexpr auto kCpu = render::ClassifyMode::Cpu;
constexpr auto kIndirect = render::SubmissionMode::Indirect;
constexpr auto kBatched = render::SubmissionMode::Batched;

// Both GPU layouts, a CPU frame between GPU frames, culling off, an empty view and a resize: every
// state transition of the paced row, argument, state and counter buffers the renderer declares.
constexpr std::array<VisibilityFrame, 9> kFrames{
    {{.classifier = kGpu, .submission = kIndirect, .culling = true, .empty = false, .resize = {}},
     {.classifier = kGpu, .submission = kBatched, .culling = true, .empty = false, .resize = {}},
     {.classifier = kCpu, .submission = kIndirect, .culling = true, .empty = false, .resize = {}},
     {.classifier = kGpu, .submission = kBatched, .culling = true, .empty = false, .resize = {}},
     {.classifier = kGpu, .submission = kIndirect, .culling = false, .empty = false, .resize = {}},
     {.classifier = kGpu, .submission = kBatched, .culling = true, .empty = true, .resize = {}},
     {.classifier = kGpu,
      .submission = kBatched,
      .culling = true,
      .empty = false,
      .resize = Extent{{48, 40}}},
     {.classifier = kGpu, .submission = kIndirect, .culling = true, .empty = false, .resize = {}},
     {.classifier = kGpu, .submission = kBatched, .culling = true, .empty = false, .resize = {}}}};

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

} // namespace

//======================================================================================================================
TEST_CASE("GPU visibility frames classify and emit across classifier and layout switches under "
          "the tracker",
          "[render][device][tracker][visibility]") {
    uint32_t inspected = 0;
    lmx::test::runTrackedFrames(
        "visibility-lab",
        {.frames = static_cast<uint32_t>(kFrames.size()),
         .perFrame =
             [](render::SceneView& view, uint32_t frame) {
                 const auto& state = kFrames[frame];
                 view.localLightMode = lmx::engine::LocalLightMode::Off;
                 view.temporal.enabled = false;
                 view.bloomEnabled = false;
                 view.classifyMode = state.classifier;
                 view.classifyCheck = state.classifier == kGpu;
                 view.submission = state.submission;
                 view.visibilityEnabled = state.culling;
                 if (state.empty)
                     view.items = {};
                 REQUIRE((view.items.empty() == state.empty));
             },
         .readDisplay = {},
         .resizeBefore = [](uint32_t frame) { return kFrames[frame].resize; },
         .inspect =
             [](const render::CompiledFrameRecord& record, uint32_t frame) {
                 const auto& state = kFrames[frame];
                 const bool gpu = state.classifier == kGpu;
                 // The reset, classify and emit passes run on every GPU frame, empty or not, and
                 // only the dense layout scans.
                 REQUIRE(scheduled(record, "lmx.pass.visibility.reset") == gpu);
                 REQUIRE(scheduled(record, "lmx.pass.visibility.classify") == gpu);
                 REQUIRE(scheduled(record, "lmx.pass.visibility.emit") == gpu);
                 REQUIRE(scheduled(record, "lmx.pass.visibility.scan") ==
                         (gpu && state.submission == kBatched));
                 REQUIRE(scheduled(record, "lmx.pass.scene"));
                 REQUIRE(scheduled(record, "lmx.pass.shadow"));
             },
         .inspectRenderer =
             [&](render::Renderer& renderer, uint32_t frame) {
                 const auto& state = kFrames[frame];
                 const auto& status = renderer.visibilityStatus();
                 REQUIRE(status.frameNumber == uint64_t{frame} + 1);
                 REQUIRE(status.classifyMode == state.classifier);
                 REQUIRE_FALSE(status.isRetired);
                 REQUIRE(status.scene.candidates.empty() == state.empty);
                 // Every GPU frame retires through the renderer's own joined retirement three
                 // frames later; a CPU frame's counters are already final.
                 if (state.classifier == kCpu)
                     REQUIRE(status.sceneCounters.emittedRows == status.scene.visibleItems.size());
                 ++inspected;
             }});
    REQUIRE(inspected == kFrames.size());
}
