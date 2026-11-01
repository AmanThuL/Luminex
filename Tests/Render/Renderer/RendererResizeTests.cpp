#include "Render/Renderer/Renderer.h"
#include "Support/TestGpu.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <optional>
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
RendererSnapshot snapshot(render::Renderer& renderer) {
    auto& states = renderer.textureStates();
    RendererSnapshot result{renderer.width(), renderer.height(), {}, states.size()};
    for (const engine::GpuTexture* target :
         {&renderer.hdrColorTexture(), &renderer.colorTarget(), renderer.motionTexture(),
          renderer.reactiveTexture(), renderer.historyTexture(), &renderer.depthTexture()}) {
        REQUIRE(target != nullptr);
        result.targets.push_back({target->rhi(), states.logicalUse(target->rhi())});
    }
    return result;
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
// then the temporal slots -- and every failure must leave the renderer whole at its old extent.
TEST_CASE("a failed resize leaves the renderer at its old extent", "[render][device]") {
    auto gpu = createTestGpu();
    auto created = render::Renderer::create(*gpu, 64, 64);
    INFO(errorOf(created));
    REQUIRE(created.has_value());
    render::Renderer& renderer = **created;
    retireFrames(*gpu);
    const RendererSnapshot before = snapshot(renderer);
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
        REQUIRE(snapshot(renderer) == before);
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
}
