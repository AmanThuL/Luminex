#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Render/Camera.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"

#include <cmath>
#include <optional>

using namespace lmx::render;
using Catch::Approx;

namespace {
constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

//======================================================================================================================
FrameExtents squareExtents(uint32_t side) {
    return FrameExtents{side, side, side, side};
}

//======================================================================================================================
FrameSignature baseSignature() {
    return FrameSignature{7, FrameExtents{kWidth, kHeight, kWidth, kHeight}, glm::radians(60.0f),
                          0.1f, true};
}

// The motion convention's UV, written out from the projection's own algebra rather than reused
// from the code under test: a view-space point (x, y, -d) with d > 0 projects to
// clip = (x / (aspect * t), y / t, nearZ, d) with t = tan(fovY / 2), so the texture-space UV is
// (0.5 + 0.5 * x / (aspect * t * d), 0.5 - 0.5 * y / (t * d)).
//======================================================================================================================
glm::vec2 analyticUv(const glm::vec3& viewPoint, float fovY, float aspect) {
    const float t = std::tan(fovY * 0.5f);
    const float d = -viewPoint.z;
    return {0.5f + 0.5f * viewPoint.x / (aspect * t * d), 0.5f - 0.5f * viewPoint.y / (t * d)};
}
} // namespace

//======================================================================================================================
TEST_CASE("the jitter sequence is Halton(2,3) centred on the pixel", "[render][temporal]") {
    // The sequence is phased from 1, so index 0 through 3 evaluate the radical inverses at 1
    // through 4. Hand-computed: base 2 gives 1/2, 1/4, 3/4, 1/8 and base 3 gives 1/3, 2/3, 1/9,
    // 4/9. Each axis is that value minus half a pixel.
    const glm::vec2 j0 = haltonJitterPixels(0);
    CHECK(j0.x == Approx(0.0f).margin(1e-7f));
    CHECK(j0.y == Approx(-1.0f / 6.0f));

    const glm::vec2 j1 = haltonJitterPixels(1);
    CHECK(j1.x == Approx(-0.25f));
    CHECK(j1.y == Approx(1.0f / 6.0f));

    const glm::vec2 j2 = haltonJitterPixels(2);
    CHECK(j2.x == Approx(0.25f));
    CHECK(j2.y == Approx(-7.0f / 18.0f));

    const glm::vec2 j3 = haltonJitterPixels(3);
    CHECK(j3.x == Approx(-0.375f));
    CHECK(j3.y == Approx(-1.0f / 18.0f));
}

//======================================================================================================================
TEST_CASE("every jitter sample stays within half a pixel of the centre", "[render][temporal]") {
    // Strictly inside, on both bounds: the 1-based phase excludes the pixel corner.
    for (uint32_t index = 0; index < kJitterSequenceLength; ++index) {
        const glm::vec2 jitter = haltonJitterPixels(index);
        CHECK(jitter.x > -0.5f);
        CHECK(jitter.x < 0.5f);
        CHECK(jitter.y > -0.5f);
        CHECK(jitter.y < 0.5f);
    }
}

//======================================================================================================================
TEST_CASE("the jitter sequence repeats after its declared length", "[render][temporal]") {
    for (uint32_t index = 0; index < kJitterSequenceLength; ++index) {
        const glm::vec2 first = haltonJitterPixels(index);
        const glm::vec2 wrapped = haltonJitterPixels(index + kJitterSequenceLength);
        const glm::vec2 twice = haltonJitterPixels(index + 3 * kJitterSequenceLength);
        CHECK(first.x == wrapped.x);
        CHECK(first.y == wrapped.y);
        CHECK(first.x == twice.x);
        CHECK(first.y == twice.y);
    }
}

//======================================================================================================================
TEST_CASE("jitter shifts clip x and y by two pixels of NDC per pixel", "[render][temporal]") {
    Camera camera;
    camera.position = {1.0f, 2.0f, 3.0f};
    camera.yaw = 0.4f;
    camera.pitch = -0.2f;

    const FrameExtents extents{kWidth, kHeight, kWidth, kHeight};
    const glm::vec2 jitter{0.3f, -0.2f};
    const CameraFrameState state = buildCameraFrameState(camera, extents, jitter);

    const glm::vec4 viewPoint = state.view * glm::vec4{-2.0f, 0.5f, -6.0f, 1.0f};
    const glm::vec4 clip = state.projection * viewPoint;
    const glm::vec4 jittered = state.projectionJittered * viewPoint;

    // The NDC translate scales with w, because it is applied before the perspective divide.
    CHECK(jittered.x == Approx(clip.x + 2.0f * jitter.x / kWidth * clip.w));
    CHECK(jittered.y == Approx(clip.y + 2.0f * jitter.y / kHeight * clip.w));
    CHECK(jittered.z == Approx(clip.z));
    CHECK(jittered.w == Approx(clip.w));
    CHECK(state.jitterPixels.x == jitter.x);
    CHECK(state.jitterPixels.y == jitter.y);
}

