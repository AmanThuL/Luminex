#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// An emissive factor of 0.5 with every light off and no environment bound is linear 0.5 at the
// fragment: emissive is radiance the surface produces, so it takes no lighting term and reaches the
// target as itself. Nothing between there and the display target may store it as a display-space
// number: the tone map subtracts its 0.04 black offset (0.5 is above the 0.08 knee and below the
// 0.76 shoulder, so that is the whole of it) and the encode turns the remaining 0.46 into byte 181.
// A scene shader that still encoded would put 188 here, and an 8-bit intermediate would round it
// somewhere else again.
//
// Emissive is what carries this probe now that the ambient term is gone. It is the one input that
// still puts a chosen linear value on a surface without routing it through a BRDF, which is what
// keeps the display transform the only thing this case measures.
TEST_CASE("the display transform tone maps and encodes the scene's linear output", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(2.0f), "lmx.test.encodePlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*plane,
         .model = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}, .emissive = {0.5f, 0.5f, 0.5f}}},
    }};

    FixtureSceneView view;
    view.items = items;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const int want = lmx::test::displayByte(0.5f);
    const Pixel probe = pixelAt(pixels, 32, 32);
    INFO(describe("emissive-only white", 32, 32, probe));
    REQUIRE(channelNear(probe.r, want, 6));
    REQUIRE(channelNear(probe.g, want, 6));
    REQUIRE(channelNear(probe.b, want, 6));
    REQUIRE(probe.a == 255);
}

//======================================================================================================================
// The scene target is scene-linear and unbounded, and exposure is a multiply applied before it.
//
// An emissive factor of 4.0 puts linear 4.0 at the fragment -- four times what an 8-bit unorm
// target can hold -- so the readback finding exactly 4.0 is what says no display-space
// intermediate stands between the shading and the target. 4.0 and 8.0 are exact in binary16, so
// these are equalities: raising exposure by one EV doubles the stored radiance and nothing else.
//
// The clear is probed on the same terms. It is authored in display space, decoded once when the
// pass is declared, and pre-exposed with everything else -- if it were not, an exposure change
// would move the shaded pixels and leave the background behind, which is the failure this pins.
TEST_CASE("the scene target holds radiance above 1.0 and exposure scales it exactly", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(2.0f), "lmx.test.exposurePlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*plane,
         .model = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}, .emissive = {4.0f, 4.0f, 4.0f}}},
    }};

    FixtureSceneView view;
    view.items = items;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    // An unset SceneView must render at unit exposure, or every existing probe in this file moves.
    REQUIRE(view.exposureEv == 0.0f);

    std::vector<uint16_t> texels(size_t{kSize} * kSize * 4);
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto renderAtExposure = [&](float exposureEv) {
        view.exposureEv = exposureEv;
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    };

    renderAtExposure(0.0f);
    const HalfPixel litAtZero = halfPixelAt(texels, 32, 32);
    REQUIRE(litAtZero.r == halfBits(4.0f));
    REQUIRE(litAtZero.g == halfBits(4.0f));
    REQUIRE(litAtZero.b == halfBits(4.0f));

    // Radiance of 4.0 reaches the display target as 253, not 255: the tone map's shoulder
    // compresses it. Clipping it to 1.0 anywhere upstream would have written 255 instead.
    const Pixel displayAtZero = pixelAt(pixels, 32, 32);
    INFO(describe("emissive 4.0 through the display transform", 32, 32, displayAtZero));
    REQUIRE(channelNear(displayAtZero.r, lmx::test::displayByte(4.0f), 2));

    // The renderer's authored clear is 0.05 in its red channel; scene-linear, that is 0.003936.
    const HalfPixel clearAtZero = halfPixelAt(texels, 2, 2);
    INFO(describe("scene clear, exposure 0", 2, 2, displayAtZero));
    REQUIRE(floatOfHalfBits(clearAtZero.r) ==
            Catch::Approx(lmx::test::srgbDecode(kSceneClear[0])).epsilon(0.001));

    renderAtExposure(1.0f);
    const HalfPixel litAtOne = halfPixelAt(texels, 32, 32);
    REQUIRE(litAtOne.r == halfBits(8.0f));
    REQUIRE(litAtOne.g == halfBits(8.0f));
    REQUIRE(litAtOne.b == halfBits(8.0f));

    // Doubling a binary16 value is exact, so the clear's two readings compare without a tolerance.
    const HalfPixel clearAtOne = halfPixelAt(texels, 2, 2);
    REQUIRE(floatOfHalfBits(clearAtOne.r) == 2.0f * floatOfHalfBits(clearAtZero.r));
    REQUIRE(floatOfHalfBits(clearAtOne.g) == 2.0f * floatOfHalfBits(clearAtZero.g));
    REQUIRE(floatOfHalfBits(clearAtOne.b) == 2.0f * floatOfHalfBits(clearAtZero.b));
}

