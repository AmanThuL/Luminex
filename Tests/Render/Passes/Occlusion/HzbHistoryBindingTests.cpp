#include "Engine/Scene/DrawItem.h"
#include "Render/Passes/Occlusion/HzbHistoryBinding.h"
#include "Render/Passes/Occlusion/HzbStage.h"
#include "Render/Renderer/SceneView.h"
#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include <string_view>

//======================================================================================================================
TEST_CASE("an abandoned HZB declaration cannot supply next-frame history",
          "[render][device][hzb-history]") {
    using namespace lmx;
    using namespace lmx::render;
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("history transaction fixture requires the configured Null backend");
    auto gpu = createTestGpu();
    auto stage = HzbStage::create(*gpu);
    REQUIRE(stage);
    TextureStateTable states;
    REQUIRE((*stage)->resize(8, 8, states));
    auto depth =
        engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::D32Float,
                                                   .extent = {8, 8, 1},
                                                   .usage = rojoRHI::TextureUsage::DepthStencil |
                                                            rojoRHI::TextureUsage::Sampled,
                                                   .label = "lmx.test.hzb.abandoned.depth"});
    REQUIRE(depth);
    auto first = gpu->beginFrame();
    {
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto source = graph.importTexture(depth->ref(), "lmx.test.depth");
        auto bundle = (*stage)->beginGraph(graph, first.commands);
        (*stage)->declare(*bundle, source, {.frameNumber = 0, .activeWidth = 8, .activeHeight = 8});
        REQUIRE((*stage)->previousSource().built);
        REQUIRE((*stage)->previousSource().frameNumber == 0);
    }
    gpu->submitAndEndFrame(first);
    auto second = gpu->beginFrame();
    OcclusionParams params;
    {
        RenderGraph graph;
        graph.bindTextureStates(states);
        auto bundle = (*stage)->beginGraph(graph, second.commands);
        auto previous = (*stage)->importPrevious(*bundle);
        params = previous->recordingParams();
    }
    gpu->submitAndEndFrame(second);
    REQUIRE((params.flags & 1u) != 0);
    REQUIRE((params.flags & 2u) == 0);
}

//======================================================================================================================
TEST_CASE("a real reduction prefix without publication cannot become HZB history",
          "[render][device][hzb-history][hzb-prefix]") {
    using namespace lmx;
    using namespace lmx::render;
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("history transaction fixture requires the configured Null backend");
    auto gpu = createTestGpu();
    auto stage = HzbStage::create(*gpu);
    REQUIRE(stage);
    TextureStateTable states;
    REQUIRE((*stage)->resize(8, 8, states));
    auto depth =
        engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::D32Float,
                                                   .extent = {8, 8, 1},
                                                   .usage = rojoRHI::TextureUsage::DepthStencil |
                                                            rojoRHI::TextureUsage::Sampled,
                                                   .label = "lmx.test.hzb.prefix.depth"});
    REQUIRE(depth);
    auto first = gpu->beginFrame();
    uint32_t callbacks = 0;
    {
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto source = graph.importTexture(depth->ref(), "lmx.test.depth");
        auto bundle = (*stage)->beginGraph(graph, first.commands);
        (*stage)->declare(*bundle, source, {.frameNumber = 0, .activeWidth = 8, .activeHeight = 8});
        const auto recorded = HzbGraphTestAccess::recordThrough(graph, first.commands, first.number,
                                                                "lmx.pass.hzb.level0");
        if (!recorded)
            gpu->submitAndEndFrame(first);
        INFO((recorded ? "" : recorded.error().message));
        REQUIRE(recorded);
        callbacks = *recorded;
    }
    gpu->submitAndEndFrame(first);
    gpu->waitIdle();
    auto second = gpu->beginFrame();
    OcclusionParams params;
    {
        RenderGraph graph;
        graph.bindTextureStates(states);
        auto bundle = (*stage)->beginGraph(graph, second.commands);
        params = (*stage)->importPrevious(*bundle)->recordingParams();
    }
    gpu->submitAndEndFrame(second);
    REQUIRE(callbacks == 1);
    REQUIRE((params.flags & 2u) == 0);
}

#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Passes/Occlusion/OcclusionRoots.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"
#include "Render/Passes/Visibility/VisibilityRoots.h"
#include "Support/VisibilityOracleRoots.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace lmx::render {
struct HzbMatrixTestAccess {
    //==================================================================================================================
    static void attach(GpuVisibility& owner, void* context,
                       GpuVisibility::RecordingObserver observer) {
        owner.m_recordingObserverContext = context;
        owner.m_recordingObserver = observer;
    }
};
} // namespace lmx::render

