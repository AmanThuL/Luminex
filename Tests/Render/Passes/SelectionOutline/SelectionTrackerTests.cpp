#include "Support/OracleRoots.h"
#include "Support/SceneTableTestSupport.h"
#include "Support/TestGpu.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/View/Camera.h"
#include "Render/Graph/FrameDeclaration.h"
#include "Render/Passes/SelectionOutline/SelectionOutline.h"
#include "Render/Renderer/Renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NDEBUG
namespace {
using namespace lmx;

constexpr uint32_t kViewCapacity = 1'000'000;
// Views the fixture holds before exhaustion, the Renderer's temporal motion, reactive, slot and
// fallback views among them. It is the one count to update when the fixture's views change:
// exhaustion reserves the free slots greedily by descending powers of two, so it keeps one range
// per set bit of the free count and is refused once at every power of two.
constexpr uint32_t kFixtureViews = 31;
constexpr uint32_t kFreeViews = kViewCapacity - kFixtureViews;
constexpr uint32_t kExpectedRanges = static_cast<uint32_t>(std::popcount(kFreeViews));
constexpr uint32_t kExpectedRefusals = static_cast<uint32_t>(std::bit_width(kViewCapacity));
constexpr size_t kMaxRanges = 460;
constexpr uint32_t kMaxReserveAttempts = 480;
constexpr std::string_view kHeapFull = "the view heap of 1000000 slots is full";

struct FrameGuard {
    engine::GpuContext& gpu;
    engine::Frame& frame;
    bool open = true;

    ~FrameGuard();
    void finish();
};

//======================================================================================================================
FrameGuard::~FrameGuard() {
    if (open)
        gpu.submitAndEndFrame(frame);
}

//======================================================================================================================
void FrameGuard::finish() {
    gpu.submitAndEndFrame(frame);
    open = false;
}

struct SingleRangeOwner {
    rojoRHI::Device device;
    std::optional<rojoRHI::IndexRange> range;

    ~SingleRangeOwner() noexcept(false);
};

//======================================================================================================================
SingleRangeOwner::~SingleRangeOwner() noexcept(false) {
    if (range)
        device.release(*range);
}

struct RangeReservations {
    rojoRHI::Device device;
    std::array<rojoRHI::IndexRange, kMaxRanges> ranges;
    size_t used = 0;
    bool releaseUncertain = false;
    uint32_t attempts = 0;
    uint32_t refusals = 0;
    uint32_t totalSlots = 0;

    //==================================================================================================================
    explicit RangeReservations(rojoRHI::Device source);
    //==================================================================================================================
    RangeReservations(const RangeReservations&) = delete;
    //==================================================================================================================
    RangeReservations& operator=(const RangeReservations&) = delete;
    ~RangeReservations() noexcept(false);
    void exhaust();
    void releaseAll();
};

//======================================================================================================================
RangeReservations::RangeReservations(rojoRHI::Device source) : device(source) {}

//======================================================================================================================
RangeReservations::~RangeReservations() noexcept(false) {
    // A throwing release may already have mutated the allocator; it must never be retried.
    if (!releaseUncertain)
        releaseAll();
}

//======================================================================================================================
void RangeReservations::exhaust() {
    REQUIRE(device.capabilities().maxViews == kViewCapacity);
    for (uint32_t count = std::bit_floor(kViewCapacity); count != 0; count /= 2) {
        for (;;) {
            CAPTURE(count, attempts, used);
            REQUIRE(attempts < kMaxReserveAttempts);
            auto result = device.reserveViews(count);
            ++attempts;
            if (!result) {
                REQUIRE(result.error().category == rojoRHI::ErrorCategory::OutOfMemory);
                REQUIRE(
                    result.error().message ==
                    std::format("the view heap of {} slots has no contiguous run of {} free slots",
                                kViewCapacity, count));
                ++refusals;
                break;
            }
            SingleRangeOwner temporary{device, *result};
            REQUIRE(used < ranges.size());
            ranges[used++] = *result;
            temporary.range.reset();
            REQUIRE(result->kind == rojoRHI::IndexRange::Kind::Views);
            REQUIRE(result->count == count);
            totalSlots += count;
        }
    }
    REQUIRE(used == kExpectedRanges);
    REQUIRE(refusals == kExpectedRefusals);
    REQUIRE(attempts == kExpectedRanges + kExpectedRefusals);
    REQUIRE(totalSlots == kFreeViews);
}

//======================================================================================================================
void RangeReservations::releaseAll() {
    while (used != 0) {
        releaseUncertain = true;
        device.release(ranges[used - 1]);
        releaseUncertain = false;
        --used;
    }
}

struct TextureFacts {
    rojoRHI::TextureType type;
    rojoRHI::Format format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mips;
    uint32_t layers;
    uint32_t samples;
    rojoRHI::TextureUsage usage;
    std::string label;