//======================================================================================================================
TEST_CASE("zero jitter leaves the projection bit-identical", "[render][temporal]") {
    Camera camera;
    camera.position = {-3.0f, 1.5f, 2.0f};
    camera.yaw = -0.9f;
    camera.pitch = 0.3f;

    const CameraFrameState state =
        buildCameraFrameState(camera, FrameExtents{kWidth, kHeight, kWidth, kHeight}, {0.0f, 0.0f});
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(state.projectionJittered[column][row] == state.projection[column][row]);
            CHECK(state.viewProjectionJittered[column][row] == state.viewProjection[column][row]);
        }
    }
}

//======================================================================================================================
TEST_CASE("renderExtentsForScale rounds each axis and keeps the output", "[render][temporal]") {
    const FrameExtents full = renderExtentsForScale(kWidth, kHeight, 1.0f);
    CHECK(full.renderWidth == kWidth);
    CHECK(full.renderHeight == kHeight);
    CHECK(full.outputWidth == kWidth);
    CHECK(full.outputHeight == kHeight);

    const FrameExtents half = renderExtentsForScale(kWidth, kHeight, 0.5f);
    CHECK(half.renderWidth == 640);
    CHECK(half.renderHeight == 360);
    CHECK(half.outputWidth == kWidth);
    CHECK(half.outputHeight == kHeight);

    // 1280 * 0.71 is 908.8 and 720 * 0.71 is 511.2: each axis rounds on its own.
    const FrameExtents odd = renderExtentsForScale(kWidth, kHeight, 0.71f);
    CHECK(odd.renderWidth == 909);
    CHECK(odd.renderHeight == 511);
}

//======================================================================================================================
TEST_CASE("renderExtentsForScale never falls below one pixel", "[render][temporal]") {
    const FrameExtents tiny = renderExtentsForScale(1, 1, 0.5f);
    CHECK(tiny.renderWidth == 1);
    CHECK(tiny.renderHeight == 1);
    CHECK(tiny.outputWidth == 1);
    CHECK(tiny.outputHeight == 1);
}

//======================================================================================================================
TEST_CASE("jitterTexelOffset flips y into texture space", "[render][temporal]") {
    const glm::vec2 offset = jitterTexelOffset({0.25f, -0.125f});
    CHECK(offset.x == Approx(0.25f));
    CHECK(offset.y == Approx(0.125f));
}

//======================================================================================================================
TEST_CASE("the jitter texel offset is the shift the jittered matrix applies",
          "[render][temporal]") {
    Camera camera;
    camera.position = {2.0f, 1.0f, -4.0f};
    camera.yaw = 0.7f;
    camera.pitch = -0.15f;

    const FrameExtents extents{640, 360, kWidth, kHeight};
    const glm::vec2 jitter{0.3f, -0.2f};
    const CameraFrameState state = buildCameraFrameState(camera, extents, jitter);

    const glm::vec4 world{-1.5f, 0.75f, -3.0f, 1.0f};
    const glm::vec2 texel = clipToMotionUv(state.viewProjection * world) *
                            glm::vec2{static_cast<float>(extents.renderWidth),
                                      static_cast<float>(extents.renderHeight)};
    const glm::vec2 jitteredTexel = clipToMotionUv(state.viewProjectionJittered * world) *
                                    glm::vec2{static_cast<float>(extents.renderWidth),
                                              static_cast<float>(extents.renderHeight)};

    const glm::vec2 offset = jitterTexelOffset(jitter);
    CHECK(jitteredTexel.x - texel.x == Approx(offset.x).margin(1e-4f));
    CHECK(jitteredTexel.y - texel.y == Approx(offset.y).margin(1e-4f));
}

//======================================================================================================================
TEST_CASE("renderSamplePosition maps an output pixel into the render image", "[render][temporal]") {
    const glm::vec2 jitter{0.3f, -0.2f};
    const glm::vec2 offset = jitterTexelOffset(jitter);

    const FrameExtents half{640, 360, kWidth, kHeight};
    const glm::vec2 scaled = renderSamplePosition({3, 2}, half, jitter);
    CHECK(scaled.x == Approx(1.75f + offset.x));
    CHECK(scaled.y == Approx(1.25f + offset.y));

    const FrameExtents full{kWidth, kHeight, kWidth, kHeight};
    const glm::vec2 native = renderSamplePosition({3, 2}, full, jitter);
    CHECK(native.x == Approx(3.5f + offset.x));
    CHECK(native.y == Approx(2.5f + offset.y));
}

