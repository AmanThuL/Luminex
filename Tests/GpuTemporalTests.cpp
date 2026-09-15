#include "GpuTemporalTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Parity: with temporal off the renderer declares the frame it declared before temporal existed.
// The golden matches the M5.5-tip renderer's dump of the same frame, so a declaration, an import,
// or an attachment that the temporal path leaked into the off path fails here rather than in a
// screenshot nobody diffs. It is a regression guard over this renderer's own output, not
// independent evidence: the parity claim rests on the three screenshot hashes.
TEST_CASE("the temporal-off frame declares the pre-temporal graph", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    const FixtureSceneView view = temporalSceneView(items);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, temporalCamera(), lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(1);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal-off.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The declared temporal frame under the default reconstruction, as the spec's pass table states
// it: the exposure seed a manual-mode temporal frame declares to record the applied exposure pair
// (M6.2 spec 7, exported because its consumer is the next frame), the scene pass carrying the
// motion and reactive attachments, the reprojection diagnostic declared but culled because the
// Motion view does not sink it, the resolve writing this frame's colour slot with no copy behind
// it, and the debug view overwriting the display target after the display pass produced it.
TEST_CASE("a temporal frame declares the motion, resolve and debug view passes",
          "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;

    // The dumped frame is the second one, so its history is valid and the record shows the
    // reprojection pass a frame with no history could not declare at all.
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, temporalCamera(), lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(2);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The raw bypass declaring the same inputs and none of the accumulation: the commit copy makes this
// frame's raw colour the slot's contents, no resolve pass exists, and bloom and display read the
// raw scene colour. It is the declaration a test comparing the two modes at identical inputs rests
// on, so it is pinned by a golden of its own rather than inferred from the accumulated one.
TEST_CASE("a raw temporal frame declares the commit copy and no resolve", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;

    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, temporalCamera(), lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(2);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal-raw.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The same raw frame rasterising into half of each axis. The declaration differs in exactly three
// places: the scene pass carries a render area of the active rectangle, the one-to-one copy is
// replaced by the spatial commit that resamples that rectangle into the whole colour slot, and
// bloom and display read the slot the commit wrote rather than the scene colour behind it. Every
// target keeps its output-extent allocation, so no descriptor and no transient footprint moves.
TEST_CASE("an upscaled raw temporal frame declares the spatial commit", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
    view.temporal.renderScale = 0.5f;

    renderFrame(**device, **renderer, temporalCamera(), view);
    const lmx::render::TemporalStatus status = (*renderer)->temporalStatus();
    CHECK(status.upscaled);
    CHECK(status.extents.renderWidth == kSize / 2);
    CHECK(status.extents.renderHeight == kSize / 2);
    CHECK(status.extents.outputWidth == kSize);
    CHECK(status.extents.outputHeight == kSize);
    CHECK(status.renderScale == 0.5f);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, temporalCamera(), lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(2);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    requireMatchesGolden(dump, "frame-temporal-raw-upscaled.txt");
    // Said twice on purpose: the golden pins the whole record, and these two say which part of it
    // the case exists for, so a re-baselined golden cannot quietly drop either.
    CHECK(dump.find("render area 32x32") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The declared upscaled accumulation. At half of each axis under NativeTaa the frame carries the
// scene pass's render area and reaches lmx.pass.temporal.upscale instead of the native resolve --
// which reads the previous depth slot at this frame's extent and so cannot serve a frame whose
// render extent differs from its output extent. No commit copy is declared in either extent: the
// accumulation writes the colour slot itself.
TEST_CASE("an upscaled native TAA frame declares the temporal upscale", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
    view.temporal.renderScale = 0.5f;

    // The dumped frame is the second one, so its history is valid and its predecessor rasterised at
    // the same render extent -- which is what makes this the steady upscaled declaration rather
    // than the first frame after a scale change.
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, temporalCamera(), lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(2);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    requireMatchesGolden(dump, "frame-temporal-upscaled.txt");
    // Said twice on purpose: the golden pins the whole record, and these say which part of it the
    // case exists for, so a re-baselined golden cannot quietly drop any of them.
    CHECK(dump.find("render area 32x32") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.upscale") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.resolve") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitUpscaled") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The scale-1 frame declares the copy and never the spatial pass, whichever mode it runs: the
// upscaling kernels are selected by the extents, so a frame whose render extent is its output
// extent must not reach them at all.
TEST_CASE("a scale-1 temporal frame declares no upscaling pass", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    FixtureSceneView view = temporalSceneView({});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.renderScale = 1.0f;
    renderFrame(**device, **renderer, temporalCamera(), view);
    CHECK_FALSE((*renderer)->temporalStatus().upscaled);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(),
                                                    lmx::test::prepareSceneView(view, device)));
    const auto record = graph.compileFrame(2);
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitUpscaled") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.upscale") == std::string::npos);
    CHECK(dump.find("render area") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// ADR 0016's kernel-selection rule, stated directly rather than inferred from an upscaled frame:
// the native kernel addresses every input at one extent, the previous depth slot included, so the
// one scale-1 frame that follows a differently sized render extent runs the upscaling kernel --
// which carries the previous render extent explicitly -- and the frame after it is native again.
TEST_CASE("a scale-1 frame after a scale change declares the upscale kernel once",
          "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;

    // The predecessor: a whole frame at half scale, so the history the following frames read was
    // accumulated with a 32x32 depth slot behind it.
    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    // Declares one frame at `scale` and answers its compiled record's dump. Every call is a
    // declared temporal frame, so the order of the calls is the order of the frames.
    const auto declareDump = [&](float scale) {
        view.temporal.renderScale = scale;
        CommandList& commands = (*device)->beginFrame();
        transients.beginFrame();
        lmx::render::RenderGraph graph(transients);
        graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(),
                                                        lmx::test::prepareSceneView(view, device)));
        const auto record = graph.compileFrame(2);
        INFO((record.has_value() ? std::string{} : record.error().message));
        REQUIRE(record.has_value());
        std::string dump = lmx::render::dumpCompiledFrame(*record);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        return dump;
    };

    const std::string afterChange = declareDump(1.0f);
    CHECK(afterChange.find("lmx.pass.temporal.upscale") != std::string::npos);
    CHECK(afterChange.find("lmx.pass.temporal.resolve") == std::string::npos);

    const std::string steady = declareDump(1.0f);
    CHECK(steady.find("lmx.pass.temporal.resolve") != std::string::npos);
    CHECK(steady.find("lmx.pass.temporal.upscale") == std::string::npos);
}
