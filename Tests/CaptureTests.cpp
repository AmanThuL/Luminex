#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

#include "RHI/Metal4/Metal4Capture.h"
#include "RHI/RHI.h"

// MTL_CAPTURE_ENABLED must be in the environment before Metal initializes, so it is set at
// static-init time -- before Catch2's main creates any device. This is the plumbing the M1
// review said the guard test needed. It only *permits* capture; tests that want it off rely
// on beginCapture's own path validation rejecting first.
namespace {
const bool kCaptureEnv = [] {
    ::setenv("MTL_CAPTURE_ENABLED", "1", 0);
    return true;
}();
} // namespace

TEST_CASE("beginCapture rejects bad paths without touching the filesystem", "[gpu]") {
    auto device = lmx::rhi::createDevice();
    REQUIRE(device.has_value());
    REQUIRE_FALSE(lmx::rhi::metal4::beginCapture(**device, ""));
    REQUIRE_FALSE(lmx::rhi::metal4::beginCapture(**device, "frame.trace")); // not .gputrace
    REQUIRE_FALSE(std::filesystem::exists("frame.trace"));
}

TEST_CASE("begin/endCapture writes a .gputrace document", "[gpu]") {
    auto device = lmx::rhi::createDevice();
    REQUIRE(device.has_value());
    const std::filesystem::path path = "lmx-capture-test.gputrace";
    std::filesystem::remove_all(path);
    if (!lmx::rhi::metal4::beginCapture(**device, path.string())) {
        // Capture support can be absent (headless CI); the guard above is the required
        // coverage, the happy path is best-effort. SKIP keeps that honest.
        SKIP("programmatic capture unavailable in this environment");
    }
    (*device)->beginFrame();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    lmx::rhi::metal4::endCapture();
    REQUIRE(std::filesystem::exists(path));
    std::filesystem::remove_all(path);
}