//======================================================================================================================
TEST_CASE("the projection follows the output extent and the jitter the render extent",
          "[render][temporal]") {
    Camera camera;
    camera.position = {0.5f, 2.5f, -1.0f};
    camera.yaw = 1.1f;
    camera.pitch = 0.2f;

    const glm::vec2 jitter{0.3f, -0.2f};
    const CameraFrameState native =
        buildCameraFrameState(camera, FrameExtents{kWidth, kHeight, kWidth, kHeight}, jitter);
    const CameraFrameState upscaled =
        buildCameraFrameState(camera, FrameExtents{640, 360, kWidth, kHeight}, jitter);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(upscaled.projection[column][row] == native.projection[column][row]);
        }
    }

    // Only the jitter differs, and it differs by exactly the render extent's NDC scale.
    const glm::vec4 viewPoint = native.view * glm::vec4{-2.0f, 0.5f, -6.0f, 1.0f};
    const glm::vec4 clip = native.projection * viewPoint;

    const glm::vec4 upscaledClip = upscaled.projectionJittered * viewPoint;
    CHECK(upscaledClip.x == Approx(clip.x + 2.0f * jitter.x / 640.0f * clip.w));
    CHECK(upscaledClip.y == Approx(clip.y + 2.0f * jitter.y / 360.0f * clip.w));

    const glm::vec4 nativeClip = native.projectionJittered * viewPoint;
    CHECK(nativeClip.x == Approx(clip.x + 2.0f * jitter.x / static_cast<float>(kWidth) * clip.w));
    CHECK(nativeClip.y == Approx(clip.y + 2.0f * jitter.y / static_cast<float>(kHeight) * clip.w));
}

//======================================================================================================================
TEST_CASE("the frame state carries the camera's own parameters", "[render][temporal]") {
    Camera camera;
    camera.position = {4.0f, -1.0f, 0.5f};
    camera.fovY = glm::radians(75.0f);
    camera.nearZ = 0.25f;

    // The render extent is deliberately a different aspect ratio (2:1) from the output extent
    // (4:1), so the assertion below distinguishes which one the projection reads.
    const CameraFrameState state =
        buildCameraFrameState(camera, FrameExtents{800, 400, 1600, 400}, {0.1f, 0.1f});
    CHECK(state.position == camera.position);
    CHECK(state.fovY == camera.fovY);
    CHECK(state.nearZ == camera.nearZ);
    CHECK(state.projection[0][0] == Approx(1.0f / (4.0f * std::tan(camera.fovY * 0.5f))));
}

//======================================================================================================================
TEST_CASE("inverseViewProjection round-trips a world point", "[render][temporal]") {
    Camera camera;
    camera.position = {2.0f, 3.0f, 4.0f};
    camera.yaw = 1.1f;
    camera.pitch = -0.35f;

    const CameraFrameState state =
        buildCameraFrameState(camera, FrameExtents{kWidth, kHeight, kWidth, kHeight}, {0.2f, 0.4f});

    const glm::vec4 world{-1.5f, 0.75f, -7.0f, 1.0f};
    const glm::vec4 clip = state.viewProjection * world;
    const glm::vec4 back = state.inverseViewProjection * clip;
    CHECK(back.x / back.w == Approx(world.x).epsilon(1e-4f));
    CHECK(back.y / back.w == Approx(world.y).epsilon(1e-4f));
    CHECK(back.z / back.w == Approx(world.z).epsilon(1e-4f));
}

//======================================================================================================================
TEST_CASE("clipToMotionUv flips y into texture space", "[render][temporal]") {
    // Clip w of 2 exercises the divide; the NDC is then (0.5, -0.5).
    const glm::vec2 uv = clipToMotionUv(glm::vec4{1.0f, -1.0f, 0.5f, 2.0f});
    CHECK(uv.x == Approx(0.75f));
    CHECK(uv.y == Approx(0.75f));
}