namespace {
using namespace lmx;
using namespace lmx::render;

struct HzbRecordedArguments {
    uint64_t frame = 0;
    std::vector<std::byte> params, root;
    rojoRHI::Addr paramsAddress{}, rootAddress{};
    rojoRHI::ViewIndex view{};
    bool accessDeclared = false;
    std::vector<rojoRHI::ResourceAccess> accesses;
};

//======================================================================================================================
void retainHzbArguments(void* context, uint64_t frame, std::span<const std::byte> params,
                        rojoRHI::Addr paramsAddress, std::span<const std::byte> root,
                        rojoRHI::Addr rootAddress, rojoRHI::ViewIndex view, bool accessDeclared,
                        std::span<const rojoRHI::ResourceAccess> accesses) {
    auto& output = *static_cast<std::vector<HzbRecordedArguments>*>(context);
    output.push_back({frame,
                      {params.begin(), params.end()},
                      {root.begin(), root.end()},
                      paramsAddress,
                      rootAddress,
                      view,
                      accessDeclared,
                      {accesses.begin(), accesses.end()}});
}

struct HzbMatrixFrame {
    engine::GpuContext& gpu;
    engine::Frame frame;
    bool closed = false;
    //==================================================================================================================
    explicit HzbMatrixFrame(engine::GpuContext& owner) : gpu(owner), frame(owner.beginFrame()) {}
    //==================================================================================================================
    void close() {
        if (!closed) {
            gpu.submitAndEndFrame(frame);
            closed = true;
        }
    }
    //==================================================================================================================
    ~HzbMatrixFrame() { close(); }
};

//======================================================================================================================
engine::GpuBuffer hzbMatrixBuffer(engine::GpuContext& gpu, uint64_t bytes,
                                  rojoRHI::BufferUsage usage,
                                  std::span<const std::byte> initial = {}) {
    auto result = engine::GpuBuffer::create(
        gpu.device(), {.size = bytes, .usage = usage, .label = "lmx.test.hzbMatrix.buffer"},
        initial);
    REQUIRE(result);
    return std::move(*result);
}

struct HzbMatrixFixture {
    std::unique_ptr<engine::GpuContext> gpu = createTestGpu();
    std::unique_ptr<HzbStage> stage;
    std::unique_ptr<GpuVisibility> visibility;
    TextureStateTable states;
    TransientPool pool{*gpu};
    engine::GpuTexture depth;
    engine::GpuBuffer instances, meshes, rows, arguments;
    engine::GpuOwned<rojoRHI::ShaderLibrary> probeLibrary, publishLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> probe, publish;
    std::array<engine::InstanceRow, 1> instanceRows{};
    std::array<engine::DrawItem, 1> items{};
    SceneView view;
    FrustumPlanes planes = extractFrustumPlanes(glm::mat4(1));
    PreparedSubmission submission;
    std::vector<HzbRecordedArguments> recorded;
    //==================================================================================================================
    HzbMatrixFixture() {
        auto createdStage = HzbStage::create(*gpu);
        REQUIRE(createdStage);
        stage = std::move(*createdStage);
        REQUIRE(stage->resize(8, 8, states));
        auto createdVisibility = GpuVisibility::create(*gpu);
        REQUIRE(createdVisibility);
        visibility = std::move(*createdVisibility);
        auto createdDepth = engine::GpuTexture::create(
            gpu->device(),
            {.format = rojoRHI::Format::D32Float,
             .extent = {8, 8, 1},
             .usage = rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled,
             .label = "lmx.test.hzbMatrix.depth"});
        REQUIRE(createdDepth);
        depth = std::move(*createdDepth);
        instanceRows[0].worldBoundsMin = glm::vec3(0, 0, 0.25f);
        instanceRows[0].worldBoundsMax = instanceRows[0].worldBoundsMin;
        const std::array<engine::MeshRow, 1> meshRows{};
        instances = hzbMatrixBuffer(*gpu, sizeof(instanceRows), rojoRHI::BufferUsage::None,
                                    std::as_bytes(std::span(instanceRows)));
        meshes = hzbMatrixBuffer(*gpu, sizeof(meshRows), rojoRHI::BufferUsage::None,
                                 std::as_bytes(std::span(meshRows)));
        rows = hzbMatrixBuffer(*gpu, 512, rojoRHI::BufferUsage::ShaderWrite);
        arguments = hzbMatrixBuffer(
            *gpu, 512, rojoRHI::BufferUsage::ShaderWrite | rojoRHI::BufferUsage::IndirectArgs);
        items[0].instanceRow = 0;
        items[0].instanceIdentity = 1;
        view.items = items;
        view.tables.instances = &instances;
        view.tables.meshes = &meshes;
        view.tables.instanceRows = instanceRows;
        view.tables.instanceCount = view.tables.instanceCapacity = view.tables.meshCount = 1;
        view.classifyMode = ClassifyMode::Gpu;
        view.submission = SubmissionMode::Indirect;
        view.occlusionEnabled = true;
        auto pl = gpu->loadShaderLibrary("OcclusionProbe");
        auto hl = gpu->loadShaderLibrary("HzbPublish");
        REQUIRE(pl);
        REQUIRE(hl);
        probeLibrary = std::move(*pl);
        publishLibrary = std::move(*hl);
        auto pp = engine::adoptGpu(gpu->device(),
                                   gpu->device().createPipeline(rojoRHI::ComputePipelineDesc{
                                       .compute = {probeLibrary.get(), "computeMain"},
                                       .label = "lmx.test.hzbMatrix.probe"}));
        auto hp = engine::adoptGpu(gpu->device(),
                                   gpu->device().createPipeline(rojoRHI::ComputePipelineDesc{
                                       .compute = {publishLibrary.get(), "computeMain"},
                                       .label = "lmx.test.hzbMatrix.publish"}));
        REQUIRE(pp);
        REQUIRE(hp);
        probe = std::move(*pp);
        publish = std::move(*hp);
        HzbMatrixTestAccess::attach(*visibility, &recorded, retainHzbArguments);
    }
    //==================================================================================================================
    ~HzbMatrixFixture() {
        HzbMatrixTestAccess::attach(*visibility, nullptr, nullptr);
        gpu->waitIdle();
    }
    //==================================================================================================================
    GraphTexture clear(RenderGraph& graph, GraphTexture input, float value,
                       std::string_view label) {
        PassDesc pass;
        pass.depth = DepthAttachment{
            .handle = input, .load = LoadOp::Clear, .store = StoreOp::Store, .clearDepth = value};
        graph.addPass(label, std::move(pass), [](const PassResources&) {});
        return nextVersion(input);
    }
    //==================================================================================================================
    void prime(HzbMatrixFrame& frame, bool partial = false) {
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto input = graph.importPersistentTexture(depth, "lmx.test.hzbMatrix.depth");
        const auto source = clear(graph, input, 1, "lmx.test.hzbMatrix.primeClear");
        stage->declare(graph, frame.frame.commands, source,
                       {.frameNumber = 0, .activeWidth = 8, .activeHeight = 8});
        if (partial) {
            const auto prefix = HzbGraphTestAccess::recordThrough(
                graph, frame.frame.commands, frame.frame.number, "lmx.pass.hzb.level0");
            REQUIRE(prefix);
            REQUIRE(*prefix == 2);
        } else {
            graph.execute(frame.frame.commands, frame.frame.number);
        }
    }
    //==================================================================================================================
    GpuVisibilityOutputs classify(RenderGraph& graph, HzbMatrixFrame& frame,
                                  const std::shared_ptr<HzbHistoryBinding>& history, bool camera,
                                  bool shadow, bool zeroCapacity) {
        VisibilityResult cameraRows, shadowRows;
        if (camera)
            cameraRows.visibleItems.push_back(0);
        if (shadow)
            shadowRows.visibleItems.push_back(0);
        submission = buildDrawSubmission(view, cameraRows, shadowRows, view.submission);
        submission.scene.rows = submission.shadow.rows = &rows;
        submission.scene.arguments = submission.shadow.arguments = &arguments;
        const auto tables = buildVisibilityTables(view, submission, planes);
        REQUIRE(tables.views[0].candidateCount == (camera ? 1 : 0));
        REQUIRE(tables.views[1].candidateCount == (shadow ? 1 : 0));
        if (zeroCapacity)
            visibility->setCapacityOverride(std::array<uint32_t, 3>{0, 0, 0});
        VisibilityStatus status;
        status.frameNumber = frame.frame.number;
        status.sceneGeneration = 7;
        status.classifyMode = ClassifyMode::Gpu;
        status.occlusionEnabled = true;
        status.occlusionInvalidReason = OcclusionInvalidReason::None;
        status.scene.candidates.push_back(
            {.instanceIdentity = 1,
             .instanceRow = 0,
             .worldBounds = {instanceRows[0].worldBoundsMin, instanceRows[0].worldBoundsMax}});
        status.shadow.candidates = status.scene.candidates;
        status.occlusionAssociation =
            createOcclusionDeclarationAssociation(*gpu, frame.frame.number, 7);
        auto result = visibility->declare(
            graph, frame.frame.commands, view,
            {.planes = planes,
             .submission = submission,
             .instances = graph.importBuffer(instances, "lmx.test.hzbMatrix.instances"),
             .meshes = graph.importBuffer(meshes, "lmx.test.hzbMatrix.meshes"),
             .rows = graph.importBuffer(rows, "lmx.test.hzbMatrix.rows"),
             .arguments = graph.importBuffer(arguments, "lmx.test.hzbMatrix.arguments"),
             .history = history,
             .occlusion =
                 makeOcclusionParams(glm::mat4(1), 8, 8, stage->layout().levelCount, true, true)},
            status);
        REQUIRE(result);
        return std::move(*result);
    }
};

struct HzbMatrixProbe {
    engine::GpuBuffer bounds, output, publication;
    GraphBuffer boundsHandle, outputHandle, publicationHandle;
    OcclusionParams params;
    rojoRHI::ViewIndex view{};
    std::optional<ReadbackTicket> ticket;
    uint32_t calls = 0;
};

//======================================================================================================================
std::shared_ptr<HzbMatrixProbe> hzbMatrixProbe(HzbMatrixFixture& fixture, RenderGraph& graph) {
    auto result = std::make_shared<HzbMatrixProbe>();
    const std::array<test::HzbProbeBounds, 1> bounds{
        {{glm::vec4(0, 0, 0.25f, 0), glm::vec4(0, 0, 0.25f, 0)}}};
    result->bounds = hzbMatrixBuffer(*fixture.gpu, 512, rojoRHI::BufferUsage::None,
                                     std::as_bytes(std::span(bounds)));
    result->output = hzbMatrixBuffer(*fixture.gpu, 512, rojoRHI::BufferUsage::ShaderWrite);
    result->publication = hzbMatrixBuffer(*fixture.gpu, 4, rojoRHI::BufferUsage::ShaderWrite);
    result->boundsHandle = graph.importBuffer(result->bounds, "lmx.test.hzbMatrix.bounds");
    result->outputHandle = graph.importBuffer(result->output, "lmx.test.hzbMatrix.output");
    result->publicationHandle =
        graph.importBuffer(result->publication, "lmx.test.hzbMatrix.publication");
    return result;
}

//======================================================================================================================
void appendHzbProbe(HzbMatrixFixture& fixture, RenderGraph& graph, HzbMatrixFrame& frame,
                    const std::shared_ptr<HzbHistoryBinding>& history,
                    const std::shared_ptr<HzbMatrixProbe>& probe, GraphTexture canonical,
                    std::string_view label, bool live, bool normalRead = false) {
    ComputePassDesc desc;
    desc.shaderBufferReads.push_back(probe->boundsHandle);
    desc.bufferWrites.push_back(probe->outputHandle);
    if (normalRead) {
        desc.shaderTextureReads.push_back(canonical);
        desc.bufferWrites.push_back(probe->publicationHandle);
    }
    history->appendRead(
        graph, label, std::move(desc),
        [&, history, probe, normalRead, canonical](const PassResources& resources) {
            auto& commands = frame.frame.commands;
            probe->params = history->recordingParams();
            probe->view = history->sampledView(resources);
            ++probe->calls;
            commands.setPipeline(fixture.probe.get());
            commands.pushRoot(test::OcclusionProbeRoot{
                resources.address(probe->boundsHandle), resources.address(probe->outputHandle),
                frameData(commands, probe->params), static_cast<uint32_t>(probe->view), 0});
#ifndef NDEBUG
            std::vector<rojoRHI::ResourceAccess> accesses{
                {.buffer = resources.buffer(probe->boundsHandle),
                 .size = 32,
                 .stage = rojoRHI::Stage::ComputeShader,
                 .access = rojoRHI::Access::ShaderRead},
                {.buffer = resources.buffer(probe->outputHandle),
                 .size = 32,
                 .stage = rojoRHI::Stage::ComputeShader,
                 .access = rojoRHI::Access::ShaderWrite}};
            if ((probe->params.flags & 3u) == 3u)
                accesses.push_back({.texture = resources.texture(canonical).texture,
                                    .stage = rojoRHI::Stage::ComputeShader,
                                    .access = rojoRHI::Access::ShaderRead});
            commands.declareAccess(accesses);
#endif
            commands.dispatch(1, 1, 1);
            if (normalRead) {
                commands.setPipeline(fixture.publish.get());
                commands.pushRoot(HzbPublishRoot{resources.address(probe->publicationHandle),
                                                 static_cast<uint32_t>(probe->view),
                                                 fixture.stage->layout().levelCount});
#ifndef NDEBUG
                const std::array<rojoRHI::ResourceAccess, 2> normal{
                    {{.texture = resources.texture(canonical).texture,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderRead},
                     {.buffer = resources.buffer(probe->publicationHandle),
                      .size = 4,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderWrite}}};
                commands.declareAccess(normal);
#endif
                commands.dispatch(1, 1, 1);
            }
            probe->ticket =
                recordReadback(*fixture.gpu, commands, probe->output,
                               {rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                                rojoRHI::Layout::Undefined},
                               {0, 32});
        },
        true);
    if (live) {
        graph.readbackBuffer(nextVersion(probe->outputHandle));
        if (normalRead)
            graph.readbackBuffer(nextVersion(probe->publicationHandle));
    }
}

//======================================================================================================================
GraphTexture hzbMatrixCanonical(const RenderGraph& graph, rojoRHI::Texture texture,
                                uint64_t frameId) {
    const auto frame = lowered(graph, frameId);
    for (uint32_t index = 0; index < frame.resources.size(); ++index)
        if (frame.resources[index].texture.texture == texture)
            return {index, 0};
    FAIL("actual canonical history allocation is missing");
    return {};
}

//======================================================================================================================
GraphTexture copyHzbOrderToDepth(RenderGraph& graph, HzbMatrixFrame& frame, GraphBuffer source,
                                 GraphTexture depth, std::string_view label) {
    CopyPassDesc desc;
    desc.bufferSources.push_back(source);
    desc.textureDestinations.push_back(depth);
    graph.addCopyPass(label, std::move(desc), [&, source, depth](const PassResources& resources) {
        frame.frame.commands.copyBufferToTexture(resources.buffer(source), {.rowPitch = 256},
                                                 resources.texture(depth).texture,
                                                 {.extent = {1, 1, 1}});
    });
    return nextVersion(depth);
}

//======================================================================================================================
void hzbLateBounds(RenderGraph& graph, HzbMatrixFrame& frame, GraphTexture written,
                   const std::shared_ptr<HzbMatrixProbe>& probe) {
    CopyPassDesc desc;
    desc.textureSources.push_back(written);
    desc.bufferDestinations.push_back(probe->boundsHandle);
    const auto destination = probe->boundsHandle;
    graph.addCopyPass("lmx.test.hzbMatrix.aliasToBounds", std::move(desc),
                      [&, written, destination](const PassResources& resources) {
                          frame.frame.commands.copyTextureToBuffer(
                              resources.texture(written).texture, {.extent = {1, 1, 1}},
                              resources.buffer(destination), {.rowPitch = 256});
                      });
    probe->boundsHandle = nextVersion(destination);
}

struct HzbCurrentReadback {
    engine::GpuBuffer pixels, publication;
    std::optional<ReadbackTicket> pixelsTicket, publicationTicket;
    uint32_t width = 0, height = 0;
};

//======================================================================================================================
std::shared_ptr<HzbCurrentReadback> retainCurrentHzb(HzbMatrixFixture& fixture, RenderGraph& graph,
                                                     HzbMatrixFrame& frame, GraphTexture written) {
    const auto observed = lowered(graph, frame.frame.number);
    std::optional<GraphBuffer> publication;
    for (const auto& pass : observed.record.debug.passes)
        for (const auto& use : pass.uses)
            if (use.role == UseRole::Write &&
                observed.record.debug.resources[use.resource].name == "lmx.render.hzbPublish")
                publication = nextVersion(GraphBuffer{use.resource, use.version});
    REQUIRE(publication);
    auto result = std::make_shared<HzbCurrentReadback>();
    result->width = fixture.stage->layout().width;
    result->height = fixture.stage->layout().height;
    auto pixels = engine::GpuBuffer::create(fixture.gpu->device(),
                                            {.size = uint64_t{256} * result->height,
                                             .memory = rojoRHI::Memory::Readback,
                                             .label = "lmx.test.hzbMatrix.currentPixels"});
    auto sum = engine::GpuBuffer::create(fixture.gpu->device(),
                                         {.size = 4,
                                          .memory = rojoRHI::Memory::Readback,
                                          .label = "lmx.test.hzbMatrix.currentPublication"});
    REQUIRE(pixels);
    REQUIRE(sum);
    result->pixels = std::move(*pixels);
    result->publication = std::move(*sum);
    const auto destination = graph.importBuffer(result->pixels, "lmx.test.hzbMatrix.currentPixels");
    const auto sumDestination =
        graph.importBuffer(result->publication, "lmx.test.hzbMatrix.currentPublication");
    CopyPassDesc desc;
    desc.textureSources.push_back(written);
    desc.bufferSources.push_back(*publication);
    desc.bufferDestinations = {destination, sumDestination};
    graph.addCopyPass(
        "lmx.test.hzbMatrix.retainCurrent", std::move(desc),
        [&, result, written, source = *publication, destination,
         sumDestination](const PassResources& resources) {
            frame.frame.commands.copyTextureToBuffer(
                resources.texture(written).texture, {.extent = {result->width, result->height, 1}},
                resources.buffer(destination), {.rowPitch = 256});
            frame.frame.commands.copyBuffer(resources.buffer(source), 0,
                                            resources.buffer(sumDestination), 0, 4);
            const SyncPoint copied{rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite,
                                   rojoRHI::Layout::Undefined};
            result->pixelsTicket =
                recordReadback(*fixture.gpu, frame.frame.commands, result->pixels, copied,
                               wholeBuffer(result->pixels));
            result->publicationTicket = recordReadback(*fixture.gpu, frame.frame.commands,
                                                       result->publication, copied, {0, 4});
        });
    graph.readbackBuffer(nextVersion(destination));
    graph.readbackBuffer(nextVersion(sumDestination));
    return result;
}

//======================================================================================================================
void checkHzbArguments(const HzbRecordedArguments& observed, const VisibilityStatus& status,
                       rojoRHI::Texture history, bool sampled) {
    static_assert(sizeof(OcclusionParams) == 96);
    REQUIRE(observed.params.size() == sizeof(OcclusionParams));
    REQUIRE(observed.root.size() == sizeof(VisibilityOcclusionRoot));
    CHECK(std::memcmp(observed.params.data(), &status.occlusionParams, sizeof(OcclusionParams)) ==
          0);
    VisibilityOcclusionRoot root;
    std::memcpy(&root, observed.root.data(), sizeof(root));
    CHECK(root.occlusionAddr.lo == observed.paramsAddress.lo);
    CHECK(root.occlusionAddr.hi == observed.paramsAddress.hi);
    CHECK(root.pyramid == static_cast<uint32_t>(observed.view));
    CHECK(root.padding == 0);
    CHECK((observed.paramsAddress.lo != 0 || observed.paramsAddress.hi != 0));
    CHECK((observed.rootAddress.lo != 0 || observed.rootAddress.hi != 0));
#ifndef NDEBUG
    REQUIRE(observed.accessDeclared);
    const auto reads = std::ranges::count_if(observed.accesses, [&](const auto& access) {
        return access.texture == history && access.stage == rojoRHI::Stage::ComputeShader &&
               access.access == rojoRHI::Access::ShaderRead;
    });
    CHECK(reads == (sampled ? 1 : 0));
#else
    CHECK_FALSE(observed.accessDeclared);
    CHECK(observed.accesses.empty());
    (void)history;
    (void)sampled;
#endif
}

enum class HzbMatrixCase {
    Early,
    Late,
    Appended,
    Culled,
    Initial,
    SameFrame,
    ZeroCamera,
    ShadowOnly,
    ZeroCapacity,
    NormalRead,
    ExplicitTerminal,
    Partial
};

//======================================================================================================================
void runHzbMatrix(HzbMatrixCase kind) {
    HzbMatrixFixture fixture;
    const bool initial = kind == HzbMatrixCase::Initial || kind == HzbMatrixCase::NormalRead ||
                         kind == HzbMatrixCase::ExplicitTerminal;
    const bool sameFrame = kind == HzbMatrixCase::SameFrame;
    if (!initial && !sameFrame) {
        HzbMatrixFrame prime(*fixture.gpu);
        fixture.prime(prime, kind == HzbMatrixCase::Partial);
        prime.close();
        fixture.gpu->waitIdle();
    }
    HzbMatrixFrame frame(*fixture.gpu);
    if (sameFrame)
        fixture.prime(frame);
    fixture.pool.beginFrame();
    RenderGraph graph(fixture.pool);
    graph.bindTextureStates(fixture.states);
    auto bundle = fixture.stage->beginGraph(graph, frame.frame.commands);
    auto history = fixture.stage->importPrevious(*bundle);
    const auto historyTexture = fixture.stage->previousTexture();
    const auto canonical = hzbMatrixCanonical(graph, historyTexture, frame.frame.number);
    const auto before = fixture.states.find(historyTexture, {0, 1, 0, 1});
    const bool camera = kind != HzbMatrixCase::ZeroCamera && kind != HzbMatrixCase::ShadowOnly;
    const bool shadow = kind == HzbMatrixCase::ShadowOnly;
    auto outputs = fixture.classify(graph, frame, history, camera, shadow,
                                    kind == HzbMatrixCase::ZeroCapacity);
    auto early = hzbMatrixProbe(fixture, graph);
    const bool onlyClassifier = !camera || kind == HzbMatrixCase::ZeroCapacity;
    if (!onlyClassifier)
        appendHzbProbe(fixture, graph, frame, history, early, canonical, "lmx.test.hzbMatrix.early",
                       true, kind == HzbMatrixCase::NormalRead);
    if (kind == HzbMatrixCase::ExplicitTerminal)
        graph.setFinalUse(canonical, TextureUse::ShaderRead);
    const bool overwrite = kind == HzbMatrixCase::Early || kind == HzbMatrixCase::Late ||
                           kind == HzbMatrixCase::Appended || kind == HzbMatrixCase::Culled;
    std::shared_ptr<HzbMatrixProbe> late;
    GraphTexture written{};
    if (overwrite) {
        auto depth = graph.importPersistentTexture(fixture.depth, "lmx.test.hzbMatrix.depth");
        depth = copyHzbOrderToDepth(graph, frame, outputs.arguments, depth,
                                    "lmx.test.hzbMatrix.classifierToDepth");
        depth = copyHzbOrderToDepth(graph, frame, nextVersion(early->outputHandle), depth,
                                    "lmx.test.hzbMatrix.probeToDepth");
        depth = fixture.clear(graph, depth, 0, "lmx.test.hzbMatrix.clearCurrent");
        fixture.stage->declare(*bundle, depth,
                               {.frameNumber = 17, .activeWidth = 8, .activeHeight = 8});
        written = fixture.stage->declare(*bundle, depth,
                                         {.frameNumber = 18, .activeWidth = 8, .activeHeight = 8});
        REQUIRE(written.index == canonical.index);
        REQUIRE(written.version > canonical.version);
        if (kind != HzbMatrixCase::Early) {
            if (kind == HzbMatrixCase::Appended) {
                const auto earlierPlan = lowered(graph, frame.frame.number);
                CHECK(std::ranges::none_of(earlierPlan.record.debug.passes, [](const auto& pass) {
                    return pass.label == "lmx.test.hzbMatrix.late";
                }));
            }
            late = hzbMatrixProbe(fixture, graph);
            hzbLateBounds(graph, frame, written, late);
            appendHzbProbe(fixture, graph, frame, history, late, canonical,
                           "lmx.test.hzbMatrix.late", kind != HzbMatrixCase::Culled);
        }
    }
    const auto current = overwrite ? retainCurrentHzb(fixture, graph, frame, written) : nullptr;
    const auto plan = lowered(graph, frame.frame.number);
    const auto liveLabel = [&](std::string_view label) {
        return std::ranges::any_of(plan.record.debug.schedule.passes, [&](auto pass) {
            return plan.record.debug.passes[pass].label == label;
        });
    };
    CHECK(liveLabel("lmx.pass.visibility.classify"));
    const auto& scheduled = plan.record.debug.schedule.passes;
    size_t classifierPosition = scheduled.size(), earlyPosition = scheduled.size();
    size_t overwritePosition = scheduled.size(), latePosition = scheduled.size();
    for (size_t position = 0; position < scheduled.size(); ++position) {
        const auto& pass = plan.record.debug.passes[scheduled[position]];
        if (pass.label == "lmx.pass.visibility.classify") {
            classifierPosition = position;
            CHECK(std::ranges::any_of(pass.uses, [&](const auto& use) {
                return use.resource == canonical.index && use.role == UseRole::ShaderRead;
            }));
        }
        if (pass.label == "lmx.test.hzbMatrix.early") {
            earlyPosition = position;
            CHECK(std::ranges::count_if(pass.uses, [&](const auto& use) {
                      return use.resource == canonical.index && use.version == canonical.version &&
                             use.role == UseRole::ShaderRead;
                  }) == (kind == HzbMatrixCase::NormalRead ? 2 : 1));
        }
        if (pass.label == "lmx.test.hzbMatrix.late")
            latePosition = position;
        for (const auto& use : pass.uses)
            if (use.resource == canonical.index && use.role == UseRole::Write)
                overwritePosition = std::min(overwritePosition, position);
    }
    if (overwrite) {
        REQUIRE(overwritePosition < scheduled.size());
        CHECK(classifierPosition < overwritePosition);
        CHECK(earlyPosition < overwritePosition);
        if (late && kind != HzbMatrixCase::Culled) {
            REQUIRE(latePosition < scheduled.size());
            CHECK(latePosition > overwritePosition);
        }
        CHECK(std::ranges::count_if(plan.record.debug.resources, [](const auto& resource) {
                  return resource.name == "lmx.render.hzb0" || resource.name == "lmx.render.hzb1";
              }) == 2);
    }
    if (late)
        CHECK(liveLabel("lmx.test.hzbMatrix.late") == (kind != HzbMatrixCase::Culled));
    const bool valid = !initial && !sameFrame && kind != HzbMatrixCase::Partial &&
                       kind != HzbMatrixCase::Late && kind != HzbMatrixCase::Appended;
    const bool actualHistory = (valid && camera) || kind == HzbMatrixCase::NormalRead ||
                               kind == HzbMatrixCase::ExplicitTerminal || overwrite;
    const auto touched = std::ranges::any_of(
        plan.physicalStates, [&](const auto& state) { return state.resource == canonical.index; });
    CHECK(touched == actualHistory);
    if (initial) {
        const auto barriers = textureBarriers(plan);
        const auto shaderReads = std::ranges::count_if(barriers, [&](const auto& barrier) {
            return barrier.resource == canonical.index &&
                   barrier.to.layout == rojoRHI::Layout::ShaderRead;
        });
        CHECK((shaderReads != 0) ==
              (kind == HzbMatrixCase::NormalRead || kind == HzbMatrixCase::ExplicitTerminal));
    }
    CHECK(std::ranges::count_if(plan.resources, [&](const auto& resource) {
              return resource.texture.texture == historyTexture;
          }) == 1);
    graph.execute(frame.frame.commands, frame.frame.number);
    frame.close();
    fixture.gpu->waitIdle();
    REQUIRE(fixture.recorded.size() == 1);
    const auto& status = outputs.projection->status;
    CHECK(((status.occlusionParams.flags & 2u) != 0) == valid);
    checkHzbArguments(fixture.recorded[0], status, historyTexture, valid && camera);
    CHECK(fixture.recorded[0].frame == frame.frame.number);
    if (!onlyClassifier) {
        REQUIRE(early->calls == 1);
        CHECK(std::memcmp(&early->params, &status.occlusionParams, sizeof(OcclusionParams)) == 0);
        CHECK(static_cast<uint32_t>(early->view) ==
              static_cast<uint32_t>(fixture.recorded[0].view));
    }
    if (late && kind != HzbMatrixCase::Culled) {
        REQUIRE(late->calls == 1);
        CHECK(std::memcmp(&late->params, &early->params, sizeof(OcclusionParams)) == 0);
        CHECK(static_cast<uint32_t>(late->view) == static_cast<uint32_t>(early->view));
    }
    if (!onlyClassifier)
        REQUIRE(early->ticket);
    if (late && kind == HzbMatrixCase::Culled) {
        CHECK(late->calls == 0);
        CHECK_FALSE(late->ticket);
    }
    if (late && kind != HzbMatrixCase::Culled)
        REQUIRE(late->ticket);
    if (kind == HzbMatrixCase::ZeroCapacity) {
        const auto tickets = outputs.readbackIntent.lock();
        REQUIRE(tickets);
        REQUIRE(tickets->states);
        CHECK(tickets->states->region.size == 0);
        CHECK((status.occlusionParams.flags & 3u) == 3u);
    }
    if (kind == HzbMatrixCase::Partial) {
        REQUIRE(before);
        REQUIRE(before->access == rojoRHI::Access::ShaderWrite);
        REQUIRE(before->layout == rojoRHI::Layout::ShaderWrite);
        const auto after = fixture.states.find(historyTexture, {0, 1, 0, 1});
        REQUIRE(after);
        CHECK(after->stage == before->stage);
        CHECK(after->access == before->access);
        CHECK(after->layout == before->layout);
    }
    // Null records real arguments and synchronization; it does not execute these kernels.
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib" && current) {
        REQUIRE(current->pixelsTicket);
        REQUIRE(current->publicationTicket);
        std::vector<uint8_t> bytes(current->pixels.desc().size);
        REQUIRE(resolve(*fixture.gpu, *current->pixelsTicket, bytes));
        for (uint32_t y = 0; y < current->height; ++y)
            for (uint32_t x = 0; x < current->width; ++x) {
                float value = 1;
                std::memcpy(&value, bytes.data() + uint64_t{y} * 256 + uint64_t{x} * 4, 4);
                CHECK(value == 0);
            }
        std::array<float, 1> publication{1};
        REQUIRE(resolve(*fixture.gpu, *current->publicationTicket,
                        resolveBytes(std::span(publication))));
        CHECK(publication[0] == 0);
    }
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib" && !onlyClassifier) {
        const auto checkProbe = [&](const std::shared_ptr<HzbMatrixProbe>& item) {
            REQUIRE(item->ticket);
            std::array<uint32_t, 8> words;
            REQUIRE(resolve(*fixture.gpu, *item->ticket, resolveBytes(std::span(words))));
            if (!valid) {
                CHECK(words[6] == static_cast<uint32_t>(OcclusionOutcome::HistoryInvalid));
                CHECK(words[7] == 0);
            } else {
                // A cleared depth of one reduces to one; the point lies strictly behind it.
                CHECK(words[6] == static_cast<uint32_t>(OcclusionOutcome::Retained));
                CHECK(words[7] == 1);
            }
        };
        checkProbe(early);
        if (late && kind != HzbMatrixCase::Culled)
            checkProbe(late);
    }
    REQUIRE(fixture.visibility->retireThrough(frame.frame.number));
    const auto retired = fixture.visibility->takeRetired();
    REQUIRE(retired.size() == 1);
    CHECK(std::memcmp(&retired[0].occlusionParams, fixture.recorded[0].params.data(),
                      sizeof(OcclusionParams)) == 0);
    CHECK(retired[0].occlusionAssociation == status.occlusionAssociation);
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib" && camera &&
        kind != HzbMatrixCase::ZeroCapacity) {
        REQUIRE(retired[0].scene.candidates.size() == 1);
        // An occluded state word stores reason Occluded and no outcome bits, as in v1.
        const auto& candidate = retired[0].scene.candidates[0];
        CHECK(candidate.occlusion.outcome ==
              (valid ? OcclusionOutcome::NotTested : OcclusionOutcome::HistoryInvalid));
        CHECK(candidate.occlusion.occluded == valid);
        CHECK((candidate.reason == VisibilityReason::Occluded) == valid);
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("final HZB observers distinguish early late appended and culled alias readers",
          "[render][device][hzb-matrix]") {
    SECTION("early reader consumes old content before real alias overwrite") {
        runHzbMatrix(HzbMatrixCase::Early);
    }
    SECTION("live late reader invalidates the shared classifier and early probe") {
        runHzbMatrix(HzbMatrixCase::Late);
    }
    SECTION("a live observer appended after earlier lowering participates in final decisions") {
        runHzbMatrix(HzbMatrixCase::Appended);
    }
    SECTION("a culled late observer leaves the actual early reader eligible") {
        runHzbMatrix(HzbMatrixCase::Culled);
    }
}

//======================================================================================================================
TEST_CASE("HZB recording separates invalid and same-frame content from actual camera potential",
          "[render][device][hzb-matrix]") {
    SECTION("initial unbuilt history has no physical read") {
        runHzbMatrix(HzbMatrixCase::Initial);
    }
    SECTION("publication in this same actual frame is not previous history") {
        runHzbMatrix(HzbMatrixCase::SameFrame);
    }
    SECTION("empty camera and shadow work omit history access") {
        runHzbMatrix(HzbMatrixCase::ZeroCamera);
    }
    SECTION("real shadow candidates alone omit history access") {
        runHzbMatrix(HzbMatrixCase::ShadowOnly);
    }
    SECTION("zero outputs retain nonzero camera sampling potential") {
        runHzbMatrix(HzbMatrixCase::ZeroCapacity);
    }
}

//======================================================================================================================
TEST_CASE("HZB masks preserve unrelated exact reads explicit terminals and partial writers",
          "[render][device][hzb-matrix]") {
    SECTION("an identical normal declaration has a real unconditional publication dispatch") {
        runHzbMatrix(HzbMatrixCase::NormalRead);
    }
    SECTION("an explicit normal terminal is not an observer-only final use") {
        runHzbMatrix(HzbMatrixCase::ExplicitTerminal);
    }
    SECTION("an actual reduction prefix retains its physical writer across invalid observers") {
        runHzbMatrix(HzbMatrixCase::Partial);
    }
}

//======================================================================================================================
TEST_CASE("a partial overwrite invalidates complete content in the exact cycled HZB allocation",
          "[render][device][hzb-matrix][hzb-boundary]") {
    HzbMatrixFixture fixture;
    REQUIRE(fixture.stage->resize(80, 80, fixture.states));
    REQUIRE(fixture.stage->layout().levelCount > 1);
    const auto fullBuild = [&](HzbMatrixFrame& frame) {
        RenderGraph graph;
        graph.bindTextureStates(fixture.states);
        const auto input =
            graph.importPersistentTexture(fixture.depth, "lmx.test.hzbBoundary.depth");
        const auto source = fixture.clear(graph, input, 1, "lmx.test.hzbBoundary.fullClear");
        fixture.stage->declare(graph, frame.frame.commands, source,
                               {.frameNumber = 0, .activeWidth = 8, .activeHeight = 8});
        const auto plan = lowered(graph, frame.frame.number);
        CHECK(std::ranges::count_if(plan.ops, [](const auto& op) {
                  const auto* run = std::get_if<RunOp>(&op);
                  return run && run->label == "lmx.pass.hzb.publish";
              }) == 1);
        // Ordinary execution records the whole live graph, including its real publication.
        graph.execute(frame.frame.commands, frame.frame.number);
        return fixture.stage->previousTexture();
    };
    rojoRHI::Texture firstSlot{}, secondSlot{};
    uint64_t firstCompleteFrame = 0;
    {
        HzbMatrixFrame frame(*fixture.gpu);
        firstSlot = fullBuild(frame);
        firstCompleteFrame = frame.frame.number;
        REQUIRE(firstSlot.bits != 0);
        frame.close();
        fixture.gpu->waitIdle();
    }
    uint64_t overwrittenCompleteFrame = 0;
    {
        HzbMatrixFrame frame(*fixture.gpu);
        // First prove that this exact allocation really is eligible previous complete history.
        {
            RenderGraph graph;
            graph.bindTextureStates(fixture.states);
            auto bundle = fixture.stage->beginGraph(graph, frame.frame.commands);
            auto history = fixture.stage->importPrevious(*bundle);
            CHECK(fixture.stage->previousTexture() == firstSlot);
            CHECK(firstCompleteFrame + 1 == frame.frame.number);
            const auto canonical = hzbMatrixCanonical(graph, firstSlot, frame.frame.number);
            auto probe = hzbMatrixProbe(fixture, graph);
            appendHzbProbe(fixture, graph, frame, history, probe, canonical,
                           "lmx.test.hzbBoundary.complete", true);
            graph.execute(frame.frame.commands, frame.frame.number);
            REQUIRE(probe->calls == 1);
            CHECK((probe->params.flags & 3u) == 3u);
            REQUIRE(probe->ticket);
        }
        secondSlot = fullBuild(frame);
        REQUIRE(secondSlot.bits != 0);
        CHECK(secondSlot != firstSlot);
        CHECK(fullBuild(frame) == firstSlot);
        RenderGraph completeGraph;
        completeGraph.bindTextureStates(fixture.states);
        auto completeBundle = fixture.stage->beginGraph(completeGraph, frame.frame.commands);
        auto sameAllocationComplete = fixture.stage->importPrevious(*completeBundle);
        overwrittenCompleteFrame = frame.frame.number;
        CHECK(overwrittenCompleteFrame == frame.frame.number);
        // Complete, but never eligible as previous history on its own actual frame.
        CHECK((sameAllocationComplete->recordingParams().flags & 2u) == 0);
        const auto upperBefore = fixture.states.find(firstSlot, {1, 1, 0, 1});
        REQUIRE(upperBefore);
        REQUIRE(upperBefore->layout == rojoRHI::Layout::ShaderRead);
        CHECK(fullBuild(frame) == secondSlot);
        // Same actual frame: level0 overwrites A after full A/B publications. The old complete
        // stamp would be adjacent on the next frame, so adjacency rejection alone cannot pass.
        fixture.prime(frame, true);
        CHECK(fixture.stage->previousTexture() == firstSlot);
        const auto lowerAfter = fixture.states.find(firstSlot, {0, 1, 0, 1});
        const auto upperAfter = fixture.states.find(firstSlot, {1, 1, 0, 1});
        REQUIRE(lowerAfter);
        REQUIRE(upperAfter);
        CHECK(lowerAfter->stage == rojoRHI::Stage::ComputeShader);
        CHECK(lowerAfter->access == rojoRHI::Access::ShaderWrite);
        CHECK(lowerAfter->layout == rojoRHI::Layout::ShaderWrite);
        CHECK(upperAfter->stage == upperBefore->stage);
        CHECK(upperAfter->access == upperBefore->access);
        CHECK(upperAfter->layout == upperBefore->layout);
        frame.close();
        fixture.gpu->waitIdle();
    }
    {
        HzbMatrixFrame frame(*fixture.gpu);
        CHECK(frame.frame.number == overwrittenCompleteFrame + 1);
        RenderGraph graph;
        graph.bindTextureStates(fixture.states);
        auto bundle = fixture.stage->beginGraph(graph, frame.frame.commands);
        auto history = fixture.stage->importPrevious(*bundle);
        CHECK(fixture.stage->previousTexture() == firstSlot);
        CHECK(history->source().built);
        CHECK(history->source().frameNumber == 0);
        const auto frontier = fixture.states.find(firstSlot, {0, 1, 0, 1});
        REQUIRE(frontier);
        REQUIRE(frontier->access == rojoRHI::Access::ShaderWrite);
        REQUIRE(frontier->layout == rojoRHI::Layout::ShaderWrite);
        const auto canonical = hzbMatrixCanonical(graph, firstSlot, frame.frame.number);
        auto probe = hzbMatrixProbe(fixture, graph);
        appendHzbProbe(fixture, graph, frame, history, probe, canonical,
                       "lmx.test.hzbBoundary.conservative", true);
        graph.execute(frame.frame.commands, frame.frame.number);
        REQUIRE(probe->calls == 1);
        CHECK((probe->params.flags & 1u) != 0);
        CHECK((probe->params.flags & 2u) == 0);
        const auto retained = fixture.states.find(firstSlot, {0, 1, 0, 1});
        REQUIRE(retained);
        CHECK(retained->stage == frontier->stage);
        CHECK(retained->access == frontier->access);
        CHECK(retained->layout == frontier->layout);
        frame.close();
        fixture.gpu->waitIdle();
        REQUIRE(probe->ticket);
        if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib") {
            std::array<uint32_t, 8> words;
            REQUIRE(resolve(*fixture.gpu, *probe->ticket, resolveBytes(std::span(words))));
            CHECK(words[6] == static_cast<uint32_t>(OcclusionOutcome::HistoryInvalid));
            CHECK(words[7] == 0);
        }
    }
}