    bool operator==(const TextureFacts&) const;
};

struct PhysicalCell {
    uint32_t mip;
    uint32_t layer;
    render::TextureState state;

    bool operator==(const PhysicalCell&) const;
};

struct TargetSnapshot {
    rojoRHI::Texture texture;
    rojoRHI::ViewIndex sampled;
    uint32_t storageViews;
    rojoRHI::Layout initialLayout;
    TextureFacts facts;
    std::optional<render::TextureUse> logical;
    std::vector<PhysicalCell> physical;
    size_t tableSize;

    bool operator==(const TargetSnapshot&) const;
};

//======================================================================================================================
bool TextureFacts::operator==(const TextureFacts&) const = default;

//======================================================================================================================
bool PhysicalCell::operator==(const PhysicalCell&) const = default;

//======================================================================================================================
bool TargetSnapshot::operator==(const TargetSnapshot&) const = default;

//======================================================================================================================
TextureFacts textureFacts(const engine::GpuTexture& target) {
    const auto& desc = target.desc();
    return {desc.type, desc.format, desc.extent.width, desc.extent.height, desc.extent.depth,
            desc.mips, desc.layers, desc.samples,      desc.usage,         std::string(desc.label)};
}

//======================================================================================================================
TargetSnapshot snapshotTarget(const engine::GpuTexture& target,
                              const render::TextureStateTable& states) {
    TargetSnapshot result{target.rhi(),
                          target.view(),
                          target.storageViewCount(),
                          target.initialLayout(),
                          textureFacts(target),
                          states.logicalUse(target.rhi()),
                          {},
                          states.size()};
    for (uint32_t layer = 0; layer < target.desc().layers; ++layer) {
        for (uint32_t mip = 0; mip < target.desc().mips; ++mip) {
            const auto ranges = states.physicalRanges(target.rhi(), {mip, 1, layer, 1});
            REQUIRE(ranges.size() == 1);
            REQUIRE(ranges[0].range.baseMip == mip);
            REQUIRE(ranges[0].range.mipCount == 1);
            REQUIRE(ranges[0].range.baseLayer == layer);
            REQUIRE(ranges[0].range.layerCount == 1);
            result.physical.push_back({mip, layer, ranges[0].state});
        }
    }
    return result;
}

//======================================================================================================================
engine::MeshData selectionQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

struct SelectionFixture {
    std::unique_ptr<engine::GpuContext> gpu;
    render::TransientPool pool;
    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<render::SelectionOutline> selection;
    engine::GpuTexture offscreen;
    engine::GpuOwned<rojoRHI::ShaderLibrary> sampleLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> samplePipeline;
    engine::Camera camera;
    test::FixtureMesh mesh;
    std::array<test::FixtureDrawItem, 2> items;
    test::FixtureSceneView view;

    //==================================================================================================================
    explicit SelectionFixture(std::unique_ptr<engine::GpuContext> context);
    ~SelectionFixture();
    void setup();
    //==================================================================================================================
    void replaceOffscreen(uint32_t width, uint32_t height);
    //==================================================================================================================
    render::CompiledFrameRecord sampleFrame(bool visible = true, bool enabled = true);
    void sampleEpoch();
    void emptyRetirementFrames();
};

//======================================================================================================================
SelectionFixture::SelectionFixture(std::unique_ptr<engine::GpuContext> context)
    : gpu(std::move(context)), pool(*gpu) {}

//======================================================================================================================
SelectionFixture::~SelectionFixture() {
    LMX_ASSERT(!gpu->inFrame(), "selection fixture must close its acquired frame");
    gpu->waitIdle();
    view.state.reset();
    view.retired.clear();
    mesh.retained.clear();
}

//======================================================================================================================
void SelectionFixture::replaceOffscreen(uint32_t width, uint32_t height) {
    auto created =
        engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::BGRA8Unorm,
                                                   .extent = {width, height, 1},
                                                   .usage = rojoRHI::TextureUsage::RenderTarget |
                                                            rojoRHI::TextureUsage::Sampled,
                                                   .label = "lmx.test.selection.consumer"});
    INFO(errorOf(created));
    REQUIRE(created);
    if (offscreen)
        renderer->textureStates().forget(offscreen.rhi());
    offscreen = std::move(*created);
}

