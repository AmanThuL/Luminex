#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Render/Renderer/Renderer.h"
#include "Support/TestGpu.h"

#include "Engine/View/Camera.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace lmx;

// One target the resize may replace: its identity and the use the state table records for it.
struct TargetSnapshot {
    rojoRHI::Texture texture;
    std::optional<render::TextureUse> use;

    bool operator==(const TargetSnapshot&) const;
};

// Everything a failed resize must leave as it was.
struct RendererSnapshot {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<TargetSnapshot> targets;
    size_t tableSize = 0;

    bool operator==(const RendererSnapshot&) const;
};

//======================================================================================================================
bool TargetSnapshot::operator==(const TargetSnapshot&) const = default;

//======================================================================================================================
bool RendererSnapshot::operator==(const RendererSnapshot&) const = default;

//======================================================================================================================
// `otherSlot` is the colour and depth pair of the temporal slot the last frame did not write, which
// the renderer reports only while a frame writes it.
RendererSnapshot snapshot(render::Renderer& renderer, std::span<const rojoRHI::Texture> otherSlot) {
    auto& states = renderer.textureStates();
    RendererSnapshot result{.width = renderer.width(),
                            .height = renderer.height(),
                            .targets = {},
                            .tableSize = states.size()};
    for (const engine::GpuTexture* target :
         {&renderer.hdrColorTexture(), &renderer.colorTarget(), renderer.motionTexture(),
          renderer.reactiveTexture(), renderer.historyTexture(), &renderer.depthTexture()}) {
        REQUIRE(target != nullptr);
        result.targets.push_back(
            {.texture = target->rhi(), .use = states.logicalUse(target->rhi())});
    }
    for (const rojoRHI::Texture texture : otherSlot) {
        result.targets.push_back({.texture = texture, .use = states.logicalUse(texture)});
    }
    return result;
}

//======================================================================================================================
// Renders one temporal frame of `mode` with no scene items and waits for it.
void renderTemporalFrame(engine::GpuContext& gpu, render::Renderer& renderer,
                         render::ReconstructionMode mode) {
    render::SceneView view;
    view.temporal.enabled = true;
    view.temporal.reconstruction = mode;
    engine::Camera camera;
    camera.position = {0, 0, 5};
    auto frame = gpu.beginFrame();
    const auto rendered = renderer.render(frame.commands, camera, view);
    gpu.submitAndEndFrame(frame);
    INFO(errorOf(rendered));
    REQUIRE(rendered.has_value());
    gpu.waitIdle();
}

//======================================================================================================================
uint32_t vendorAttempts() {
    return render::temporal_detail::vendorCreationProbe().attempts.load();
}

//======================================================================================================================
// Retires every frame slot. Released ranges and destroyed views return to the heap only when the
// frame slot open at the release retires.
void retireFrames(engine::GpuContext& gpu) {
    for (uint32_t frame = 0; frame < 4; ++frame) {
        auto slot = gpu.beginFrame();
        gpu.submitAndEndFrame(slot);
    }
    gpu.waitIdle();
}

// Holds up to `count` view slots, reserved greedily by descending powers of two wherever they are
// free: the renderer's own views fragment the heap, so no single run need hold them all. Releases
// them on destruction, deferred until retireFrames(). A hold owns its ranges, so it is never
// copied.
struct ViewHold {
    rojoRHI::Device device;
    std::vector<rojoRHI::IndexRange> ranges;
    uint32_t held = 0;

    //==================================================================================================================
    ViewHold(rojoRHI::Device source, uint32_t count);
    ~ViewHold();
};

//======================================================================================================================
ViewHold::ViewHold(rojoRHI::Device source, uint32_t count) : device(source) {
    for (uint32_t size = std::bit_floor(device.capabilities().maxViews); size != 0; size /= 2) {
        while (size <= count - held) {
            auto range = device.reserveViews(size);
            if (!range) {
                break;
            }
            ranges.push_back(*range);
            held += size;
        }
    }
}

//======================================================================================================================
ViewHold::~ViewHold() {
    for (const rojoRHI::IndexRange range : ranges) {
        device.release(range);
    }
}

} // namespace

