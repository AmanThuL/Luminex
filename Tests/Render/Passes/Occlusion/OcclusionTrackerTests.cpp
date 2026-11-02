//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionTrackerTests.cpp
/// @brief Runs depth-pyramid builds, history reads and the independent reference under the tracker.
//----------------------------------------------------------------------------------------------------------------------

#include "Support/TrackerFrameSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace render = lmx::render;
using Reason = render::OcclusionInvalidReason;
using Extent = std::optional<std::pair<uint32_t, uint32_t>>;

struct OcclusionFrame {
    bool occlusion;
    bool check;
    int32_t debugLevel;
    Extent resize;
    Reason reason;
};

// First build, valid history with the debug view, an unchecked frame, a disabled frame and its
// return, a resize of the renderer and both pyramids, then valid history again.
constexpr std::array<OcclusionFrame, 8> kFrames{{
    {.occlusion = true, .check = true, .debugLevel = -1, .resize = {}, .reason = Reason::NoSource},
    {.occlusion = true, .check = true, .debugLevel = 0, .resize = {}, .reason = Reason::None},
    {.occlusion = true, .check = false, .debugLevel = -1, .resize = {}, .reason = Reason::None},
    {.occlusion = false,
     .check = false,
     .debugLevel = -1,
     .resize = {},
     .reason = Reason::Disabled},
    {.occlusion = true,
     .check = true,
     .debugLevel = -1,
     .resize = {},
     .reason = Reason::PreviouslyDisabled},
    {.occlusion = true,
     .check = true,
     .debugLevel = -1,
     .resize = Extent{{48, 40}},
     .reason = Reason::OutputExtentChanged},
    {.occlusion = true, .check = true, .debugLevel = 1, .resize = {}, .reason = Reason::None},
    {.occlusion = true, .check = true, .debugLevel = -1, .resize = {}, .reason = Reason::None},
}};

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
// Whether the classify pass declares a shader read of the named pyramid.
bool classifyReads(const render::CompiledFrameRecord& record, std::string_view pyramid) {
    const auto pass = passIndex(record, "lmx.pass.visibility.classify");
    REQUIRE(pass.has_value());
    return std::ranges::any_of(record.debug.passes[pass.value()].uses,
                               [&](const render::DebugUse& use) {
                                   return record.debug.resources[use.resource].name == pyramid &&
                                          use.role == render::UseRole::ShaderRead;
                               });
}

} // namespace

//======================================================================================================================
TEST_CASE("occlusion pyramids alternate and the reference checks every frame under the tracker",
          "[render][device][tracker][occlusion]") {
    uint32_t builds = 0;
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
                 view.classifyMode = render::ClassifyMode::Gpu;
                 view.classifyCheck = true;
                 view.submission = render::SubmissionMode::Batched;
                 view.occlusionEnabled = state.occlusion;
                 view.occlusionCheck = state.check;
                 view.hzbDebugLevel = state.debugLevel;
             },
         .readDisplay = {},
         .resizeBefore = [](uint32_t frame) { return kFrames[frame].resize; },
         .inspect =
             [&](const render::CompiledFrameRecord& record, uint32_t frame) {
                 const auto& state = kFrames[frame];
                 REQUIRE(scheduled(record, "lmx.pass.hzb.level0") == state.occlusion);
                 REQUIRE(scheduled(record, "lmx.pass.hzb.publish") == state.occlusion);
                 REQUIRE(scheduled(record, "lmx.pass.hzb.debug") ==
                         (state.occlusion && state.debugLevel >= 0));
                 REQUIRE(scheduled(record, "lmx.pass.occlusion.reference.readback") == state.check);
                 if (!state.occlusion)
                     return;
                 // Builds alternate slots; the classifier observes the other one, the pyramid the
                 // previous build wrote.
                 const uint32_t slot = builds % 2;
                 const auto written = std::format("lmx.render.hzb{}", slot);
                 const auto previous = std::format("lmx.render.hzb{}", 1 - slot);
                 REQUIRE(std::ranges::contains(record.debug.resources, written,
                                               &render::DebugResource::name));
                 REQUIRE_FALSE(classifyReads(record, written));
                 if (state.reason == Reason::None)
                     REQUIRE(classifyReads(record, previous));
                 ++builds;
             },
         .inspectRenderer =
             [&](render::Renderer& renderer, uint32_t frame) {
                 const auto& state = kFrames[frame];
                 const auto& status = renderer.visibilityStatus();
                 CAPTURE(render::occlusionInvalidReasonName(status.occlusionInvalidReason));
                 REQUIRE(status.occlusionInvalidReason == state.reason);
                 REQUIRE(status.occlusionEnabled == state.occlusion);
                 REQUIRE(status.occlusionCheckEnabled == state.check);
                 REQUIRE((status.occlusionAssociation != nullptr) == state.occlusion);
                 REQUIRE((status.pyramidBytes != 0) == state.occlusion);
                 if (state.occlusion)
                     REQUIRE(status.pyramidBytes ==
                             render::hzbLayout(renderer.width(), renderer.height()).bytes);
                 ++inspected;
             }});
    REQUIRE(builds == 7);
    REQUIRE(inspected == kFrames.size());
}