//======================================================================================================================
void SelectionFixture::setup() {
    auto created = render::Renderer::create(*gpu, 64, 64);
    INFO(errorOf(created));
    REQUIRE(created);
    renderer = std::move(*created);
    auto outlined = render::SelectionOutline::create(*gpu, 64, 64);
    INFO(errorOf(outlined));
    REQUIRE(outlined);
    selection = std::move(*outlined);
    replaceOffscreen(64, 64);
    auto library = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library);
    sampleLibrary = std::move(*library);
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::BGRA8Unorm}}};
    auto pipeline = engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline({.vertex = {sampleLibrary.get(), "vertexMain"},
                                      .fragment = {sampleLibrary.get(), "fragmentMain"},
                                      .raster = {.cull = rojoRHI::CullMode::None},
                                      .depthStencil = {.depthTest = false,
                                                       .depthWrite = false,
                                                       .depthCompare = rojoRHI::CompareOp::Always},
                                      .colors = colors,
                                      .label = "lmx.test.selection.consumerPipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline);
    samplePipeline = std::move(*pipeline);
    auto geometry = test::fixtureMesh(gpu->device(), selectionQuad(), "lmx.test.selection.mesh");
    INFO(errorOf(geometry));
    REQUIRE(geometry);
    mesh = std::move(*geometry);
    for (auto& item : items) {
        item.mesh = &mesh;
        item.material.albedo = {0, 0, 0, 1};
        item.material.emissive = {0.1f, 0.25f, 0.4f};
    }
    items[1].enabled = false;
    items[1].model = glm::translate(glm::mat4{1.0f}, glm::vec3{0, 0, 1});
    items[1].previousModel = items[1].model;
    view.items = items;
    view.boundingSphere = {0, 0, 0, 8};
    view.bloomEnabled = false;
    view.temporal.enabled = false;
    view.localLightMode = engine::LocalLightMode::Off;
    view.shadowCaster = -1;
    for (auto& light : view.lights)
        light.strength = {0, 0, 0};
    camera.position = {0, 0, 4};
    camera.fovY = glm::half_pi<float>();
}

//======================================================================================================================
render::CompiledFrameRecord SelectionFixture::sampleFrame(bool visible, bool enabled) {
    items[0].enabled = enabled;
    auto frame = gpu->beginFrame();
    FrameGuard guard{*gpu, frame};
    const auto prepared = view.prepare(*gpu);
    REQUIRE(prepared.items.size() == 2);
    REQUIRE(prepared.items[0].instanceRow < prepared.tables.instanceRows.size());
    REQUIRE(prepared.items[1].instanceRow < prepared.tables.instanceRows.size());
    REQUIRE(((prepared.tables.instanceRows[prepared.items[0].instanceRow].flags &
              engine::kInstanceDisabled) != 0) == !enabled);
    REQUIRE((prepared.tables.instanceRows[prepared.items[1].instanceRow].flags &
             engine::kInstanceDisabled) != 0);
    auto& commands = frame.commands;
    render::FrameDeclaration declaration(pool, commands, true);
    auto declared = declaration.declare(*renderer, camera, prepared);
    INFO(errorOf(declared));
    REQUIRE(declared);
    auto& graph = declaration.graph();
    const auto output = selection->declare(graph, commands, prepared,
                                           {.display = declaration.displayColor(),
                                            .camera = camera,
                                            .selectedDraw = 0,
                                            .backingScale = 1.0f,
                                            .visible = visible});
    const auto target = graph.importPersistentTexture(offscreen, "lmx.test.selection.consumer");
    render::PassDesc consumer{.textureReads = {output},
                              .color = render::ColorAttachment{.handle = target}};
    engine::TextureRef sampled;
    rojoRHI::ResourceAccess sampledAccess;
    uint64_t sampledFrame = 0;
    uint32_t draws = 0;
    graph.addPass("lmx.test.selection.sample", std::move(consumer),
                  [&](const render::PassResources& resources) {
                      sampled = resources.texture(output);
                      commands.setPipeline(samplePipeline.get());
                      commands.pushRoot(test::FullscreenSampleRoot{
                          .source = static_cast<uint32_t>(sampled.view)});
                      sampledAccess = {.texture = sampled.texture,
                                       .range = {0, 1, 0, 1},
                                       .stage = rojoRHI::Stage::FragmentShader,
                                       .access = rojoRHI::Access::ShaderRead};
                      commands.declareAccess(std::span(&sampledAccess, 1));
                      commands.draw(3);
                      sampledFrame = gpu->frameNumber();
                      ++draws;
                  });
    graph.exportTexture(render::nextVersion(target));
    auto record = declaration.execute();
    guard.finish();

    REQUIRE(record.frameId == frame.number);
    REQUIRE(sampledFrame == frame.number);
    REQUIRE(draws == 1);
    REQUIRE(sampled.texture == selection->target().rhi());
    REQUIRE(sampled.view == selection->target().view());
    REQUIRE(sampled.extent.width == selection->target().desc().extent.width);
    REQUIRE(sampled.extent.height == selection->target().desc().extent.height);
    REQUIRE(sampledAccess.texture == sampled.texture);
    REQUIRE(sampledAccess.range.baseMip == 0);
    REQUIRE(sampledAccess.range.mipCount == 1);
    REQUIRE(sampledAccess.range.baseLayer == 0);
    REQUIRE(sampledAccess.range.layerCount == 1);
    REQUIRE(sampledAccess.stage == rojoRHI::Stage::FragmentShader);
    REQUIRE(sampledAccess.access == rojoRHI::Access::ShaderRead);
    const bool outline = visible && enabled;
    const std::array<std::string_view, 4> names{
        "lmx.pass.selection.coverage", "lmx.pass.selection.visibility",
        "lmx.pass.selection.outline", "lmx.pass.selection.passthrough"};
    for (size_t index = 0; index < names.size(); ++index) {
        const auto pass =
            std::ranges::find(record.debug.passes, names[index], &render::DebugPass::label);
        const bool expected = index == 3 ? !outline : outline;
        REQUIRE((pass != record.debug.passes.end()) == expected);
        if (expected) {
            const auto passIndex = static_cast<uint32_t>(pass - record.debug.passes.begin());
            REQUIRE(std::ranges::find(record.debug.schedule.passes, passIndex) !=
                    record.debug.schedule.passes.end());
            REQUIRE_FALSE(pass->cullReason.has_value());
        }
    }
    const auto pass = std::ranges::find(record.debug.passes, "lmx.test.selection.sample",
                                        &render::DebugPass::label);
    REQUIRE(pass != record.debug.passes.end());
    const auto passIndex = static_cast<uint32_t>(pass - record.debug.passes.begin());
    REQUIRE(std::ranges::find(record.debug.schedule.passes, passIndex) !=
            record.debug.schedule.passes.end());
    REQUIRE_FALSE(pass->cullReason.has_value());
    REQUIRE(std::ranges::any_of(pass->uses, [&](const render::DebugUse& use) {
        return use.resource == output.index && use.version == output.version &&
               use.role == render::UseRole::Read;
    }));
    const auto state = renderer->textureStates().find(sampled.texture, {0, 1, 0, 1});
    REQUIRE(state.has_value());
    REQUIRE(state->layout == rojoRHI::Layout::ShaderRead);
    REQUIRE((state->stage & rojoRHI::Stage::FragmentShader) != rojoRHI::Stage::None);
    REQUIRE((state->access & rojoRHI::Access::ShaderRead) != rojoRHI::Access::None);
    return record;
}

//======================================================================================================================
void SelectionFixture::sampleEpoch() {
    const uint64_t first = gpu->frameNumber() + 1;
    for (uint32_t ordinal = 0; ordinal < 4; ++ordinal) {
        CAPTURE(ordinal);
        const auto record = sampleFrame();
        REQUIRE(record.frameId == first + ordinal);
    }
}

//======================================================================================================================
void SelectionFixture::emptyRetirementFrames() {
    REQUIRE(gpu->frameNumber() == 4);
    for (uint64_t expected = 5; expected <= 8; ++expected) {
        auto frame = gpu->beginFrame();
        FrameGuard guard{*gpu, frame};
        const uint64_t completed = gpu->completedFrame();
        guard.finish();
        REQUIRE(frame.number == expected);
        if (expected == 8)
            REQUIRE(completed >= 5);
    }
    REQUIRE(gpu->frameNumber() == 8);
}

//======================================================================================================================
void requireReplacement(const TargetSnapshot& old, const engine::GpuTexture& target,
                        const render::TextureStateTable& states, uint32_t width, uint32_t height) {
    REQUIRE(target.rhi() != old.texture);
    REQUIRE(target.view() != old.sampled);
    REQUIRE(target.storageViewCount() == old.storageViews);
    REQUIRE(target.initialLayout() == rojoRHI::Layout::Undefined);
    auto expected = old.facts;
    expected.width = width;
    expected.height = height;
    REQUIRE(textureFacts(target) == expected);
    REQUIRE_FALSE(states.logicalUse(old.texture).has_value());
    REQUIRE(states.physicalRanges(old.texture, {0, old.facts.mips, 0, old.facts.layers}).empty());
    REQUIRE_FALSE(states.logicalUse(target.rhi()).has_value());
    REQUIRE(states.physicalRanges(target.rhi(), {0, target.desc().mips, 0, target.desc().layers})
                .empty());
}

} // namespace
#endif

