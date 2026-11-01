#include "Support/GpuTemporalTestSupport.h"

#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Passes/Temporal/TemporalRoots.h"
#include "Support/SceneTableTestSupport.h"
#include <array>
#include <cstring>

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

    auto gpu = createTestGpu();
    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    FixtureSceneView view = temporalSceneView({});
    view.temporal.enabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::HistoryAge;
    renderFrame(*gpu, **renderer, temporalCamera(), view);
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(*gpu, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(*gpu);
    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const auto display = (*renderer)->declarePasses(graph, commands, temporalCamera(),
                                                    lmx::test::prepareSceneView(view, *gpu));
    if (!display)
        gpu->submitAndEndFrame(commandsFrame);
    INFO(errorOf(display));
    REQUIRE(display.has_value());
    graph.presentTexture(*display);
    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());
    bool sawRecycledSlot = false;
    for (const auto& transition : record->debug.transitions) {
        if (record->debug.resources[transition.resource].name == "lmx.render.historyColor0" &&
            transition.textureTo == lmx::render::TextureUse::CopyDestination) {
            sawRecycledSlot = true;
            CHECK(transition.textureFrom == lmx::render::TextureUse::ShaderRead);
        }
    }
    CHECK(sawRecycledSlot);
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();
}

//======================================================================================================================
// Disabled diagnostic outputs retain a poison texel, including on reset frames. A bound fallback
// is still a real writable texture at (0,0); suppressing its graph declaration alone is not enough.
TEST_CASE("temporal resolve writes only enabled diagnostics", "[gpu][temporal][taa-diagnostics]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);
    auto library = gpu->loadShaderLibrary("TemporalResolve");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = lmx::engine::adoptGpu(gpu->device(),
                                          gpu->device().createPipeline(ComputePipelineDesc{
                                              .compute = {library->get(), "computeTemporalResolve"},
                                              .label = "lmx.test.diagnosticWritePipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());
    auto sampler = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createSampler({.filter = SamplerFilter::Linear,
                                                    .address = SamplerAddress::ClampToEdge,
                                                    .label = "lmx.test.diagnosticSampler"}));
    REQUIRE(sampler.has_value());
    constexpr std::array<float, 2> kExposure{1.0f, 1.0f};
    auto exposure = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = sizeof(kExposure), .label = "lmx.test.diagnosticExposure"},
        std::as_bytes(std::span{kExposure}));
    REQUIRE(exposure.has_value());

    const auto makeTexel = [&](const std::array<uint16_t, 4>& texel, const char* label) {
        const TextureData mip{.bytes = std::as_bytes(std::span{texel}),
                              .bytesPerRow = sizeof(texel)};
        return lmx::engine::GpuTexture::create(
            gpu->device(),
            {.format = Format::RGBA16Float,
             .extent = {1, 1, 1},
             .usage = TextureUsage::Sampled | TextureUsage::Storage,
             .label = label},
            std::span{&mip, 1}, {.sampled = true, .storageMips = true});
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
    // Each storage output leaves the dispatch as a compute-stage write in the storage layout,
    // which is the exact state its readback preserves.
    const std::array<lmx::render::TextureRangeState, 1> written{
        {{.range = {0, 1, 0, 1},
          .state = {Stage::ComputeShader, Access::ShaderWrite, Layout::ShaderWrite}}}};
    for (const uint32_t valid : {0u, 1u}) {
        for (const uint32_t mask : {0u, 1u, 2u, 3u}) {
            INFO("history valid " << valid << ", diagnostic mask " << mask);
            auto output = makeTexel(kPoison, "lmx.test.diagnosticResolved");
            auto rejection = makeTexel(kPoison, "lmx.test.diagnosticRejection");
            auto reprojected = makeTexel(kPoison, "lmx.test.diagnosticReprojected");
            REQUIRE(output.has_value());
            REQUIRE(rejection.has_value());
            REQUIRE(reprojected.has_value());
            auto commandsFrame = gpu->beginFrame();
            auto& commands = commandsFrame.commands;
            // Initial data leaves every output sampled; the kernel writes them as storage.
            std::array<TextureBarrier, 3> toStorage{};
            const std::array<const lmx::engine::GpuTexture*, 3> outputs{&*output, &*rejection,
                                                                        &*reprojected};
            for (size_t i = 0; i < outputs.size(); ++i) {
                toStorage[i] = {.texture = outputs[i]->rhi(),
                                .range = {0, 1, 0, 1},
                                .dstStage = Stage::ComputeShader,
                                .dstAccess = Access::ShaderWrite,
                                .oldLayout = Layout::ShaderRead,
                                .newLayout = Layout::ShaderWrite};
            }
            commands.barrier({}, {}, toStorage);
            commands.beginScope("lmx.test.diagnosticWrites");
            commands.setPipeline(pipeline->get());
            const auto inputView = static_cast<uint32_t>(input->view());
            commands.pushRoot(lmx::render::TemporalResolveRoot{
                .paramsAddr = lmx::render::frameData(
                    commands, ResolveParams{.historyValid = valid, .writeDiagnostics = mask}),
                .exposureAddr = exposure->address(),
                .sceneColor = inputView,
                .depth = inputView,
                .previousDepth = inputView,
                .motion = inputView,
                .reactive = inputView,
                .history = inputView,
                .output = static_cast<uint32_t>(output->storageView(0)),
                .rejection = static_cast<uint32_t>(rejection->storageView(0)),
                .reprojected = static_cast<uint32_t>(reprojected->storageView(0)),
                .sampler = static_cast<uint32_t>(sampler->get())});
#ifndef NDEBUG
            const std::array<ResourceAccess, 5> accesses{{{.texture = input->rhi(),
                                                           .range = {0, 1, 0, 1},
                                                           .stage = Stage::ComputeShader,
                                                           .access = Access::ShaderRead},
                                                          {.texture = output->rhi(),
                                                           .range = {0, 1, 0, 1},
                                                           .stage = Stage::ComputeShader,
                                                           .access = Access::ShaderWrite},
                                                          {.texture = rejection->rhi(),
                                                           .range = {0, 1, 0, 1},
                                                           .stage = Stage::ComputeShader,
                                                           .access = Access::ShaderWrite},
                                                          {.texture = reprojected->rhi(),
                                                           .range = {0, 1, 0, 1},
                                                           .stage = Stage::ComputeShader,
                                                           .access = Access::ShaderWrite},
                                                          {.buffer = exposure->rhi(),
                                                           .size = sizeof(kExposure),
                                                           .stage = Stage::ComputeShader,
                                                           .access = Access::ShaderRead}}};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(1, 1, 1);
            commands.endScope();
            gpu->submitAndEndFrame(commandsFrame);
            gpu->waitIdle();
            std::array<uint16_t, 4> actual{};
            {
                const auto readbackBytes = lmx::render::readbackNow(*gpu, *rejection, written);
                INFO(errorOf(readbackBytes));
                REQUIRE(readbackBytes.has_value());
                std::memcpy(actual.data(), readbackBytes->data(), sizeof(actual));
            }
            CHECK((actual != kPoison) == ((mask & 1u) != 0u));
            {
                const auto readbackBytes = lmx::render::readbackNow(*gpu, *reprojected, written);
                INFO(errorOf(readbackBytes));
                REQUIRE(readbackBytes.has_value());
                std::memcpy(actual.data(), readbackBytes->data(), sizeof(actual));
            }
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

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    // Disabling keeps the allocation rather than freeing it under frames still in flight.
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);

    view.temporal.enabled = true;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    gpu->waitIdle();
    const auto resized = (*renderer)->resize(kSize / 2, kSize / 2);
    INFO((resized.has_value() ? std::string{} : resized.error().message));
    REQUIRE(resized.has_value());
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::ExtentChanged);
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes / 4);

    view.temporal.cameraCut = true;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::CameraCut);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    // A render-scale change is not an extent change: the history lives at the output extent and is
    // reprojected in UV, so it survives and the frame reports the change instead of resetting.
    view.temporal.cameraCut = false;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    const uint64_t beforeChange = (*renderer)->temporalStatus().lastRenderExtentChangeFrame;

    view.temporal.renderScale = 0.5f;
    renderFrame(*gpu, **renderer, camera, view);
    const lmx::render::TemporalStatus scaled = (*renderer)->temporalStatus();
    REQUIRE(scaled.lastReset == HistoryResetReason::None);
    REQUIRE(scaled.historyValid);
    REQUIRE(scaled.upscaled);
    REQUIRE(scaled.extents.renderWidth == scaled.extents.outputWidth / 2);
    REQUIRE(scaled.lastRenderExtentChangeFrame > beforeChange);
    // Nothing was reallocated: the output extent is what the targets are sized by.
    REQUIRE(scaled.historyBytes == historyBytes / 4);

    // Holding the scale leaves the count where the change put it.
    renderFrame(*gpu, **renderer, camera, view);
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

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();
    view.temporal.enabled = true;
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;

    // Three accumulated frames: the first is the reset that starts the count at 1.
    for (uint32_t frame = 1; frame <= 3; ++frame) {
        renderFrame(*gpu, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().historyAge == frame);
    }
    REQUIRE((*renderer)->temporalStatus().historyAge >= 3);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);

    // Temporal off: no history is kept, so nothing is counted.
    view.temporal.enabled = false;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().historyAge == 0);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    // Re-enabling is a reset, so the count starts over at the reset frame's own 1.
    view.temporal.enabled = true;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);
    REQUIRE((*renderer)->temporalStatus().historyAge == 1);

    // A change of reconstruction is not a reset: the raw commit leaves a real frame in the slot,
    // so the count keeps running through it and through the switch back.
    view.temporal.reconstruction = ReconstructionMode::Raw;
    renderFrame(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::Raw);
    REQUIRE((*renderer)->temporalStatus().historyAge == 2);

    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    renderFrame(*gpu, **renderer, camera, view);
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

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    renderFrame(*gpu, **renderer, camera, view);
    renderFrame(*gpu, **renderer, camera, view);

    view.temporal.enabled = false;
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(*gpu, **renderer, camera, view);

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    lmx::render::TransientPool transients(*gpu);
    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const auto display = (*renderer)->declarePasses(graph, commands, camera,
                                                    lmx::test::prepareSceneView(view, *gpu));
    if (!display)
        gpu->submitAndEndFrame(commandsFrame);
    INFO(errorOf(display));
    REQUIRE(display.has_value());
    graph.presentTexture(*display);

    const auto record = graph.compileFrame(1);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    INFO(dump);
    REQUIRE(dump.contains("r4 texture \"lmx.render.motion\" RG16Float"));
    REQUIRE(dump.contains("texture r4 mips[0..] layers[0..] ShaderRead -> RenderTarget"));

    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();
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

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    // The debug view is what makes the frame *read* motion and the history, so the overlap covers
    // the reprojection dispatch and not just the attachment write.
    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::ReprojectionError;
    renderFrameInFlight(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrameInFlight(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    view.temporal.debugView = TemporalDebugView::Off;
    renderFrameInFlight(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::MotionVectors;
    renderFrameInFlight(*gpu, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    renderFrameInFlight(*gpu, **renderer, camera, view);
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
        renderFrameInFlight(*gpu, **renderer, camera, view);
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
        renderFrameInFlight(*gpu, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    }
    REQUIRE((*renderer)->temporalStatus().upscaled);

    // The one drain, after every submission: the targets were never reallocated, so the run's own
    // completion is the assertion the overlap makes.
    gpu->waitIdle();
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);
    REQUIRE((*renderer)->temporalStatus().depthHistoryBytes > 0);
}
