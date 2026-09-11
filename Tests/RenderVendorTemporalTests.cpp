#include "GraphTestSupport.h"

#include "Render/GraphDump.h"
#include "Render/Renderer.h"
#include "Render/VendorTemporalScaler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

namespace {
using namespace lmx::render;

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
std::string declaredFrame(FakeDevice& device, Renderer& renderer, const SceneView& view) {
    if (device.frame == 0) {
        device.frame = 1;
    }
    auto& commands = device.beginFrame();
    TransientPool pool(device);
    pool.beginFrame();
    RenderGraph graph(pool);
    Camera camera;
    camera.position = {0, 0, 5};
    graph.presentTexture(renderer.declarePasses(graph, commands, camera, view));
    auto record = graph.compileFrame(device.frameNumber());
    INFO((record ? "" : record.error().message));
    REQUIRE(record);
    graph.execute(commands, device.frameNumber());
    device.endFrame(nullptr);
    return dumpCompiledFrame(*record);
}

//======================================================================================================================
lmx::rhi::TemporalScalerSupport supportedVendor() {
    return {
        .available = true, .minInputScale = 0.5f, .maxInputScale = 1.0f, .name = "Fake Temporal"};
}
} // namespace

//======================================================================================================================
TEST_CASE("vendor temporal policy preserves native modes and translates units",
          "[render][temporal][vendor]") {
    const auto support = supportedVendor();
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
    REQUIRE(params.jitterOffsetX == 0.25f);
    REQUIRE(params.jitterOffsetY == 0.125f);
    REQUIRE(params.inputContentWidth == 160);
    REQUIRE(params.inputContentHeight == 90);
    REQUIRE(params.preExposure == 1.0f);
    REQUIRE(params.reversedDepth);
}

//======================================================================================================================
TEST_CASE("vendor fallback declares byte-identical native frames", "[render][temporal][vendor]") {
    FakeDevice nativeDevice;
    FakeDevice unavailableDevice;
    auto native = Renderer::create(nativeDevice, 64, 64, false);
    auto fallback = Renderer::create(unavailableDevice, 64, 64, false);
    REQUIRE(native);
    REQUIRE(fallback);
    for (int i = 0; i < 2; ++i) {
        const auto expected =
            declaredFrame(nativeDevice, **native, vendorView(ReconstructionMode::NativeTaa));
        const auto actual = declaredFrame(unavailableDevice, **fallback,
                                          vendorView(ReconstructionMode::VendorTemporal));
        REQUIRE(actual == expected);
        REQUIRE((*fallback)->temporalStatus().reconstruction == ReconstructionMode::NativeTaa);
        REQUIRE((*fallback)->temporalStatus().vendorFallback == VendorFallback::Unsupported);
    }
    REQUIRE(unavailableDevice.temporalScalerCreations.empty());
}

//======================================================================================================================
TEST_CASE("vendor initialization failure is remembered until output resize",
          "[render][temporal][vendor]") {
    FakeDevice device;
    device.deviceCaps.temporalScaler = supportedVendor();
    device.failTemporalScalerCreation = true;
    auto renderer = Renderer::create(device, 64, 64, false);
    REQUIRE(renderer);
    const auto view = vendorView(ReconstructionMode::VendorTemporal);
    declaredFrame(device, **renderer, view);
    declaredFrame(device, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().vendorFallback == VendorFallback::CreationFailed);
    REQUIRE(device.temporalScalerCreations.size() == 1);
    device.failTemporalScalerCreation = false;
    declaredFrame(device, **renderer, view);
    REQUIRE(device.temporalScalerCreations.size() == 1);
    REQUIRE((*renderer)->resize(80, 48));
    declaredFrame(device, **renderer, view);
    REQUIRE(device.temporalScalerCreations.size() == 2);
    REQUIRE((*renderer)->temporalStatus().vendorFallback == VendorFallback::None);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE((*renderer)->temporalStatus().vendorScalerGeneration == 1);
}

//======================================================================================================================
TEST_CASE("vendor declarations keep separate histories and record their inputs",
          "[render][temporal][vendor]") {
    FakeDevice device;
    device.deviceCaps.temporalScaler = supportedVendor();
    auto renderer = Renderer::create(device, 64, 64, false);
    REQUIRE(renderer);
    auto view = vendorView(ReconstructionMode::NativeTaa);
    declaredFrame(device, **renderer, view);
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    const auto dump = declaredFrame(device, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE(dump.find("external \"lmx.pass.temporal.vendor\"") != std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.resolve") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.upscale") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.temporal.commit") == std::string::npos);
    REQUIRE(device.commands.temporalScales.size() == 1);
    REQUIRE(device.commands.temporalScales.back().inputContentWidth == 64);
    REQUIRE(device.commands.temporalScales.back().reset);
    std::ifstream file(std::string(LMX_REPO_ROOT) + "/Tests/Golden/frame-temporal-vendor.txt");
    REQUIRE(file.good());
    std::ostringstream golden;
    golden << file.rdbuf();
    REQUIRE(dump == golden.str());
    view.temporal.renderScale = 0.5f;
    declaredFrame(device, **renderer, view);
    REQUIRE_FALSE((*renderer)->temporalStatus().vendorReset);
    REQUIRE(device.temporalScalerCreations.size() == 1);
    REQUIRE(device.commands.temporalScales.back().inputContentWidth == 32);
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    declaredFrame(device, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyAge == 4);
    view.temporal.enabled = false;
    declaredFrame(device, **renderer, view);
    view.temporal.enabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    declaredFrame(device, **renderer, view);
    REQUIRE((*renderer)->temporalStatus().vendorReset);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);
}

//======================================================================================================================
TEST_CASE("unread vendor depth keeps its opaque access until overwritten",
          "[render][temporal][vendor]") {
    FakeDevice device;
    device.deviceCaps.temporalScaler = supportedVendor();
    auto renderer = Renderer::create(device, 64, 64, false);
    REQUIRE(renderer);
    auto view = vendorView(ReconstructionMode::VendorTemporal);
    view.temporal.debugView = TemporalDebugView::Off;
    declaredFrame(device, **renderer, view);
    declaredFrame(device, **renderer, view);
    const auto reuse = declaredFrame(device, **renderer, view);
    REQUIRE(reuse.find("texture r3 mips[0..] layers[0..] ExternalRead -> RenderTarget") !=
            std::string::npos);
    view.temporal.enabled = false;
    const auto disabled = declaredFrame(device, **renderer, view);
    REQUIRE(disabled.find("texture r3 mips[0..] layers[0..] ExternalRead -> RenderTarget") !=
            std::string::npos);
}