//======================================================================================================================
// Auto-exposure's cross-frame read through the real Renderer graph -- not a kernel dispatched in
// isolation (Tests/Render/Passes/Exposure/GpuExposureBloomTests.cpp covers the exact kernel oracle)
// but the actual scene pass over two command buffers kept in flight together. Frame 1 seeds
// exp2(manual EV), shades, meters, and resolves; frame 2 is submitted immediately, without
// waitIdle, and shades from frame 1's result. Waiting only after both submissions is what exercises
// the explicit cross-frame producer barrier instead of accidentally serialising the test on the
// CPU. The whole loop has no CPU readback; after retirement the final target must contain a finite
// positive pre-exposed value, while Metal validation checks the access itself is hazard-free.
TEST_CASE("auto exposure applies through the real scene pass with no CPU readback", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane = lmx::test::fixtureMesh(**device, lmx::engine::makePlane(2.0f),
                                        "lmx.test.autoExposurePlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*plane,
         .model = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}, .emissive = {4.0f, 4.0f, 4.0f}}},
    }};

    FixtureSceneView view;
    view.items = items;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    view.autoExposureEnabled = true;
    view.exposureEv = 0.0f; // reset frame's manual value: exp2(0) == 1

    const auto submitFrame = [&]() {
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
    };

    view.exposureReset = true;
    submitFrame();

    view.exposureReset = false;
    submitFrame();
    (*device)->waitIdle();

    std::vector<uint16_t> texels(size_t{kSize} * kSize * 4);
    (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));
    const HalfPixel frame2 = halfPixelAt(texels, 32, 32);
    const float frame2R = floatOfHalfBits(frame2.r);
    INFO("frame 2 lit texel r = " << frame2R);
    REQUIRE(std::isfinite(frame2R));
    REQUIRE(frame2R > 0.0f);
}

//======================================================================================================================
// depthTarget() is public and may be sampled by a compute pass after the renderer's graph. Keep
// that dispatch in flight while the next frame starts writing depth again: the renderer's
// previous-frame ShaderRead import must make the new attachment wait on the dispatch stage, not
// merely on the previous fragment attachment work.
TEST_CASE("a depth sample is ordered before the next frame overwrites depth", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto renderer = Renderer::create(**device, kSize, kSize);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/BloomThreshold");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeBloomThreshold",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.depthSamplePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    constexpr uint32_t kProbeSize = kSize / 2;
    auto probe = (*device)->createTexture({.width = kProbeSize,
                                           .height = kProbeSize,
                                           .format = Format::RGBA16Float,
                                           .storageWrite = true,
                                           .label = "lmx.test.depthSampleProbe"});
    INFO(errorOf(probe));
    REQUIRE(probe.has_value());

    struct BloomThresholdParams {
        float threshold;
        uint32_t srcWidth;
        uint32_t srcHeight;
        uint32_t dstWidth;
        uint32_t dstHeight;
    };
    constexpr BloomThresholdParams kParams{.threshold = 0.0f,
                                           .srcWidth = kSize,
                                           .srcHeight = kSize,
                                           .dstWidth = kProbeSize,
                                           .dstHeight = kProbeSize};
    FixtureSceneView view;

    CommandList& first = (*device)->beginFrame();
    (*renderer)->render(first, sceneCamera(), lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    first.textureBarrier((*renderer)->depthTarget(), TextureUse::RenderTarget,
                         TextureUse::ShaderRead);
    first.beginComputePass("lmx.test.sampleDepth");
    first.bindComputePipeline(**pipeline);
    first.bindTexture(0, (*renderer)->depthTarget());
    first.bindStorageTexture(1, **probe, {}, StorageAccess::Write);
    first.bindFrameData(0, kParams);
    first.dispatch(kProbeSize / 8, kProbeSize / 8, 1);
    first.endComputePass();
    (*device)->endFrame(nullptr);

    CommandList& second = (*device)->beginFrame();
    (*renderer)->render(second, sceneCamera(), lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// Exercise Renderer.cpp's own odd-size bloom allocation and DisplayTransform's bloom-off fallback,
// not just the kernels in isolation. The byte checks are a basic output oracle; under Metal Shader
// Validation this also proves the odd final row/column and the 1x1 disabled fallback perform no
// out-of-bounds texture loads.
TEST_CASE("odd renderer extents and disabled bloom stay within the bloom texture", "[gpu]") {
    using namespace rojoRHI;

    constexpr uint32_t kOddWidth = 5, kOddHeight = 3;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto renderer = Renderer::create(**device, kOddWidth, kOddHeight, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    FixtureSceneView view;
    const auto renderWithBloom = [&](bool enabled) {
        view.bloomEnabled = enabled;
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::vector<uint8_t> pixels(size_t{kOddWidth} * kOddHeight * 4);
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
        for (size_t texel = 0; texel < size_t{kOddWidth} * kOddHeight; ++texel) {
            REQUIRE(pixels[texel * 4 + 3] == 255);
        }
    };

    renderWithBloom(true);
    renderWithBloom(false);
}