//======================================================================================================================
TEST_CASE(
    "selection visible rejected and disabled paths preserve actual consumers under the tracker",
    "[render][device][tracker]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    SelectionFixture fixture(createTestGpu({.validation = true, .framesInFlight = 3}));
    fixture.setup();
    for (uint32_t ordinal = 0; ordinal < 4; ++ordinal) {
        CAPTURE(ordinal);
        const auto record = fixture.sampleFrame(ordinal != 1, ordinal != 2);
        REQUIRE(record.frameId == uint64_t{ordinal} + 1);
    }
#endif
}

//======================================================================================================================
TEST_CASE("selection replacement and resize preserve target generations under the tracker",
          "[render][device][tracker]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    SelectionFixture fixture(createTestGpu({.validation = true, .framesInFlight = 3}));
    fixture.setup();
    fixture.sampleEpoch();
    auto& states = fixture.renderer->textureStates();
    const auto beforeIdle = snapshotTarget(fixture.selection->target(), states);
    fixture.gpu->waitIdle();
    const auto beforeFailure = snapshotTarget(fixture.selection->target(), states);
    REQUIRE(beforeFailure == beforeIdle);
    {
        RangeReservations reservations(fixture.gpu->device());
        reservations.exhaust();
        const auto witness = fixture.gpu->device().createView(fixture.selection->target().rhi(),
                                                              {.range = {0, 1, 0, 1}});
        engine::GpuOwned<rojoRHI::ViewIndex> unexpectedWitness;
        if (witness)
            unexpectedWitness = {fixture.gpu->device(), *witness};
        REQUIRE_FALSE(witness.has_value());
        REQUIRE(witness.error().category == rojoRHI::ErrorCategory::OutOfMemory);
        REQUIRE(witness.error().message == kHeapFull);
        const auto resized = fixture.selection->resize(64, 64, states);
        REQUIRE_FALSE(resized.has_value());
        REQUIRE(resized.error().code == ErrorCode::Gpu);
        REQUIRE(resized.error().message == kHeapFull);
        REQUIRE(snapshotTarget(fixture.selection->target(), states) == beforeFailure);
        reservations.releaseAll();
    }
    fixture.emptyRetirementFrames();
    REQUIRE(snapshotTarget(fixture.selection->target(), states) == beforeFailure);
    auto recovered = engine::adoptGpu(
        fixture.gpu->device(), fixture.gpu->device().createView(fixture.selection->target().rhi(),
                                                                {.range = {0, 1, 0, 1}}));
    INFO(errorOf(recovered));
    REQUIRE(recovered.has_value());
    recovered->reset();
    fixture.sampleEpoch();
    REQUIRE(fixture.selection->target().rhi() == beforeFailure.texture);
    REQUIRE(fixture.selection->target().view() == beforeFailure.sampled);
    fixture.gpu->waitIdle();
    const auto old = snapshotTarget(fixture.selection->target(), states);
    const auto equalExtent = fixture.selection->resize(64, 64, states);
    INFO(errorOf(equalExtent));
    REQUIRE(equalExtent);
    requireReplacement(old, fixture.selection->target(), states, 64, 64);
    fixture.sampleEpoch();
    fixture.gpu->waitIdle();
    const auto previous = snapshotTarget(fixture.selection->target(), states);
    auto rendererResize = fixture.renderer->resize(80, 48);
    INFO(errorOf(rendererResize));
    REQUIRE(rendererResize);
    fixture.replaceOffscreen(80, 48);
    const auto differentExtent = fixture.selection->resize(80, 48, states);
    INFO(errorOf(differentExtent));
    REQUIRE(differentExtent);
    requireReplacement(previous, fixture.selection->target(), states, 80, 48);
    fixture.sampleEpoch();
#endif
}
