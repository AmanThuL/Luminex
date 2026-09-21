#include "App/Model/CaptureMetadata.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("capture manifest v2 preserves run fields and declares display, container and UI",
          "[app][capture]") {
    AppOptions options;
    options.frames = 2;
    options.warmup = 32;
    options.renderScale = 0.75f;
    options.temporal = TemporalMode::Vendor;
    const std::vector<std::string> records = {R"({"ordinal":0})", R"({"ordinal":1})"};
    const std::string expected =
        "{\n\"schemaVersion\":2,\"complete\":false,\"scene\":\"sponza\","
        "\"failure\":\"bad \\\"frame\\\"\\u000a\",\"device\":\"test GPU\","
        "\"requestedMode\":\"metalfx\",\"width\":1280,\"height\":720,"
        "\"fps\":60,\"warmup\":32,\"frameCount\":2,\"renderScale\":0.75,"
        "\"visibilityEnabled\":true,\"submission\":\"indirect\",\"classify\":\"cpu\","
        "\"classifyCheck\":false,\"occlusionEnabled\":false,\"occlusionCheck\":false,"
        "\"hzbDebugLevel\":-1,\"labOccluders\":0,\"localLightMode\":\"clustered\","
        "\"lightDebugView\":"
        "\"off\",\"lightCheck\":false,\"localLightRig\":true,\"labLights\":256,\"labLightPile\":0,"
        "\"labInstances\":4096,"
        "\"debugView\":0,\"cameraTrack\":true,\"display\":" +
        render::toJson(render::kSdrDisplayDomain) +
        ",\"container\":\"png\",\"ui\":{\"composited\":false},"
        "\"dynamicResolution\":false,\"frames\":[\n{\"ordinal\":0},\n{\"ordinal\":1}\n]}\n";
    REQUIRE(captureManifestJson(options, "test GPU", render::kSdrDisplayDomain, 1280, 720, true,
                                records, false, "bad \"frame\"\n") == expected);
    options.captureFormat = CaptureFormat::Bmp;
    const auto bmp =
        captureManifestJson(options, "GPU", render::kSdrDisplayDomain, 1280, 720, false, {}, true);
    REQUIRE(bmp.contains(R"("container":"bmp")"));
    REQUIRE(bmp.contains(R"("complete":true)"));
    REQUIRE_FALSE(bmp.contains("colorSpace"));
}

//======================================================================================================================
TEST_CASE("PNG frame facts identify actual reconstruction and fallback", "[app][capture]") {
    render::TemporalStatus status;
    status.reconstruction = render::ReconstructionMode::NativeTaa;
    status.vendorFallback = render::VendorFallback::Unsupported;
    REQUIRE(
        captureFrameMetadataJson(engine::defaultSceneId(), 32, 31, TemporalMode::Vendor,
                                 render::TemporalDebugView::MotionVectors, 0.5f, status,
                                 "test GPU") ==
        R"({"scene":"sponza","frameCount":32,"simulationFrame":31,)"
        R"("requestedMode":"metalfx","effectiveMode":"taa","fallback":1,)"
        R"("renderScale":0.5,"debugView":1,"visibilityEnabled":true,)"
        R"("submission":"indirect","classify":"cpu","classifyCheck":false,"visibility":null,)"
        R"("localLightMode":"clustered","lightDebugView":"off","lightCheck":false,)"
        R"("localLightRig":true,"labLights":256,"labLightPile":0,"lighting":null,"liveLightCount":0,)"
        R"("labInstances":4096,"device":"test GPU"})");
    const auto off = captureFrameMetadataJson(engine::defaultSceneId(), 1, 0, TemporalMode::Off,
                                              render::TemporalDebugView::Off, 1, status, "GPU");
    REQUIRE(off.contains(R"("effectiveMode":"off")"));
    const auto configured = captureFrameMetadataJson(
        engine::defaultSceneId(), 1, 0, TemporalMode::Off, render::TemporalDebugView::Off, 1,
        status, "GPU", false, render::SubmissionMode::Batched, 1024);
    REQUIRE(configured.contains(
        R"("visibilityEnabled":false,"submission":"batched","classify":"cpu","classifyCheck":false,"visibility":null,"localLightMode":"clustered","lightDebugView":"off","lightCheck":false,"localLightRig":true,"labLights":256,"labLightPile":0,"lighting":null,"liveLightCount":0,"labInstances":1024)"));
}

//======================================================================================================================
TEST_CASE("sequence frame records retain camera and temporal state with container filenames",
          "[app][capture]") {
    engine::Camera camera;
    camera.position = {1, 2, 3};
    render::SceneView view;
    REQUIRE(view.localLightMode == engine::LocalLightMode::Clustered);
    render::TemporalStatus status;
    status.reconstruction = render::ReconstructionMode::VendorTemporal;
    status.vendorName = "MetalFX";
    const auto record =
        captureRecordJson(0, 60, camera, view, status, "frame-000000.png", TemporalMode::Vendor);
    REQUIRE(record.contains(R"("simulationFrame":60,"timeSeconds":1)"));
    REQUIRE(record.contains(R"("file":"frame-000000.png")"));
    REQUIRE(record.contains(R"("position":[1,2,3])"));
    REQUIRE(record.contains(R"("effectiveMode":"metalfx","fallback":0,"vendorName":"MetalFX")"));
    for (std::string_view key :
         {"yaw",          "pitch",           "fovY",           "nearZ",          "farZ",
          "renderWidth",  "renderHeight",    "effectiveScale", "jitterIndex",    "jitterEnabled",
          "historyAge",   "lastResetReason", "lastResetFrame", "vendorReset",    "exposureEv",
          "autoExposure", "bloom",           "bloomThreshold", "bloomIntensity", "shadowFilter"}) {
        CAPTURE(key);
        REQUIRE(record.contains(std::string("\"") + std::string(key) + "\":"));
    }
}

//======================================================================================================================
TEST_CASE("capture metadata retains lighting requests and exact retired context",
          "[app][capture][light-check]") {
    lmx::app::AppOptions options;
    options.localLightMode = lmx::engine::LocalLightMode::Clustered;
    options.lightCheck = true;
    options.lightDebugView = lmx::engine::LightDebugView::Missed;
    options.labLights = 1024;
    options.dynamicResolution = true;
    const auto manifest =
        lmx::app::captureManifestJson(options, "test", {}, 1280, 720, true, {}, false);
    REQUIRE(manifest.find("\"localLightMode\":\"clustered\"") != std::string::npos);
    REQUIRE(manifest.find("\"labLights\":1024") != std::string::npos);
    REQUIRE(manifest.find("\"dynamicResolution\":true") != std::string::npos);
    lmx::render::SceneView view;
    view.localLightMode = options.localLightMode;
    view.lightCheck = true;
    view.lightDebugView = options.lightDebugView;
    view.tables.liveLightCount = 1024;
    lmx::render::LightingStatus lighting;
    lighting.frameNumber = 14;
    lighting.isRetired = true;
    const auto record = lmx::app::captureRecordJson(0, 13, {}, view, {}, "frame.png",
                                                    options.temporal, nullptr, &lighting);
    REQUIRE(record.find("\"lightDebugView\":\"missed\"") != std::string::npos);
    REQUIRE(record.find("\"liveLightCount\":1024") != std::string::npos);
    REQUIRE(record.find("\"frameId\":14") != std::string::npos);
}
