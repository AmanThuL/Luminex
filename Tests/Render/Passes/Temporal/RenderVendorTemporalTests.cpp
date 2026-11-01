#include "Support/GoldenFile.h"
#include "Support/TestGpu.h"

#include "Engine/Gpu/GpuContext.h"
#include "Render/Graph/GraphDump.h"
#include "Render/Graph/TransientPool.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Render/Renderer/Renderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

namespace {
using namespace lmx::render;
using lmx::render::temporal_detail::vendorCreationProbe;

//======================================================================================================================
SceneView vendorView(ReconstructionMode mode) {
    SceneView view;
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = mode;
    view.temporal.debugView = TemporalDebugView::MotionVectors;
    return view;
}

//======================================================================================================================
// One declared and executed device frame with its own transient pool, so each upscaler records at
// most one upscale per frame (VU-rojo-upscale-0008). The dump is the executed frame's record. The
// display target is exported rather than presented: the frame leaves it as a shader read, which a
// presentation transition would contradict, and the App presents its drawable instead.
std::string declaredFrame(lmx::engine::GpuContext& gpu, Renderer& renderer, const SceneView& view) {
    auto frame = gpu.beginFrame();
    std::string dump;
    {
        TransientPool pool(gpu);
        pool.beginFrame();
        RenderGraph graph(pool);
        lmx::engine::Camera camera;
        camera.position = {0, 0, 5};
        const auto display = renderer.declarePasses(graph, frame.commands, camera, view);
        if (!display) {
            gpu.submitAndEndFrame(frame);
        }
        INFO(errorOf(display));
        REQUIRE(display.has_value());
        graph.exportTexture(*display);
        dump = dumpCompiledFrame(graph.execute(frame.commands, frame.number));
    }
    gpu.submitAndEndFrame(frame);
    gpu.waitIdle();
    return dump;
}

// Restores the vendor creation probe whatever way a case leaves.
struct ProbeReset {
    ProbeReset();
    ~ProbeReset();
};

//======================================================================================================================
ProbeReset::ProbeReset() {
    vendorCreationProbe() = {};
}

//======================================================================================================================
ProbeReset::~ProbeReset() {
    vendorCreationProbe() = {};
}

//======================================================================================================================
// A device on the configured backend whose capabilities list MetalFX; the Null backend advertises
// it, as Metal 4 does on Apple silicon.
std::unique_ptr<lmx::engine::GpuContext> vendorGpu() {
    auto gpu = createTestGpu();
    REQUIRE(gpu->upscalerSupport().available);
    return gpu;
}
} // namespace

//======================================================================================================================
TEST_CASE("vendor temporal policy preserves native modes and translates units",
          "[render][temporal][vendor]") {
    const std::array<rojoRHI::UpscalerSupport, 1> entries{
        {{.kind = rojoRHI::UpscalerKind::MetalFX, .minInputScale = 0.5f, .maxInputScale = 1.0f}}};
    const auto support = lmx::engine::toUpscalerSupport(entries);
    REQUIRE(support.name == "MetalFX Temporal");
    for (auto mode : {ReconstructionMode::Raw, ReconstructionMode::NativeTaa}) {
        REQUIRE(resolveReconstruction(mode, {}, true).mode == mode);
        REQUIRE(resolveReconstruction(mode, {}, true).fallback == VendorFallback::None);
    }
    REQUIRE(resolveReconstruction(ReconstructionMode::VendorTemporal, {}).fallback ==
            VendorFallback::Unsupported);
    REQUIRE(resolveReconstruction(ReconstructionMode::VendorTemporal, support, true).fallback ==
            VendorFallback::CreationFailed);
    REQUIRE(resolveReconstruction(ReconstructionMode::VendorTemporal, support).mode ==
            ReconstructionMode::VendorTemporal);
    REQUIRE(vendorHistoryReset(HistoryResetReason::None, ReconstructionMode::NativeTaa, false));
    REQUIRE(vendorHistoryReset(HistoryResetReason::CameraCut, ReconstructionMode::VendorTemporal,
                               false));
    REQUIRE(vendorHistoryReset(HistoryResetReason::None, ReconstructionMode::VendorTemporal, true));
    REQUIRE_FALSE(
        vendorHistoryReset(HistoryResetReason::None, ReconstructionMode::VendorTemporal, false));
    auto narrower = support;
    narrower.minInputScale = 0.6f;
    narrower.maxInputScale = 0.9f;
    REQUIRE(vendorRenderScale(0.5f, narrower) == Catch::Approx(0.6f));
    REQUIRE(vendorRenderScale(1.0f, narrower) == Catch::Approx(0.9f));
    const auto params = vendorTemporalParams({160, 90, 320, 180}, {0.25f, -0.125f});
    REQUIRE(params.motionScaleX == -160.0f);
    REQUIRE(params.motionScaleY == -90.0f);
    REQUIRE(params.jitterX == 0.25f);
    REQUIRE(params.jitterY == 0.125f);
    REQUIRE(params.inputContent.width == 160);
    REQUIRE(params.inputContent.height == 90);
    REQUIRE(params.preExposure == 1.0f);
    // Reversed depth is the upscaler's creation flag rather than a frame value.
    const auto desc = vendorUpscalerDesc(320, 180, "lmx.test.vendor");
    REQUIRE(desc.kind == rojoRHI::UpscalerKind::MetalFX);
    REQUIRE(desc.input.width == 320);
    REQUIRE(desc.output.height == 180);
    REQUIRE(desc.flags == rojoRHI::UpscalerFlags::ReversedDepth);
}