//======================================================================================================================
TEST_CASE("motion of a static point under a known camera translation", "[render][temporal]") {
    const uint32_t side = 512;
    const FrameExtents extents = squareExtents(side);
    const float fovY = glm::radians(60.0f);

    Camera previousCamera;
    previousCamera.fovY = fovY;
    previousCamera.position = {0.0f, 0.0f, 0.0f};
    Camera currentCamera = previousCamera;
    const float shiftX = 0.4f;
    currentCamera.position = {shiftX, 0.0f, 0.0f};

    const glm::vec4 world{0.6f, -0.3f, -5.0f, 1.0f};
    const CameraFrameState previous = buildCameraFrameState(previousCamera, extents, {0.0f, 0.0f});
    const CameraFrameState current = buildCameraFrameState(currentCamera, extents, {0.25f, 0.25f});

    const glm::vec2 motion =
        motionBetween(current.viewProjection * world, previous.viewProjection * world);

    const glm::vec2 expected = analyticUv({world.x - shiftX, world.y, world.z}, fovY, 1.0f) -
                               analyticUv({world.x, world.y, world.z}, fovY, 1.0f);
    CHECK(motion.x == Approx(expected.x).epsilon(1e-4f));
    CHECK(motion.y == Approx(expected.y).margin(1e-6f));
    // A camera sliding right moves static geometry left in the image.
    CHECK(motion.x < 0.0f);
}

//======================================================================================================================
TEST_CASE("motion of an object translated in front of a static camera", "[render][temporal]") {
    const uint32_t side = 512;
    const FrameExtents extents = squareExtents(side);
    const float fovY = glm::radians(60.0f);

    Camera camera;
    camera.fovY = fovY;
    const CameraFrameState state = buildCameraFrameState(camera, extents, {-0.4f, 0.1f});

    const glm::vec3 previousWorld{0.2f, 0.1f, -4.0f};
    const glm::vec3 objectDelta{0.5f, -0.25f, 0.0f};
    const glm::vec3 currentWorld = previousWorld + objectDelta;

    const glm::vec2 motion = motionBetween(state.viewProjection * glm::vec4{currentWorld, 1.0f},
                                           state.viewProjection * glm::vec4{previousWorld, 1.0f});

    const glm::vec2 expected =
        analyticUv(currentWorld, fovY, 1.0f) - analyticUv(previousWorld, fovY, 1.0f);
    CHECK(motion.x == Approx(expected.x).epsilon(1e-4f));
    CHECK(motion.y == Approx(expected.y).epsilon(1e-4f));
}

//======================================================================================================================
TEST_CASE("the jittered matrices do not leak into motion", "[render][temporal]") {
    const FrameExtents extents = squareExtents(256);
    Camera camera;
    const CameraFrameState unjittered = buildCameraFrameState(camera, extents, {0.0f, 0.0f});
    const CameraFrameState jittered = buildCameraFrameState(camera, extents, {0.45f, -0.45f});

    const glm::vec4 world{0.3f, 0.4f, -3.0f, 1.0f};
    const glm::vec2 motion =
        motionBetween(jittered.viewProjection * world, unjittered.viewProjection * world);
    CHECK(motion.x == Approx(0.0f).margin(1e-7f));
    CHECK(motion.y == Approx(0.0f).margin(1e-7f));
}

//======================================================================================================================
TEST_CASE("the motion sentinel is a positive infinity a consumer can test", "[render][temporal]") {
    CHECK(std::isinf(kMotionInvalid));
    CHECK(kMotionInvalid > 0.0f);
    CHECK(kMotionFormat == lmx::rhi::Format::RG16Float);
}

//======================================================================================================================
TEST_CASE("no previous signature resets as the first frame", "[render][temporal]") {
    CHECK(deriveHistoryReset(std::nullopt, baseSignature(), false) ==
          HistoryResetReason::FirstFrame);
    CHECK(deriveHistoryReset(std::nullopt, baseSignature(), true) ==
          HistoryResetReason::FirstFrame);
}

//======================================================================================================================
TEST_CASE("re-enabling temporal is distinguishable from the first frame", "[render][temporal]") {
    FrameSignature previous = baseSignature();
    previous.temporalEnabled = false;
    CHECK(deriveHistoryReset(previous, baseSignature(), false) ==
          HistoryResetReason::TemporalEnabled);
}

//======================================================================================================================
TEST_CASE("an unchanged frame with temporal already on needs no reset", "[render][temporal]") {
    CHECK(deriveHistoryReset(baseSignature(), baseSignature(), false) == HistoryResetReason::None);
}

//======================================================================================================================
TEST_CASE("turning temporal off is not a reset of its own", "[render][temporal]") {
    FrameSignature current = baseSignature();
    current.temporalEnabled = false;
    CHECK(deriveHistoryReset(baseSignature(), current, false) == HistoryResetReason::None);
}

