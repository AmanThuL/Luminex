#include "GpuTemporalTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// The Raw age view samples its newly committed colour slot. When that slot is recycled two frames
// later, its imported terminal use must drain that read before the next history copy overwrites it.
TEST_CASE("raw history age records the sampled slot before recycling",
          "[gpu][temporal][taa-diagnostics]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    FixtureSceneView view = temporalSceneView({});
    view.temporal.enabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::HistoryAge;
    renderFrame(**device, **renderer, temporalCamera(), view);
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(),
                                                    lmx::test::prepareSceneView(view, device)));
    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());
    bool sawRecycledSlot = false;
    for (const auto& transition : record->debug.transitions) {
        if (record->debug.resources[transition.resource].name == "lmx.render.historyColor0" &&
            transition.textureTo == TextureUse::CopyDestination) {
            sawRecycledSlot = true;
            CHECK(transition.textureFrom == TextureUse::ShaderRead);
        }
    }
    CHECK(sawRecycledSlot);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// Disabled diagnostic outputs retain a poison texel, including on reset frames. A bound fallback
// is still a real writable texture at (0,0); suppressing its graph declaration alone is not enough.
TEST_CASE("temporal resolve writes only enabled diagnostics", "[gpu][temporal][taa-diagnostics]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto library = (*device)->loadShaderLibrary("Shaders/TemporalResolve");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeTemporalResolve",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.diagnosticWritePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());
    auto sampler = (*device)->createSampler({.filter = FilterMode::Linear,
                                             .addressMode = AddressMode::Clamp,
                                             .label = "lmx.test.diagnosticSampler"});
    REQUIRE(sampler.has_value());
    constexpr std::array<float, 2> kExposure{1.0f, 1.0f};
    auto exposure = (*device)->createBuffer(
        {.size = sizeof(kExposure), .storageRead = true, .label = "lmx.test.diagnosticExposure"},
        kExposure.data());
    REQUIRE(exposure.has_value());

    const auto makeTexel = [&](const std::array<uint16_t, 4>& texel, const char* label) {
        const TextureMip mip{.data = texel.data(), .bytesPerRow = sizeof(texel)};
        return (*device)->createTexture({.width = 1,
                                         .height = 1,
                                         .format = Format::RGBA16Float,
                                         .sampled = true,
                                         .storageWrite = true,
                                         .cpuReadback = true,
                                         .label = label},
                                        std::span{&mip, 1});
    };
    auto input = makeTexel({0, 0, 0, 0x3c00}, "lmx.test.diagnosticInput");
    REQUIRE(input.has_value());
    // Mirrors the resolve ABI; the identity cameras and zero depth keep this one texel valid.
    struct ResolveParams {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t historyValid = 0;
        uint32_t writeDiagnostics = 0;
        glm::mat4 inverseViewProjection{1.0f};
        glm::mat4 previousViewProjection{1.0f};
        float previousNearZ = 0.1f;
        float pad[3]{};
    };
    static_assert(sizeof(ResolveParams) == 160);
    constexpr std::array<uint16_t, 4> kPoison{0x4200, 0x4200, 0x4200, 0x4200};
    for (const uint32_t valid : {0u, 1u}) {
        for (const uint32_t mask : {0u, 1u, 2u, 3u}) {
            INFO("history valid " << valid << ", diagnostic mask " << mask);
            auto output = makeTexel(kPoison, "lmx.test.diagnosticResolved");
            auto rejection = makeTexel(kPoison, "lmx.test.diagnosticRejection");
            auto reprojected = makeTexel(kPoison, "lmx.test.diagnosticReprojected");
            REQUIRE(output.has_value());
            REQUIRE(rejection.has_value());
            REQUIRE(reprojected.has_value());
            CommandList& commands = (*device)->beginFrame();
            commands.beginComputePass("lmx.test.diagnosticWrites");
            commands.bindComputePipeline(**pipeline);
            for (uint32_t slot = 0; slot < 6; ++slot) {
                commands.bindTexture(slot, **input);
            }
            commands.bindStorageTexture(6, **output, {}, StorageAccess::Write);
            commands.bindStorageTexture(7, **rejection, {}, StorageAccess::Write);
            commands.bindStorageTexture(8, **reprojected, {}, StorageAccess::Write);
            commands.bindStorageBuffer(0, **exposure, StorageAccess::Read);
            commands.bindFrameData(1,
                                   ResolveParams{.historyValid = valid, .writeDiagnostics = mask});
            commands.bindSampler(0, **sampler);
            commands.dispatch(1, 1, 1);
            commands.endComputePass();
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            std::array<uint16_t, 4> actual{};
            (*rejection)->readback(actual.data(), sizeof(actual));
            CHECK((actual != kPoison) == ((mask & 1u) != 0u));
            (*reprojected)->readback(actual.data(), sizeof(actual));
            CHECK((actual != kPoison) == ((mask & 2u) != 0u));
        }
    }
}