//======================================================================================================================
// A device that lists MetalFX cannot be made to report it unavailable, so the fallback here is the
// creation failure the probe forces; both fallbacks select Native TAA through the same policy.
TEST_CASE("vendor fallback declares byte-identical native frames",
          "[render][temporal][vendor][device]") {
    const ProbeReset reset;
    auto nativeGpu = vendorGpu();
    auto fallbackGpu = vendorGpu();
    auto native = Renderer::create(*nativeGpu, 64, 64, false);
    auto fallback = Renderer::create(*fallbackGpu, 64, 64, false);
    REQUIRE(native);
    REQUIRE(fallback);
    vendorCreationProbe().refuse = true;
    for (int i = 0; i < 2; ++i) {
        const auto expected =
            declaredFrame(*nativeGpu, **native, vendorView(ReconstructionMode::NativeTaa));
        const auto actual =
            declaredFrame(*fallbackGpu, **fallback, vendorView(ReconstructionMode::VendorTemporal));
        REQUIRE(actual == expected);
        REQUIRE((*fallback)->temporalStatus().reconstruction == ReconstructionMode::NativeTaa);
        REQUIRE((*fallback)->temporalStatus().vendorFallback == VendorFallback::CreationFailed);
    }
    REQUIRE(vendorCreationProbe().attempts == 1);
    REQUIRE((*fallback)->temporalStatus().vendorScalerGeneration == 0);
}

//======================================================================================================================
TEST_CASE("vendor initialization failure is remembered until output resize",
          "[render][temporal][vendor][device]") {
    const ProbeReset reset;
    auto gpu = vendorGpu();
    auto renderer = Renderer::create(*gpu, 64, 64, false);
    REQUIRE(renderer);
    vendorCreationProbe().refuse = true;
    const auto view = vendorView(ReconstructionMode::VendorTemporal);
    declaredFrame(*gpu, **renderer, view);
    declaredFrame(*gpu, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().vendorFallback == VendorFallback::CreationFailed);
    REQUIRE(vendorCreationProbe().attempts == 1);
    vendorCreationProbe().refuse = false;
    declaredFrame(*gpu, **renderer, view);
    REQUIRE(vendorCreationProbe().attempts == 1);
    REQUIRE((*renderer)->resize(80, 48));
    declaredFrame(*gpu, **renderer, view);
    REQUIRE(vendorCreationProbe().attempts == 2);
    REQUIRE((*renderer)->temporalStatus().vendorFallback == VendorFallback::None);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE((*renderer)->temporalStatus().vendorScalerGeneration == 1);
}

//======================================================================================================================
TEST_CASE("vendor declarations keep separate histories and record their inputs",
          "[render][temporal][vendor][device]") {
    const ProbeReset reset;
    auto gpu = vendorGpu();
    auto renderer = Renderer::create(*gpu, 64, 64, false);
    REQUIRE(renderer);
    auto view = vendorView(ReconstructionMode::NativeTaa);
    declaredFrame(*gpu, **renderer, view);
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    const auto dump = declaredFrame(*gpu, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE(dump.find("external \"lmx.pass.temporal.vendor\"") != std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.resolve") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.upscale") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.commit") == std::string::npos);
    // The upscale's input content is the frame's render extent (vendorTemporalParams).
    REQUIRE(vendorCreationProbe().attempts == 1);
    REQUIRE((*renderer)->temporalStatus().extents.renderWidth == 64);
    // The golden stops before the transient placements, whose sizes and alignments are the
    // device's own and differ between backends.
    lmx::test::requireMatchesGolden(dump.substr(0, dump.find("\ntransients\n") + 1),
                                    "frame-temporal-vendor.txt");
    view.temporal.renderScale = 0.5f;
    declaredFrame(*gpu, **renderer, view);
    REQUIRE_FALSE((*renderer)->temporalStatus().vendorReset);
    REQUIRE(vendorCreationProbe().attempts == 1);
    REQUIRE((*renderer)->temporalStatus().extents.renderWidth == 32);
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    declaredFrame(*gpu, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyAge == 4);
    view.temporal.enabled = false;
    declaredFrame(*gpu, **renderer, view);
    view.temporal.enabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    declaredFrame(*gpu, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);
}

//======================================================================================================================
TEST_CASE("unread vendor depth keeps its opaque access until overwritten",
          "[render][temporal][vendor][device]") {
    const ProbeReset reset;
    auto gpu = vendorGpu();
    auto renderer = Renderer::create(*gpu, 64, 64, false);
    REQUIRE(renderer);
    auto view = vendorView(ReconstructionMode::VendorTemporal);
    view.temporal.debugView = TemporalDebugView::Off;
    declaredFrame(*gpu, **renderer, view);
    declaredFrame(*gpu, **renderer, view);
    const auto reuse = declaredFrame(*gpu, **renderer, view);
    REQUIRE(reuse.find("texture r3 mips[0..] layers[0..] ExternalRead -> RenderTarget") !=
            std::string::npos);
    view.temporal.enabled = false;
    const auto disabled = declaredFrame(*gpu, **renderer, view);
    REQUIRE(disabled.find("texture r3 mips[0..] layers[0..] ExternalRead -> RenderTarget") !=
            std::string::npos);
}
