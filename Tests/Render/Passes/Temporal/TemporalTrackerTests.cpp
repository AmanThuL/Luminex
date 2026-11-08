//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalTrackerTests.cpp
/// @brief Runs native and vendor temporal history frames through the renderer under the tracker.
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
#include <vector>

namespace {

namespace render = lmx::render;
using Mode = render::ReconstructionMode;
using S = rojoRHI::Stage;
using A = rojoRHI::Access;
using L = rojoRHI::Layout;
using Extent = std::optional<std::pair<uint32_t, uint32_t>>;

constexpr rojoRHI::SubresourceRange kMip0{
    .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1};

struct HistoryFrame {
    bool temporal;
    Mode mode;
    float scale;
    Extent resize;
};

// Native TAA, Raw and the native upscale at half scale write the slots in turn; a temporal-off
// frame advances no parity; a resize replaces both slot pairs mid-sequence.
constexpr std::array<HistoryFrame, 8> kNativeFrames{
    {{.temporal = true, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::Raw, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 0.5f, .resize = {}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 0.5f, .resize = {}},
     {.temporal = false, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = Extent{{48, 40}}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = {}}}};

// Vendor frames at both scales, interleaved with native, Raw and temporal-off frames that read or
// overwrite what the scaler left.
constexpr std::array<HistoryFrame, 9> kVendorFrames{
    {{.temporal = true, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::VendorTemporal, .scale = 0.5f, .resize = {}},
     {.temporal = true, .mode = Mode::NativeTaa, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}},
     {.temporal = false, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::Raw, .scale = 1.0f, .resize = {}},
     {.temporal = true, .mode = Mode::VendorTemporal, .scale = 1.0f, .resize = {}}}};

//======================================================================================================================
void applyHistoryFrame(render::SceneView& view, const HistoryFrame& frame) {
    view.localLightMode = lmx::engine::LocalLightMode::Off;
    view.classifyMode = render::ClassifyMode::Cpu;
    view.occlusionEnabled = false;
    view.temporal.enabled = frame.temporal;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = frame.mode;
    view.temporal.renderScale = frame.scale;
    view.temporal.debugView = render::TemporalDebugView::Off;
    REQUIRE_FALSE(view.items.empty());
}

//======================================================================================================================
std::optional<uint32_t> resourceIndex(const render::CompiledFrameRecord& record,
                                      std::string_view name) {
    const auto found =
        std::ranges::find(record.debug.resources, name, &render::DebugResource::name);
    if (found == record.debug.resources.end())
        return std::nullopt;
    return static_cast<uint32_t>(found - record.debug.resources.begin());
}

//======================================================================================================================
bool scheduled(const render::CompiledFrameRecord& record, uint32_t pass) {
    return std::ranges::find(record.debug.schedule.passes, pass) !=
           record.debug.schedule.passes.end();
}

//======================================================================================================================
// Labels of the scheduled passes that use `resource` in one of `roles`.
template <size_t N>
std::vector<std::string> scheduledUsers(const render::CompiledFrameRecord& record,
                                        std::optional<uint32_t> resource,
                                        const std::array<render::UseRole, N>& roles) {
    std::vector<std::string> labels;
    if (!resource)
        return labels;
    for (uint32_t pass = 0; pass < record.debug.passes.size(); ++pass) {
        if (!scheduled(record, pass))
            continue;
        const auto& uses = record.debug.passes[pass].uses;
        if (std::ranges::any_of(uses, [&](const render::DebugUse& use) {
                return use.resource == *resource &&
                       std::ranges::find(roles, use.role) != roles.end();
            }))
            labels.push_back(record.debug.passes[pass].label);
    }
    return labels;
}

constexpr std::array kWriteRoles{render::UseRole::Write, render::UseRole::CopyDestination,
                                 render::UseRole::ColorAttachment};
constexpr std::array kReadRoles{render::UseRole::Read, render::UseRole::ShaderRead};
constexpr std::array kDepthRoles{render::UseRole::DepthAttachment};

//======================================================================================================================
std::string historyName(uint32_t slot) {
    return std::format("lmx.render.historyColor{}", slot);
}

