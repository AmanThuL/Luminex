#include "GpuRendererTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Separated red and blue cubes catch argument-table last-write reuse across per-draw uniforms.
TEST_CASE("renderer draws per-object uniforms in one pass", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.cube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(),
                        lmx::test::prepareSceneView(litSceneView(items), device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    requireTwoCubeImage(pixels, "clear");
}

//======================================================================================================================
// Overlapping geometry must resolve by depth rather than submission order.
TEST_CASE("renderer depth test beats draw order", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane = lmx::test::fixtureMesh(**device, lmx::engine::makePlane(1.0f), "lmx.test.plane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const auto upright = [](float z) {
        return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
               glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    };
    const FixtureDrawItem nearGreen{
        .mesh = &*plane, .model = upright(1.0f), .material = {.albedo = {0.0f, 1.0f, 0.0f, 1.0f}}};
    const FixtureDrawItem farRed{
        .mesh = &*plane, .model = upright(-1.0f), .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}};

    const std::array<std::array<FixtureDrawItem, 2>, 2> orderings = {{
        {{farRed, nearGreen}}, // back-to-front: passes even without a depth buffer
        {{nearGreen, farRed}}, // front-to-back: only depth can keep green on top
    }};
    const std::array<const char*, 2> names = {"far-then-near", "near-then-far"};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (size_t i = 0; i < orderings.size(); ++i) {
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(),
                            lmx::test::prepareSceneView(litSceneView(orderings[i]), device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

        INFO(std::string("ordering: ") + names[i]);

        const Pixel center = pixelAt(pixels, 32, 32);
        INFO(describe("center", 32, 32, center));
        REQUIRE(center.g > 64);
        REQUIRE(center.g > center.r + 32);
        REQUIRE(center.g > center.b + 32);

        const Pixel corner = pixelAt(pixels, 2, 2);
        INFO(describe("corner", 2, 2, corner));
        requireClearPixel(corner);
    }
}

//======================================================================================================================
// A magenta destination makes a missing draw visible while the copied image pins the pass barrier.
TEST_CASE("renderer scene survives a barrier into a sampling pass", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.barrierCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = (*device)->createTexture({.width = kSize,
                                                 .height = kSize,
                                                 .format = Format::BGRA8Unorm,
                                                 .renderTarget = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.barrierDestination"});
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto copyPipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.fullscreenSamplePipeline"});
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    constexpr uint32_t kSourceTextureSlot = 0;

    CommandList& commands = (*device)->beginFrame();
    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    (*renderer)->render(commands, sceneCamera(),
                        lmx::test::prepareSceneView(litSceneView(items), device),
                        /*barrierForSampling=*/true);

    commands.beginRenderPass({.colorTarget = destination->get(),
                              .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                              .clear = true,
                              .label = "lmx.test.rendererCopy"});
    commands.bindPipeline(**copyPipeline);
    commands.bindTexture(kSourceTextureSlot, (*renderer)->colorTarget());
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());

    requireTwoCubeImage(pixels, "copied clear");
}

//======================================================================================================================
// An interior probe and total coverage bound distinguish wireframe from empty and solid output.
TEST_CASE("a wireframe SceneView leaves the interior of a face unfilled", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.wireCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*cube,
         .model = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto renderWith = [&](bool wireframe) {
        FixtureSceneView view = litSceneView(items);
        view.wireframe = wireframe;
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    };

    renderWith(false);
    const Pixel solid = pixelAt(pixels, 26, 26);
    INFO(describe("solid interior", 26, 26, solid));
    // Re-derived for the GGX model: under Blinn-Phong this probe only had to clear 128, which the
    // old diffuse term (albedo times light strength, with no 1/pi) reached easily. The cube's front
    // face is a white dielectric at the default roughness, lit head-on by litSceneView's only
    // light, so the value is what the CPU mirror of the BRDF returns for N = L = V, through the
    // display transform. The probe sits about 8 degrees off the view axis, which costs it a byte; 3
    // covers that and the target's own rounding.
    const int litByte =
        lmx::test::displayByte(lmx::test::brdf::directionalLight(
                                   {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                                   {0.0f, 0.0f, 1.0f}, {.baseColor = glm::vec3(1.0f)})
                                   .r);
    INFO("expected lit byte " << litByte);
    REQUIRE(channelNear(solid.r, litByte, 3));
    REQUIRE(channelNear(solid.g, litByte, 3));
    REQUIRE(channelNear(solid.b, litByte, 3));

    renderWith(true);
    const Pixel wire = pixelAt(pixels, 26, 26);
    INFO(describe("wireframe interior", 26, 26, wire));
    requireClearPixel(wire);
    REQUIRE(wire.a == 255);
}

//======================================================================================================================
// The corner pins the sky through the display transform; a cube probe proves depth keeps geometry
// in front of it.
TEST_CASE("the sky pass fills the background behind the scene", "[gpu]") {
    using namespace rojoRHI;

    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const TextureMip skyMip{.data = kSkyTexel.data(), .bytesPerRow = 4};
    const std::array<TextureMip, 6> skyFaces = {skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.skySceneCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto skySphere = lmx::test::fixtureMesh(
        **device, lmx::engine::fromGeo(lmx::asset::makeSphere(0.5f, 20, 20)), "lmx.test.skySphere");
    INFO(errorOf(skySphere));
    REQUIRE(skySphere.has_value());

    auto skyCubemap = (*device)->createTexture({.width = 1,
                                                .height = 1,
                                                .format = Format::RGBA8Unorm,
                                                .kind = TextureKind::Cube,
                                                .sampled = true,
                                                .label = "lmx.test.skyCubemap"},
                                               skyFaces);
    INFO(errorOf(skyCubemap));
    REQUIRE(skyCubemap.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    FixtureSceneView view = litSceneView(items);
    view.skySphere = &*skySphere;
    view.skyCubemap = skyCubemap->get();

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // kSkyTexel is uploaded to a plain RGBA8Unorm cubemap, not an sRGB view, so the sampler hands
    // the shader (0, 0.502, 1.0) as linear radiance and the sky shader passes it through untouched
    // -- the display transform is the only thing between the texel and the target. Its peak
    // channel is 1.0, above the 0.76 shoulder, so unlike the mid-grey probes this one exercises
    // the compression *and* the desaturation that comes with it, which is what lifts the black
    // channel off 0 (bytes 33, 179, 241).
    const std::array<int, 3> skyBytes = lmx::test::displayBytes(
        {kSkyTexel[0] / 255.0f, kSkyTexel[1] / 255.0f, kSkyTexel[2] / 255.0f});
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("sky corner", 2, 2, corner));
    REQUIRE(channelNear(corner.r, skyBytes[0], 4));
    REQUIRE(channelNear(corner.g, skyBytes[1], 6));
    REQUIRE(channelNear(corner.b, skyBytes[2], 4));
    REQUIRE(corner.a == 255);

    const Pixel right = pixelAt(pixels, 48, 32);
    INFO(describe("blue cube under the sky", 48, 32, right));
    REQUIRE(right.b > 64);
    // Green separates the two: the sky is half-strength green, and the blue cube only picks up
    // what its specular lobe and its environment reflection carry there.
    REQUIRE(right.g < skyBytes[1] - 32);
}

//======================================================================================================================
// The editor reads these labels straight out of the device, so the frame's passes have to arrive
// named and in the order the graph ran them -- an unnamed or missing pass is an invisible pass.
TEST_CASE("pass timings name every pass the graph ran", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.timedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(),
                        lmx::test::prepareSceneView(litSceneView(items), device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    // waitIdle retires the measured frame; the next beginFrame is what publishes its counters.
    (*device)->beginFrame();
    (*device)->endFrame(nullptr);

    // Bloom is enabled by default (spec 10) and its passes always schedule with it -- kSize == 64
    // gives a 32-wide bloom base, which clamps to Renderer.cpp's full kMaxBloomDownsampleLevels ==
    // 4: threshold, 4 downsample passes, 4 upsample passes. Auto exposure is off by default (spec
    // 9), so the histogram/resolve/seed passes are culled and do not appear here -- see the
    // culling test in RenderGraphTests.cpp for that half of the picture.
    const std::span<const PassTiming> timings = (*device)->passTimings();
    REQUIRE(timings.size() == 12);
    REQUIRE(timings[0].label == "lmx.pass.shadow");
    REQUIRE(timings[1].label == "lmx.pass.scene");
    REQUIRE(timings[2].label == "lmx.pass.bloom.threshold");
    REQUIRE(timings[3].label == "lmx.pass.bloom.downsample0");
    REQUIRE(timings[4].label == "lmx.pass.bloom.downsample1");
    REQUIRE(timings[5].label == "lmx.pass.bloom.downsample2");
    REQUIRE(timings[6].label == "lmx.pass.bloom.downsample3");
    REQUIRE(timings[7].label == "lmx.pass.bloom.upsample3");
    REQUIRE(timings[8].label == "lmx.pass.bloom.upsample2");
    REQUIRE(timings[9].label == "lmx.pass.bloom.upsample1");
    REQUIRE(timings[10].label == "lmx.pass.bloom.upsample0");
    REQUIRE(timings[11].label == "lmx.pass.display");
    for (const PassTiming& timing : timings) {
        INFO(timing.label + ": " + std::to_string(timing.gpuMilliseconds) + " ms");
        REQUIRE(timing.gpuMilliseconds > 0.0);
    }
}

//======================================================================================================================
// The editor's frame shape: the scene passes plus one joined pass that reads what they rendered.
// Nothing here places a barrier -- the read declaration is the only thing standing between the
// scene pass's writes and this pass's sample, so a correct image is what proves the graph derived
// the transition.
TEST_CASE("a joined pass samples the scene colour the graph rendered", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.joinedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = makeProbeTarget(**device, "lmx.test.joinedDestination");
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto copyPipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                           .vertexEntry = "vertexMain",
                                                           .fragmentEntry = "fragmentMain",
                                                           .colorFormat = Format::BGRA8Unorm,
                                                           .label = "lmx.test.joinedCopyPipeline"});
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    constexpr uint32_t kSourceTextureSlot = 0;

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    const FixtureSceneView view = litSceneView(items);

    // declarePasses() always declares bloom's transients (spec 10), so a caller building its own
    // graph around it needs a pool exactly as Renderer::render()'s convenience path does.
    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture sceneColor = (*renderer)->declarePasses(
        graph, commands, sceneCamera(), lmx::test::prepareSceneView(view, device));
    const lmx::render::GraphTexture copyTarget =
        graph.importTexture(**destination, Format::BGRA8Unorm, "destination");

    lmx::render::PassDesc copy;
    copy.textureReads.push_back(sceneColor);
    // Magenta makes a pass that drew nothing at all obvious rather than merely wrong.
    copy.color =
        lmx::render::ColorAttachment{.handle = copyTarget, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
    graph.addPass("lmx.pass.copy", std::move(copy),
                  [&](const lmx::render::PassResources& resources) {
                      const auto source = resources.texture(sceneColor);
                      REQUIRE(source.has_value());
                      commands.bindPipeline(**copyPipeline);
                      commands.bindTexture(kSourceTextureSlot, **source);
                      commands.draw(3);
                  });
    graph.exportTexture(lmx::render::nextVersion(copyTarget));
    graph.execute(commands, (*device)->frameNumber());

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());
    requireTwoCubeImage(pixels, "clear sampled through the graph");
}

//======================================================================================================================
// Validation has to be live in the path the frame actually takes, not only in a unit test of the
// declaration layer: a pass body reaching for a resource it never declared is refused mid-frame,
// with the pass and the resource named.
TEST_CASE("a pass resolving an undeclared texture is refused while the frame runs", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto declaredTarget = makeProbeTarget(**device, "lmx.test.declaredTarget");
    INFO(errorOf(declaredTarget));
    REQUIRE(declaredTarget.has_value());
    auto undeclaredTarget = makeProbeTarget(**device, "lmx.test.undeclaredTarget");
    INFO(errorOf(undeclaredTarget));
    REQUIRE(undeclaredTarget.has_value());

    lmx::render::RenderGraph graph;
    const lmx::render::GraphTexture declared =
        graph.importTexture(**declaredTarget, Format::BGRA8Unorm, "declaredTarget");
    const lmx::render::GraphTexture undeclared =
        graph.importTexture(**undeclaredTarget, Format::BGRA8Unorm, "undeclaredTarget");

    std::optional<lmx::render::GraphError> refusal;
    lmx::render::PassDesc probe;
    probe.color = lmx::render::ColorAttachment{.handle = declared};
    graph.addPass("lmx.pass.probe", std::move(probe),
                  [&](const lmx::render::PassResources& resources) {
                      const auto texture = resources.texture(undeclared);
                      if (!texture) {
                          refusal = texture.error();
                      }
                  });
    graph.exportTexture(lmx::render::nextVersion(declared));

    CommandList& commands = (*device)->beginFrame();
    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    REQUIRE(refusal.has_value());
    INFO(refusal->message);
    REQUIRE(refusal->message.contains("lmx.pass.probe"));
    REQUIRE(refusal->message.contains("undeclaredTarget"));
    REQUIRE(refusal->message.contains("did not declare"));
}
