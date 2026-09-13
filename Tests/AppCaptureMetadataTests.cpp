#include "App/CaptureMetadata.h"

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
    REQUIRE(captureFrameMetadataJson(engine::defaultSceneId(), 32, 31, TemporalMode::Vendor,
                                     render::TemporalDebugView::MotionVectors, 0.5f, status,
                                     "test GPU") ==
            R"({"scene":"sponza","frameCount":32,"simulationFrame":31,)"
            R"("requestedMode":"metalfx","effectiveMode":"taa","fallback":1,)"
            R"("renderScale":0.5,"debugView":1,"device":"test GPU"})");
    const auto off = captureFrameMetadataJson(engine::defaultSceneId(), 1, 0, TemporalMode::Off,
                                              render::TemporalDebugView::Off, 1, status, "GPU");
    REQUIRE(off.contains(R"("effectiveMode":"off")"));
}

//======================================================================================================================
TEST_CASE("sequence frame records retain camera and temporal state with container filenames",
          "[app][capture]") {
    render::Camera camera;
    camera.position = {1, 2, 3};
    render::SceneView view;
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