//======================================================================================================================
// A resize allocates every replacement before it adopts any. Leaving 0, 1, 2, ... view slots free
// fails it at each allocation in turn -- the scene targets, then the motion and reactive targets,
// then the temporal slots -- and every failure must leave the renderer whole at its old extent:
// both temporal slot pairs, and the vendor scaler created at that extent.
TEST_CASE("a failed resize leaves the renderer at its old extent", "[render][device]") {
    auto gpu = createTestGpu();
    REQUIRE(gpu->upscalerSupport().available);
    auto created = render::Renderer::create(*gpu, 64, 64);
    INFO(errorOf(created));
    REQUIRE(created.has_value());
    render::Renderer& renderer = **created;
    const uint32_t attempts = vendorAttempts();

    // Two temporal frames write both slots; the vendor one creates the scaler at 64x64.
    renderTemporalFrame(*gpu, renderer, render::ReconstructionMode::NativeTaa);
    const engine::GpuTexture* firstHistory = renderer.historyTexture();
    REQUIRE(firstHistory != nullptr);
    const std::array<rojoRHI::Texture, 2> firstSlot{firstHistory->rhi(),
                                                    renderer.depthTexture().rhi()};
    renderTemporalFrame(*gpu, renderer, render::ReconstructionMode::VendorTemporal);
    REQUIRE(renderer.historyTexture()->rhi() != firstSlot[0]);
    const std::array<rojoRHI::Texture, 2> secondSlot{renderer.historyTexture()->rhi(),
                                                     renderer.depthTexture().rhi()};
    REQUIRE(renderer.temporalStatus().vendorScalerGeneration == 1);
    REQUIRE(vendorAttempts() == attempts + 1);
    retireFrames(*gpu);

    // A resize refused at its first allocation keeps the scaler: the next vendor frame at the old
    // extent creates none and resets no history.
    {
        const uint32_t free = ViewHold(gpu->device(), UINT32_MAX).held;
        retireFrames(*gpu);
        const auto resized = [&] {
            const ViewHold hold(gpu->device(), free);
            return renderer.resize(80, 48);
        }();
        REQUIRE_FALSE(resized.has_value());
        retireFrames(*gpu);
    }
    renderTemporalFrame(*gpu, renderer, render::ReconstructionMode::VendorTemporal);
    REQUIRE(renderer.historyTexture()->rhi() == firstSlot[0]);
    REQUIRE(renderer.depthTexture().rhi() == firstSlot[1]);
    REQUIRE(vendorAttempts() == attempts + 1);
    REQUIRE(renderer.temporalStatus().vendorScalerGeneration == 1);
    REQUIRE_FALSE(renderer.temporalStatus().vendorReset);
    retireFrames(*gpu);

    const RendererSnapshot before = snapshot(renderer, secondSlot);
    const uint32_t free = ViewHold(gpu->device(), UINT32_MAX).held;
    retireFrames(*gpu);

    uint32_t failures = 0;
    for (uint32_t spare = 0;; ++spare) {
        CAPTURE(spare);
        REQUIRE(spare < 64);
        const auto resized = [&] {
            const ViewHold hold(gpu->device(), free - spare);
            REQUIRE(hold.held == free - spare);
            return renderer.resize(80, 48);
        }();
        if (resized) {
            break;
        }
        REQUIRE(resized.error().code == ErrorCode::Gpu);
        REQUIRE(snapshot(renderer, secondSlot) == before);
        ++failures;
        retireFrames(*gpu);
    }
    // The two scene targets hold one sampled view each, so the failures past the second reached the
    // temporal allocations after both scene targets had been allocated.
    REQUIRE(failures > 2);
    REQUIRE(renderer.width() == 80);
    REQUIRE(renderer.height() == 48);
    REQUIRE(renderer.historyTexture()->desc().extent.width == 80);
    REQUIRE(renderer.motionTexture()->desc().extent.height == 48);
    // The committed resize retired the scaler with the old extent.
    retireFrames(*gpu);
    renderTemporalFrame(*gpu, renderer, render::ReconstructionMode::VendorTemporal);
    REQUIRE(vendorAttempts() == attempts + 2);
    REQUIRE(renderer.temporalStatus().vendorScalerGeneration == 2);
    REQUIRE(renderer.temporalStatus().vendorReset);
    // Destroying a texture advances its handle generation, so these stale handles cannot name the
    // new slot textures; the state table must simply have forgotten them.
    for (const rojoRHI::Texture texture : secondSlot) {
        REQUIRE_FALSE(renderer.textureStates().logicalUse(texture).has_value());
    }
}