//======================================================================================================================
TEST_CASE("a new scene generation resets the history", "[render][temporal]") {
    FrameSignature current = baseSignature();
    current.sceneGeneration = 8;
    CHECK(deriveHistoryReset(baseSignature(), current, false) == HistoryResetReason::SceneChanged);
}

//======================================================================================================================
TEST_CASE("an output extent change resets the history", "[render][temporal]") {
    FrameSignature widerOutput = baseSignature();
    widerOutput.extents.outputWidth += 1;
    CHECK(deriveHistoryReset(baseSignature(), widerOutput, false) ==
          HistoryResetReason::ExtentChanged);

    FrameSignature tallerOutput = baseSignature();
    tallerOutput.extents.outputHeight += 1;
    CHECK(deriveHistoryReset(baseSignature(), tallerOutput, false) ==
          HistoryResetReason::ExtentChanged);
}

//======================================================================================================================
TEST_CASE("a render extent change alone keeps the history", "[render][temporal]") {
    FrameSignature scaled = baseSignature();
    scaled.extents.renderWidth = 640;
    scaled.extents.renderHeight = 360;
    CHECK(deriveHistoryReset(baseSignature(), scaled, false) == HistoryResetReason::None);

    // And back again: the history survives the return to the output extent just as it did the
    // departure from it.
    CHECK(deriveHistoryReset(scaled, baseSignature(), false) == HistoryResetReason::None);
}

//======================================================================================================================
TEST_CASE("a projection parameter change resets the history", "[render][temporal]") {
    FrameSignature widerFov = baseSignature();
    widerFov.fovY = glm::radians(70.0f);
    CHECK(deriveHistoryReset(baseSignature(), widerFov, false) ==
          HistoryResetReason::ProjectionChanged);

    FrameSignature nearer = baseSignature();
    nearer.nearZ = 0.05f;
    CHECK(deriveHistoryReset(baseSignature(), nearer, false) ==
          HistoryResetReason::ProjectionChanged);
}

//======================================================================================================================
TEST_CASE("an explicit camera cut resets the history", "[render][temporal]") {
    CHECK(deriveHistoryReset(baseSignature(), baseSignature(), true) ==
          HistoryResetReason::CameraCut);
}

//======================================================================================================================
TEST_CASE("the reset derivation reports the earliest reason in its order", "[render][temporal]") {
    FrameSignature disabled = baseSignature();
    disabled.temporalEnabled = false;

    FrameSignature everything = baseSignature();
    everything.sceneGeneration = 99;
    everything.extents.renderWidth = 640;
    everything.extents.outputWidth = 640;
    everything.fovY = glm::radians(90.0f);
    everything.nearZ = 0.5f;

    // Enabling outranks every content change, and a cut outranks nothing.
    CHECK(deriveHistoryReset(disabled, everything, true) == HistoryResetReason::TemporalEnabled);

    FrameSignature generationAndExtent = baseSignature();
    generationAndExtent.sceneGeneration = 99;
    generationAndExtent.extents.renderHeight = 480;
    generationAndExtent.extents.outputHeight = 480;
    CHECK(deriveHistoryReset(baseSignature(), generationAndExtent, true) ==
          HistoryResetReason::SceneChanged);

    FrameSignature extentAndProjection = baseSignature();
    extentAndProjection.extents.outputWidth = 1024;
    extentAndProjection.nearZ = 0.2f;
    CHECK(deriveHistoryReset(baseSignature(), extentAndProjection, true) ==
          HistoryResetReason::ExtentChanged);

    FrameSignature projectionOnly = baseSignature();
    projectionOnly.fovY = glm::radians(45.0f);
    CHECK(deriveHistoryReset(baseSignature(), projectionOnly, true) ==
          HistoryResetReason::ProjectionChanged);
}

//======================================================================================================================
TEST_CASE("every reset reason has a stable name", "[render][temporal]") {
    CHECK(historyResetReasonName(HistoryResetReason::None) == "None");
    CHECK(historyResetReasonName(HistoryResetReason::FirstFrame) == "FirstFrame");
    CHECK(historyResetReasonName(HistoryResetReason::TemporalEnabled) == "TemporalEnabled");
    CHECK(historyResetReasonName(HistoryResetReason::SceneChanged) == "SceneChanged");
    CHECK(historyResetReasonName(HistoryResetReason::ExtentChanged) == "ExtentChanged");
    CHECK(historyResetReasonName(HistoryResetReason::ProjectionChanged) == "ProjectionChanged");
    CHECK(historyResetReasonName(HistoryResetReason::CameraCut) == "CameraCut");
}