//======================================================================================================================
std::string depthName(uint32_t slot) {
    return std::format("lmx.render.sceneDepth{}", slot);
}

//======================================================================================================================
// The slot a temporal frame declares and the reconstruction pass that writes it: the scene pass
// renders depth into the slot, exactly one pass writes its colour and none writes the other slot's.
void requireSlotFrame(const render::CompiledFrameRecord& record, uint32_t slot,
                      std::string_view writer) {
    const auto history = resourceIndex(record, historyName(slot));
    REQUIRE(history.has_value());
    REQUIRE(scheduledUsers(record, history, kWriteRoles) ==
            std::vector<std::string>{std::string(writer)});
    REQUIRE(
        scheduledUsers(record, resourceIndex(record, historyName(1 - slot)), kWriteRoles).empty());
    REQUIRE(scheduledUsers(record, resourceIndex(record, depthName(slot)), kDepthRoles) ==
            std::vector<std::string>{"lmx.pass.scene"});
}

//======================================================================================================================
// The pass that writes frame `index`'s colour slot when its mode is native or Raw. Native TAA takes
// the upscaling kernel below scale 1 and for the one frame after a differently sized one whose
// history it keeps, since the native kernel addresses the previous depth at its own extent.
template <size_t N>
std::string_view nativeWriter(const std::array<HistoryFrame, N>& frames, uint32_t index) {
    const HistoryFrame& frame = frames[index];
    if (frame.mode == Mode::Raw)
        return frame.scale < 1.0f ? "lmx.pass.temporal.commitUpscaled"
                                  : "lmx.pass.temporal.commitHistory";
    const bool followsOtherExtent = index > 0 && frames[index - 1].temporal && !frame.resize &&
                                    frames[index - 1].scale != frame.scale;
    return frame.scale < 1.0f || followsOtherExtent ? "lmx.pass.temporal.upscale"
                                                    : "lmx.pass.temporal.resolve";
}

//======================================================================================================================
void requireState(render::TextureStateTable& states, rojoRHI::Texture texture,
                  render::TextureUse use, render::TextureState physical) {
    REQUIRE(states.logicalUse(texture) == use);
    REQUIRE(states.find(texture, kMip0) == physical);
}

// One slot's colour and depth identities.
struct SlotIdentity {
    rojoRHI::Texture color;
    rojoRHI::Texture depth;

    bool operator==(const SlotIdentity&) const;
};

// The identities the renderer reported for each slot since the last resize.
using SlotIdentities = std::array<std::optional<SlotIdentity>, 2>;

//======================================================================================================================
bool SlotIdentity::operator==(const SlotIdentity&) const = default;

//======================================================================================================================
// The renderer reports this frame's slot; it must be the identity that slot had before and differ
// from the other slot's, so the frames alternate two physical pairs.
void requireSlotIdentity(SlotIdentities& slots, render::Renderer& renderer, uint32_t slot) {
    const SlotIdentity now{.color = renderer.historyTexture()->rhi(),
                           .depth = renderer.depthTexture().rhi()};
    REQUIRE(slots[slot].value_or(now) == now);
    const SlotIdentity other = slots[1 - slot].value_or(SlotIdentity{});
    REQUIRE(other.color != now.color);
    REQUIRE(other.depth != now.depth);
    slots[slot] = now;
}

} // namespace