//======================================================================================================================
// Six frames of the history's own life: the first frame, a valid one, temporal switched off and
// back on, a resize, and an explicit cut. Every frame's reason is asserted, and the allocation
// survives the frame temporal spent switched off.
TEST_CASE("history reset reasons follow the frames that caused them", "[gpu][temporal]") {
    using namespace rojoRHI;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    // Disabling keeps the allocation rather than freeing it under frames still in flight.
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);

    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    (*device)->waitIdle();
    const auto resized = (*renderer)->resize(kSize / 2, kSize / 2);
    INFO((resized.has_value() ? std::string{} : resized.error().message));
    REQUIRE(resized.has_value());
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::ExtentChanged);
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes / 4);

    view.temporal.cameraCut = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::CameraCut);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    // A render-scale change is not an extent change: the history lives at the output extent and is
    // reprojected in UV, so it survives and the frame reports the change instead of resetting.
    view.temporal.cameraCut = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    const uint64_t beforeChange = (*renderer)->temporalStatus().lastRenderExtentChangeFrame;

    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, camera, view);
    const lmx::render::TemporalStatus scaled = (*renderer)->temporalStatus();
    REQUIRE(scaled.lastReset == HistoryResetReason::None);
    REQUIRE(scaled.historyValid);
    REQUIRE(scaled.upscaled);
    REQUIRE(scaled.extents.renderWidth == scaled.extents.outputWidth / 2);
    REQUIRE(scaled.lastRenderExtentChangeFrame > beforeChange);
    // Nothing was reallocated: the output extent is what the targets are sized by.
    REQUIRE(scaled.historyBytes == historyBytes / 4);

    // Holding the scale leaves the count where the change put it.
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastRenderExtentChangeFrame ==
            scaled.lastRenderExtentChangeFrame);
}

//======================================================================================================================
// The age's own bookkeeping across the two events that are not resets and the one that is. A frame
// with temporal off does not merely fail to advance the count -- it starts it over, because the
// history the next temporal frame finds is not the one the count described; a change of
// reconstruction mode does the opposite, because both modes leave a real frame in the colour slot.
// Status values only: what is asserted here is the counter, not the picture.
TEST_CASE("history age restarts across temporal off and survives a mode switch",
          "[gpu][temporal]") {
    using namespace rojoRHI;
    using lmx::render::HistoryResetReason;
    using lmx::render::ReconstructionMode;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();
    view.temporal.enabled = true;
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;

    // Three accumulated frames: the first is the reset that starts the count at 1.
    for (uint32_t frame = 1; frame <= 3; ++frame) {
        renderFrame(**device, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().historyAge == frame);
    }
    REQUIRE((*renderer)->temporalStatus().historyAge >= 3);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);

    // Temporal off: no history is kept, so nothing is counted.
    view.temporal.enabled = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().historyAge == 0);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    // Re-enabling is a reset, so the count starts over at the reset frame's own 1.
    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);
    REQUIRE((*renderer)->temporalStatus().historyAge == 1);

    // A change of reconstruction is not a reset: the raw commit leaves a real frame in the slot,
    // so the count keeps running through it and through the switch back.
    view.temporal.reconstruction = ReconstructionMode::Raw;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::Raw);
    REQUIRE((*renderer)->temporalStatus().historyAge == 2);

    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::NativeTaa);
    REQUIRE((*renderer)->temporalStatus().historyAge == 3);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);
}

//======================================================================================================================
// Enable with a debug view, spend a frame with temporal off, then re-enable. The off frame touches
// the motion target nowhere, so the re-enabling frame's import has to still name the read the last
// temporal frame ended with -- a fragment-stage barrier against its own attachment write would not
// drain that dispatch-stage read.
TEST_CASE("a re-enabling frame imports motion as the last temporal frame left it",
          "[gpu][temporal]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);

    view.temporal.enabled = false;
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(**device, **renderer, camera, view);

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display = (*renderer)->declarePasses(
        graph, commands, camera, lmx::test::prepareSceneView(view, device));
    graph.presentTexture(display);

    const auto record = graph.compileFrame(1);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    INFO(dump);
    REQUIRE(dump.contains("r4 texture \"lmx.render.motion\" RG16Float"));
    REQUIRE(dump.contains("texture r4 mips[0..] layers[0..] ShaderRead -> RenderTarget"));

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The shipped submission pattern rather than the drained one: five temporal frames with no
// waitIdle between them, so three of them are in flight over the motion and history targets at
// once while the debug view is switched on, off and on again. Under MTL_DEBUG_LAYER a frame that
// read a resource another frame had already retired, or a barrier the graph derived against the
// wrong previous use, is reported here rather than in a drained case that never overlaps.
TEST_CASE("temporal frames overlap in flight over one history", "[gpu][temporal]") {
    using namespace rojoRHI;
    using lmx::render::HistoryResetReason;
    using lmx::render::TemporalDebugView;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    // The debug view is what makes the frame *read* motion and the history, so the overlap covers
    // the reprojection dispatch and not just the attachment write.
    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::ReprojectionError;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    view.temporal.debugView = TemporalDebugView::Off;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::MotionVectors;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    // Five accumulated frames over the same two pairs, still undrained, with the debug view
    // switched on, off and on again: the resolve reads the other slot of both pairs while writing
    // this one, so the imports have to name what the frame two back actually left there rather than
    // what either mode alone would have.
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::RejectionMask : TemporalDebugView::Off;
        renderFrameInFlight(**device, **renderer, camera, view);
    }
    REQUIRE((*renderer)->temporalStatus().reconstruction ==
            lmx::render::ReconstructionMode::NativeTaa);

    // Five more, still undrained, alternating the render scale with the debug view: an upscaled
    // frame declares a different reconstruction pass and a different terminal use for the scene
    // colour than the frame before and after it, so the imports have to name what each of those
    // frames actually left rather than what one scale alone would have.
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.renderScale = frame % 2 == 0 ? 0.5f : 1.0f;
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::MotionVectors : TemporalDebugView::Off;
        renderFrameInFlight(**device, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    }
    REQUIRE((*renderer)->temporalStatus().upscaled);

    // The one drain, after every submission: the targets were never reallocated, so the run's own
    // completion is the assertion the overlap makes.
    (*device)->waitIdle();
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);
    REQUIRE((*renderer)->temporalStatus().depthHistoryBytes > 0);
}