//======================================================================================================================
TEST_CASE("native temporal history alternates its slots across frames under the tracker",
          "[render][device][tracker]") {
    uint32_t temporalFrames = 0;
    SlotIdentities identities;
    lmx::test::runTrackedFrames(
        "material-lab",
        {.frames = static_cast<uint32_t>(kNativeFrames.size()),
         .perFrame = [](render::SceneView& view,
                        uint32_t frame) { applyHistoryFrame(view, kNativeFrames[frame]); },
         .readDisplay = {},
         .resizeBefore = [](uint32_t frame) { return kNativeFrames[frame].resize; },
         .inspect =
             [&](const render::CompiledFrameRecord& record, uint32_t frame) {
                 const auto& state = kNativeFrames[frame];
                 if (!state.temporal) {
                     REQUIRE_FALSE(resourceIndex(record, historyName(0)).has_value());
                     REQUIRE_FALSE(resourceIndex(record, historyName(1)).has_value());
                     return;
                 }
                 const uint32_t slot = temporalFrames % 2;
                 requireSlotFrame(record, slot, nativeWriter(kNativeFrames, frame));
                 if (state.mode == Mode::NativeTaa) {
                     const std::vector<std::string> reader{
                         std::string(nativeWriter(kNativeFrames, frame))};
                     REQUIRE(scheduledUsers(record, resourceIndex(record, historyName(1 - slot)),
                                            kReadRoles) == reader);
                     REQUIRE(scheduledUsers(record, resourceIndex(record, depthName(1 - slot)),
                                            kReadRoles) == reader);
                 }
             },
         .inspectRenderer =
             [&](render::Renderer& renderer, uint32_t frame) {
                 const auto& state = kNativeFrames[frame];
                 if (state.resize)
                     identities = {};
                 // Every frame leaves its depth as a shader read for callers after the graph.
                 REQUIRE(renderer.textureStates().logicalUse(renderer.depthTexture().rhi()) ==
                         render::TextureUse::ShaderRead);
                 if (!state.temporal)
                     return;
                 requireSlotIdentity(identities, renderer, temporalFrames % 2);
                 ++temporalFrames;
             }});
    REQUIRE(temporalFrames == 7);
}

//======================================================================================================================
TEST_CASE("vendor temporal frames leave their slots in external states under the tracker",
          "[render][device][tracker]") {
    uint32_t temporalFrames = 0;
    SlotIdentities identities;
    lmx::test::runTrackedFrames(
        "material-lab",
        {.frames = static_cast<uint32_t>(kVendorFrames.size()),
         .perFrame = [](render::SceneView& view,
                        uint32_t frame) { applyHistoryFrame(view, kVendorFrames[frame]); },
         .readDisplay = {},
         .resizeBefore = {},
         .inspect =
             [&](const render::CompiledFrameRecord& record, uint32_t frame) {
                 const auto& state = kVendorFrames[frame];
                 if (!state.temporal)
                     return;
                 const uint32_t slot = temporalFrames % 2;
                 if (state.mode != Mode::VendorTemporal) {
                     requireSlotFrame(record, slot, nativeWriter(kVendorFrames, frame));
                     return;
                 }
                 requireSlotFrame(record, slot, "lmx.pass.temporal.vendor");
                 REQUIRE(
                     scheduledUsers(record, resourceIndex(record, depthName(slot)), kReadRoles) ==
                     std::vector<std::string>{"lmx.pass.temporal.vendor"});
                 // The scaler keeps its private history; the engine's other slot goes unread.
                 REQUIRE(scheduledUsers(record, resourceIndex(record, historyName(1 - slot)),
                                        kReadRoles)
                             .empty());
             },
         .inspectRenderer =
             [&](render::Renderer& renderer, uint32_t frame) {
                 const auto& state = kVendorFrames[frame];
                 auto& states = renderer.textureStates();
                 const rojoRHI::Texture depth = renderer.depthTexture().rhi();
                 if (state.temporal && state.mode == Mode::VendorTemporal) {
                     REQUIRE(renderer.temporalStatus().reconstruction == Mode::VendorTemporal);
                     // The colour slot keeps the scaler's write and the depth its opaque read,
                     // each in the compute stage set the external pass declares.
                     requireState(states, renderer.historyTexture()->rhi(),
                                  render::TextureUse::ExternalWrite,
                                  {.stage = S::ComputeShader,
                                   .access = A::ShaderWrite,
                                   .layout = L::ShaderWrite});
                     requireState(states, depth, render::TextureUse::ExternalRead,
                                  {.stage = S::ComputeShader,
                                   .access = A::ShaderRead,
                                   .layout = L::ShaderRead});
                 } else {
                     REQUIRE(states.logicalUse(depth) == render::TextureUse::ShaderRead);
                 }
                 if (!state.temporal)
                     return;
                 requireSlotIdentity(identities, renderer, temporalFrames % 2);
                 ++temporalFrames;
             }});
    REQUIRE(temporalFrames == 8);
}
