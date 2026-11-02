#include "Render/Passes/Occlusion/HzbHistoryBinding.h"
#include "Render/Passes/Occlusion/HzbStage.h"
#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lmx::render {
struct HzbResizeState {
    std::vector<uint64_t> words;
    std::vector<float> reals;
    std::vector<std::string> labels;
    //==================================================================================================================
    bool operator==(const HzbResizeState&) const = default;
};

struct HzbResizeTestAccess {
    //==================================================================================================================
    template <typename... T>
    static void append(HzbResizeState& state, T... values) {
        (state.words.push_back(static_cast<uint64_t>(values)), ...);
    }

    //==================================================================================================================
    static void source(HzbResizeState& state, const HzbSource& source) {
        append(state, source.frameNumber, source.coverageEpoch, source.sceneGeneration,
               source.activeWidth, source.activeHeight, source.outputWidth, source.outputHeight,
               source.levelCount, source.built);
        for (glm::length_t column = 0; column < 4; ++column)
            for (glm::length_t row = 0; row < 4; ++row)
                state.reals.push_back(source.viewProjection[column][row]);
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            state.reals.push_back(source.cameraPosition[axis]);
            state.reals.push_back(source.cameraForward[axis]);
        }
    }

    //==================================================================================================================
    static HzbResizeState snapshot(const HzbStage& stage) {
        HzbResizeState result;
        append(result, reinterpret_cast<uintptr_t>(&stage.m_gpu), stage.m_outputWidth,
               stage.m_outputHeight, stage.m_next, stage.m_lastBuilt, stage.m_layout.width,
               stage.m_layout.height, stage.m_layout.levelCount, stage.m_layout.bytes,
               stage.m_declarationSerial, stage.m_allocationGeneration,
               reinterpret_cast<uintptr_t>(stage.m_allocation.get()),
               stage.m_reduceLibrary.get().bits, stage.m_reducePipeline.get().bits,
               stage.m_publishLibrary.get().bits, stage.m_publishPipeline.get().bits,
               stage.m_publishBuffer.rhi().bits, stage.m_publishBuffer.desc().size,
               stage.m_publishBuffer.desc().usage, stage.m_published, stage.m_publishPhysical.stage,
               stage.m_publishPhysical.access, stage.m_publishPhysical.layout);
        result.labels.emplace_back(stage.m_publishBuffer.desc().label);
        for (const auto& value : stage.m_sources)
            source(result, value);
        for (const auto serial : stage.m_declaredSerials)
            append(result, serial);
        if (stage.m_allocation) {
            append(result, stage.m_allocation->generation);
            for (const auto& texture : stage.m_allocation->textures) {
                const auto& desc = texture.desc();
                append(result, texture.rhi().bits, texture.view(), texture.initialLayout(),
                       desc.type, desc.format, desc.extent.width, desc.extent.height,
                       desc.extent.depth, desc.mips, desc.layers, desc.samples, desc.usage,
                       texture.storageViewCount());
                result.labels.emplace_back(desc.label);
                for (uint32_t mip = 0; mip < texture.storageViewCount(); ++mip)
                    append(result, texture.storageView(mip));
            }
            for (const auto& content : stage.m_allocation->contents) {
                append(result, reinterpret_cast<uintptr_t>(content.owner),
                       reinterpret_cast<uintptr_t>(content.commands), content.actualFrame,
                       content.generation, content.serial, content.recordedLevels,
                       content.complete);
                source(result, content.source);
            }
        }
        append(result, stage.m_graphBindings.size(), stage.m_graphBindings.capacity());
        for (const auto& borrow : stage.m_graphBindings) {
            const auto binding = borrow.binding.lock();
            append(result, reinterpret_cast<uintptr_t>(borrow.graph),
                   reinterpret_cast<uintptr_t>(borrow.commands), borrow.frame,
                   reinterpret_cast<uintptr_t>(binding.get()), borrow.binding.expired());
        }
        return result;
    }

    //==================================================================================================================
    static bool current(const HzbStage& stage, const PreparedHzbResize& prepared) {
        return stage.resizeCurrent(prepared);
    }

    //==================================================================================================================
    static std::array<engine::TextureRef, 2> replacement(const HzbStage& stage,
                                                         const PreparedHzbResize& prepared) {
        const auto owners = stage.resizeTextures(prepared);
        std::array<engine::TextureRef, 2> result{};
        for (uint32_t slot = 0; slot < owners.size(); ++slot) {
            if (!owners[slot])
                continue;
            const auto& owner = *owners[slot];
            const auto& desc = owner.desc();
            REQUIRE(desc.label == (slot == 0 ? "lmx.render.hzb0" : "lmx.render.hzb1"));
            REQUIRE(desc.usage ==
                    (rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::Storage));
            REQUIRE(owner.storageViewCount() == desc.mips);
            for (uint32_t mip = 0; mip < desc.mips; ++mip)
                REQUIRE(owner.storageView(mip) != engine::kNoView);
            result[slot] = owner.ref();
        }
        return result;
    }

    //==================================================================================================================
    static std::array<engine::TextureRef, 2> textures(const HzbStage& stage) {
        if (!stage.m_allocation)
            return {};
        return {stage.m_allocation->textures[0].ref(), stage.m_allocation->textures[1].ref()};
    }

    //==================================================================================================================
    static uint64_t generation(const HzbStage& stage) { return stage.m_allocationGeneration; }

    //==================================================================================================================
    static uint64_t serial(const HzbStage& stage) { return stage.m_declarationSerial; }

    //==================================================================================================================
    static std::array<uint64_t, 2> declared(const HzbStage& stage) {
        return stage.m_declaredSerials;
    }

    //==================================================================================================================
    static bool complete(const HzbStage& stage, uint32_t slot) {
        return stage.m_allocation && stage.m_allocation->contents[slot].complete;
    }

    //==================================================================================================================
    static bool slotsReset(const HzbStage& stage) {
        if (!stage.m_allocation || stage.m_next != 0 || stage.m_lastBuilt != 1 ||
            stage.m_declaredSerials != std::array<uint64_t, 2>{} ||
            stage.m_allocation->generation != stage.m_allocationGeneration)
            return false;
        HzbResizeState empty;
        source(empty, HzbSource{});
        for (uint32_t slot = 0; slot < stage.m_sources.size(); ++slot) {
            HzbResizeState declared, recorded;
            source(declared, stage.m_sources[slot]);
            const auto& content = stage.m_allocation->contents[slot];
            source(recorded, content.source);
            if (declared != empty || recorded != empty || content.owner || content.commands ||
                content.actualFrame != 0 || content.generation != 0 || content.serial != 0 ||
                content.recordedLevels != 0 || content.complete)
                return false;
        }
        return true;
    }

    //==================================================================================================================
    static bool published(const HzbStage& stage) { return stage.m_published; }

    //==================================================================================================================
    static rojoRHI::Buffer publication(const HzbStage& stage) {
        return stage.m_publishBuffer.rhi();
    }

    //==================================================================================================================
    static SyncPoint publicationFrontier(const HzbStage& stage) { return stage.m_publishPhysical; }

    //==================================================================================================================
    static uint32_t borrowCount(const HzbStage& stage) {
        return static_cast<uint32_t>(stage.m_graphBindings.size());
    }
};
} // namespace lmx::render

namespace {
using namespace lmx;
using namespace lmx::render;

static_assert(!std::is_default_constructible_v<PreparedHzbResize>);
static_assert(!std::is_copy_constructible_v<PreparedHzbResize>);
static_assert(!std::is_copy_assignable_v<PreparedHzbResize>);
static_assert(std::is_nothrow_move_constructible_v<PreparedHzbResize>);
static_assert(std::is_nothrow_move_assignable_v<PreparedHzbResize>);
static_assert(std::is_nothrow_destructible_v<PreparedHzbResize>);
static_assert(std::is_nothrow_move_assignable_v<engine::GpuTexture>);
static_assert(std::is_nothrow_move_assignable_v<TextureStateTable>);
static_assert(std::is_nothrow_swappable_v<TextureStateTable>);
static_assert(noexcept(std::declval<HzbStage&>().commitResize(std::declval<PreparedHzbResize&&>(),
                                                              std::declval<TextureStateTable&>())));

struct ResizeCell {
    uint32_t mip = 0, layer = 0, mipCount = 0, layerCount = 0;
    TextureState state;
    //==================================================================================================================
    bool operator==(const ResizeCell&) const = default;
};
struct ResizeRow {
    uint64_t identity = 0;
    uint32_t mips = 0, layers = 0;
    std::optional<TextureUse> logical;
    std::vector<ResizeCell> physical;
    //==================================================================================================================
    bool operator==(const ResizeRow&) const = default;
};

// The fixture owns every possible table identity. Count equality makes the roster exhaustive.
//======================================================================================================================
std::vector<ResizeRow> resizeRows(const TextureStateTable& states,
                                  std::span<const engine::TextureRef> known) {
    std::set<uint64_t> identities;
    std::vector<ResizeRow> result;
    for (const auto& ref : known) {
        REQUIRE(ref.texture.bits != 0);
        REQUIRE(identities.insert(ref.texture.bits).second);
        ResizeRow row{ref.texture.bits, ref.mips, ref.layers, states.logicalUse(ref.texture), {}};
        const auto cells = states.physicalRanges(ref.texture, {0, ref.mips, 0, ref.layers});
        REQUIRE(cells.size() == static_cast<size_t>(ref.mips) * ref.layers);
        for (const auto& cell : cells)
            row.physical.push_back({cell.range.baseMip, cell.range.baseLayer, cell.range.mipCount,
                                    cell.range.layerCount, cell.state});
        result.push_back(std::move(row));
    }
    REQUIRE(identities.size() == states.size());
    return result;
}

//======================================================================================================================
std::unique_ptr<HzbStage> resizeStage(engine::GpuContext& gpu) {
    auto result = HzbStage::create(gpu);
    INFO(lmx::test::errorOf(result));
    REQUIRE(result);
    return std::move(*result);
}

//======================================================================================================================
engine::GpuTexture resizeSentinel(engine::GpuContext& gpu, std::string_view label) {
    auto result = engine::GpuTexture::create(
        gpu.device(), {.type = rojoRHI::TextureType::Tex2DArray,
                       .format = rojoRHI::Format::R32Float,
                       .extent = {16, 8, 1},
                       .mips = 3,
                       .layers = 2,
                       .usage = rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::Storage,
                       .label = label});
    INFO(lmx::test::errorOf(result));
    REQUIRE(result);
    return std::move(*result);
}

struct ResizeFrame {
    engine::GpuContext& gpu;
    engine::Frame frame;
    //==================================================================================================================
    explicit ResizeFrame(engine::GpuContext& owner) : gpu(owner), frame(owner.beginFrame()) {}
    //==================================================================================================================
    ~ResizeFrame() { gpu.submitAndEndFrame(frame); }
};

struct ResizeIdle {
    engine::GpuContext& gpu;
    //==================================================================================================================
    ~ResizeIdle() { gpu.waitIdle(); }
};

struct ResizeFixture {
    std::unique_ptr<engine::GpuContext> gpu = createTestGpu();
    std::unique_ptr<HzbStage> stage = resizeStage(*gpu);
    engine::GpuTexture depth = createTestTexture(
        *gpu, rojoRHI::Format::D32Float,
        rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled, 64, 48);
    std::array<engine::GpuTexture, 2> sentinels{
        resizeSentinel(*gpu, "lmx.test.hzb.resize.sentinel0"),
        resizeSentinel(*gpu, "lmx.test.hzb.resize.sentinel1")};
    TextureStateTable states;

    //==================================================================================================================
    explicit ResizeFixture(bool initialize = true) {
        for (uint32_t index = 0; index < sentinels.size(); ++index) {
            const auto ref = sentinels[index].ref();
            states.seedPhysical(ref.texture, ref.mips, ref.layers, ref.initialLayout);
            states.setLogicalUse(ref.texture,
                                 index == 0 ? TextureUse::ShaderRead : TextureUse::StorageWrite);
            for (uint32_t layer = 0; layer < ref.layers; ++layer)
                for (uint32_t mip = 0; mip < ref.mips; ++mip)
                    states.set(ref.texture, {mip, 1, layer, 1},
                               {(mip + layer + index) % 2 == 0 ? rojoRHI::Stage::ComputeShader
                                                               : rojoRHI::Stage::FragmentShader,
                                rojoRHI::Access::ShaderRead, rojoRHI::Layout::ShaderRead});
        }
        if (initialize) {
            REQUIRE(stage->resize(64, 48, states));
            seedPair();
        }
    }

    //==================================================================================================================
    ~ResizeFixture() { gpu->waitIdle(); }

    //==================================================================================================================
    void seedPair() {
        for (const auto& ref : HzbResizeTestAccess::textures(*stage)) {
            REQUIRE(ref.texture.bits != 0);
            states.seedPhysical(ref.texture, ref.mips, ref.layers, ref.initialLayout);
        }
    }

    //==================================================================================================================
    std::vector<engine::TextureRef> known(bool pyramids = true) const {
        std::vector<engine::TextureRef> result{sentinels[0].ref(), sentinels[1].ref()};
        if (pyramids)
            for (const auto& ref : HzbResizeTestAccess::textures(*stage))
                result.push_back(ref);
        return result;
    }

    // Graph and callbacks end before this helper returns; a retained observer owns only safe copies
    // and allocation references. No caller uses its graph/list-dependent accessors after return.
    //==================================================================================================================
    std::shared_ptr<HzbHistoryBinding> prime() {
        ResizeFrame frame(*gpu);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto input = graph.importTexture(depth.ref(), "lmx.test.hzb.resize.depth");
        PassDesc clear;
        clear.depth = DepthAttachment{.handle = input, .store = StoreOp::Store};
        graph.addPass("lmx.test.hzb.resize.clear", std::move(clear), kNoWork);
        auto binding = stage->beginGraph(graph, frame.frame.commands);
        auto history = stage->importPrevious(*binding);
        stage->declare(*binding, nextVersion(input),
                       {.frameNumber = frame.frame.number,
                        .coverageEpoch = 17,
                        .sceneGeneration = 23,
                        .activeWidth = 64,
                        .activeHeight = 48});
        const auto compiled = graph.compile();
        INFO(errorOf(compiled));
        REQUIRE(compiled);
        graph.execute(frame.frame.commands, frame.frame.number);
        return history;
    }

    //==================================================================================================================
    uint32_t nextHistoryFlags() {
        ResizeFrame frame(*gpu);
        RenderGraph graph;
        graph.bindTextureStates(states);
        auto binding = stage->beginGraph(graph, frame.frame.commands);
        return stage->importPrevious(*binding)->recordingParams().flags;
    }
};

//======================================================================================================================
void requireReplacement(const std::array<engine::TextureRef, 2>& refs, uint32_t width,
                        uint32_t height) {
    const auto layout = hzbLayout(width, height);
    REQUIRE(refs[0].texture.bits != 0);
    REQUIRE(refs[1].texture.bits != 0);
    REQUIRE(refs[0].texture != refs[1].texture);
    for (const auto& ref : refs) {
        REQUIRE(ref.extent.width == layout.width);
        REQUIRE(ref.extent.height == layout.height);
        REQUIRE(ref.mips == layout.levelCount);
        REQUIRE(ref.layers == 1);
        REQUIRE(ref.format == rojoRHI::Format::R32Float);
        REQUIRE(ref.view != engine::kNoView);
        REQUIRE(ref.initialLayout == rojoRHI::Layout::Undefined);
    }
}

//======================================================================================================================
TEST_CASE("initial HZB preparation owns a complete pair without publishing it",
          "[render][device][hzb-resize][hzb-resize-initial]") {
    ResizeFixture fixture(false);
    const auto before = HzbResizeTestAccess::snapshot(*fixture.stage);
    const auto rows = resizeRows(fixture.states, fixture.known(false));
    REQUIRE(HzbResizeTestAccess::generation(*fixture.stage) == 0);
    {
        auto prepared = fixture.stage->prepareResize(64, 48);
        INFO(lmx::test::errorOf(prepared));
        REQUIRE(prepared);
        REQUIRE(HzbResizeTestAccess::current(*fixture.stage, *prepared));
        requireReplacement(HzbResizeTestAccess::replacement(*fixture.stage, *prepared), 64, 48);
        REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
        REQUIRE(resizeRows(fixture.states, fixture.known(false)) == rows);
    }
    REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
    REQUIRE(resizeRows(fixture.states, fixture.known(false)) == rows);
    auto prepared = fixture.stage->prepareResize(64, 48);
    REQUIRE(prepared);
    const auto replacement = HzbResizeTestAccess::replacement(*fixture.stage, *prepared);
    fixture.stage->commitResize(std::move(*prepared), fixture.states);
    REQUIRE_FALSE(HzbResizeTestAccess::current(*fixture.stage, *prepared));
    const auto installed = HzbResizeTestAccess::textures(*fixture.stage);
    REQUIRE(installed[0].texture == replacement[0].texture);
    REQUIRE(installed[1].texture == replacement[1].texture);
    REQUIRE(HzbResizeTestAccess::generation(*fixture.stage) == 1);
    REQUIRE(HzbResizeTestAccess::slotsReset(*fixture.stage));
    REQUIRE_FALSE(fixture.stage->previousSource().built);
    REQUIRE(resizeRows(fixture.states, fixture.known(false)) == rows);
}

//======================================================================================================================
TEST_CASE("abandoning changed HZB preparation preserves all primed state and next-frame history",
          "[render][device][hzb-resize][hzb-resize-abandon]") {
    ResizeFixture fixture;
    auto firstCopy = fixture.prime();
    auto secondCopy = fixture.prime();
    fixture.gpu->waitIdle();
    REQUIRE(HzbResizeTestAccess::complete(*fixture.stage, 0));
    REQUIRE(HzbResizeTestAccess::complete(*fixture.stage, 1));
    REQUIRE(HzbResizeTestAccess::published(*fixture.stage));
    const auto before = HzbResizeTestAccess::snapshot(*fixture.stage);
    const auto rows = resizeRows(fixture.states, fixture.known());
    {
        auto prepared = fixture.stage->prepareResize(96, 80);
        REQUIRE(prepared);
        requireReplacement(HzbResizeTestAccess::replacement(*fixture.stage, *prepared), 96, 80);
        REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
        REQUIRE(resizeRows(fixture.states, fixture.known()) == rows);
    }
    REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
    REQUIRE(resizeRows(fixture.states, fixture.known()) == rows);
    REQUIRE((fixture.nextHistoryFlags() & 2u) != 0);
}

//======================================================================================================================
TEST_CASE("same-extent HZB commit consumes only its token and preserves complete live state",
          "[render][device][hzb-resize][hzb-resize-noop]") {
    ResizeFixture fixture;
    auto firstCopy = fixture.prime();
    auto secondCopy = fixture.prime();
    fixture.gpu->waitIdle();
    const auto before = HzbResizeTestAccess::snapshot(*fixture.stage);
    const auto rows = resizeRows(fixture.states, fixture.known());
    auto prepared = fixture.stage->prepareResize(64, 48);
    REQUIRE(prepared);
    REQUIRE(HzbResizeTestAccess::current(*fixture.stage, *prepared));
    const auto replacement = HzbResizeTestAccess::replacement(*fixture.stage, *prepared);
    REQUIRE_FALSE(replacement[0].texture.bits != 0);
    REQUIRE_FALSE(replacement[1].texture.bits != 0);
    REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
    auto independentNoop = fixture.stage->prepareResize(64, 48);
    REQUIRE(independentNoop);
    fixture.stage->commitResize(std::move(*prepared), fixture.states);
    REQUIRE_FALSE(HzbResizeTestAccess::current(*fixture.stage, *prepared));
    REQUIRE(HzbResizeTestAccess::current(*fixture.stage, *independentNoop));
    REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
    REQUIRE(resizeRows(fixture.states, fixture.known()) == rows);
    fixture.stage->commitResize(std::move(*independentNoop), fixture.states);
    REQUIRE_FALSE(HzbResizeTestAccess::current(*fixture.stage, *independentNoop));
    REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
    REQUIRE(resizeRows(fixture.states, fixture.known()) == rows);
    REQUIRE((fixture.nextHistoryFlags() & 2u) != 0);
}

//======================================================================================================================
TEST_CASE("changed HZB commit replaces only its pair and preserves publication dependencies",
          "[render][device][hzb-resize][hzb-resize-commit]") {
    ResizeFixture fixture;
    auto retained = fixture.prime();
    fixture.prime();
    fixture.gpu->waitIdle();
    const auto old = HzbResizeTestAccess::textures(*fixture.stage);
    const auto generation = HzbResizeTestAccess::generation(*fixture.stage);
    const auto serial = HzbResizeTestAccess::serial(*fixture.stage);
    const auto publication = HzbResizeTestAccess::publication(*fixture.stage);
    const auto frontier = HzbResizeTestAccess::publicationFrontier(*fixture.stage);
    const auto allRows = resizeRows(fixture.states, fixture.known());
    const std::vector<ResizeRow> sentinelRows{allRows[0], allRows[1]};
    auto prepared = fixture.stage->prepareResize(96, 80);
    REQUIRE(prepared);
    const auto replacement = HzbResizeTestAccess::replacement(*fixture.stage, *prepared);
    fixture.stage->commitResize(std::move(*prepared), fixture.states);
    REQUIRE(HzbResizeTestAccess::generation(*fixture.stage) == generation + 1);
    REQUIRE(HzbResizeTestAccess::slotsReset(*fixture.stage));
    REQUIRE(HzbResizeTestAccess::serial(*fixture.stage) == serial);
    const std::array<uint64_t, 2> emptyDeclarations{};
    REQUIRE(HzbResizeTestAccess::declared(*fixture.stage) == emptyDeclarations);
    REQUIRE_FALSE(HzbResizeTestAccess::complete(*fixture.stage, 0));
    REQUIRE_FALSE(HzbResizeTestAccess::complete(*fixture.stage, 1));
    REQUIRE_FALSE(fixture.stage->previousSource().built);
    REQUIRE(HzbResizeTestAccess::borrowCount(*fixture.stage) == 0);
    REQUIRE(HzbResizeTestAccess::publication(*fixture.stage) == publication);
    REQUIRE(HzbResizeTestAccess::published(*fixture.stage));
    const auto afterFrontier = HzbResizeTestAccess::publicationFrontier(*fixture.stage);
    REQUIRE(afterFrontier.stage == frontier.stage);
    REQUIRE(afterFrontier.access == frontier.access);
    REQUIRE(afterFrontier.layout == frontier.layout);
    REQUIRE(frontier.access == rojoRHI::Access::ShaderWrite);
    const auto installed = HzbResizeTestAccess::textures(*fixture.stage);
    for (uint32_t slot = 0; slot < installed.size(); ++slot) {
        REQUIRE(installed[slot].texture == replacement[slot].texture);
        REQUIRE(installed[slot].texture != old[0].texture);
        REQUIRE(installed[slot].texture != old[1].texture);
        REQUIRE_FALSE(fixture.states.logicalUse(old[slot].texture));
        REQUIRE(fixture.states.physicalRanges(old[slot].texture, {}).empty());
    }
    REQUIRE(resizeRows(fixture.states, fixture.known(false)) == sentinelRows);

    // A fresh graph imports creation facts, never the old pair's writer frontier.
    RenderGraph graph;
    graph.bindTextureStates(fixture.states);
    ComputePassDesc write;
    for (uint32_t slot = 0; slot < installed.size(); ++slot) {
        const auto handle = graph.importPersistentTexture(installed[slot], "lmx.test.newPyramid");
        write.textureWrites.push_back(handle);
        graph.exportTexture(nextVersion(handle));
    }
    graph.addComputePass("lmx.test.newPyramid.write", std::move(write), kNoWork);
    auto lowered = graph.lower(fixture.gpu->frameNumber() + 1);
    INFO(errorOf(lowered));
    REQUIRE(lowered);
    std::set<uint64_t> observed;
    for (const auto& barrier : textureBarriers(*lowered)) {
        const auto identity = lowered->resources[barrier.resource].texture.texture;
        if (identity == installed[0].texture || identity == installed[1].texture) {
            REQUIRE(barrier.from.layout == rojoRHI::Layout::Undefined);
            REQUIRE(barrier.from.stage == rojoRHI::Stage::None);
            REQUIRE(barrier.from.access == rojoRHI::Access::None);
            observed.insert(identity.bits);
        }
    }
    REQUIRE(observed.size() == 2);
    REQUIRE(resizeRows(fixture.states, fixture.known(false)) == sentinelRows);
}

//======================================================================================================================
TEST_CASE("HZB token provenance rejects actual foreign stale moved and consumed values",
          "[render][device][hzb-resize][hzb-resize-provenance]") {
    ResizeFixture first;
    ResizeFixture foreign;
    auto sibling = resizeStage(*first.gpu);
    TextureStateTable siblingStates;
    REQUIRE(sibling->resize(64, 48, siblingStates));
    const auto siblingBefore = HzbResizeTestAccess::snapshot(*sibling);
    auto candidate = first.stage->prepareResize(96, 80);
    auto stale = first.stage->prepareResize(128, 96);
    REQUIRE(candidate);
    REQUIRE(stale);
    const auto firstBefore = HzbResizeTestAccess::snapshot(*first.stage);
    const auto foreignBefore = HzbResizeTestAccess::snapshot(*foreign.stage);
    const auto firstRows = resizeRows(first.states, first.known());
    const auto foreignRows = resizeRows(foreign.states, foreign.known());
    REQUIRE_FALSE(HzbResizeTestAccess::current(*foreign.stage, *candidate));
    REQUIRE_FALSE(HzbResizeTestAccess::current(*sibling, *candidate));
    REQUIRE(HzbResizeTestAccess::snapshot(*sibling) == siblingBefore);
    PreparedHzbResize moved(std::move(*candidate));
    REQUIRE_FALSE(HzbResizeTestAccess::current(*first.stage, *candidate));
    REQUIRE(HzbResizeTestAccess::current(*first.stage, moved));
    auto destination = first.stage->prepareResize(160, 112);
    REQUIRE(destination);
    *destination = std::move(moved);
    // The moved-from token is the subject: it must no longer be current.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    REQUIRE_FALSE(HzbResizeTestAccess::current(*first.stage, moved));
    REQUIRE(HzbResizeTestAccess::current(*first.stage, *destination));
    REQUIRE(HzbResizeTestAccess::snapshot(*first.stage) == firstBefore);
    REQUIRE(HzbResizeTestAccess::snapshot(*foreign.stage) == foreignBefore);
    REQUIRE(resizeRows(first.states, first.known()) == firstRows);
    REQUIRE(resizeRows(foreign.states, foreign.known()) == foreignRows);
    first.gpu->waitIdle();
    first.stage->commitResize(std::move(*destination), first.states);
    const auto after = HzbResizeTestAccess::snapshot(*first.stage);
    const auto afterRows = resizeRows(first.states, first.known(false));
    REQUIRE_FALSE(HzbResizeTestAccess::current(*first.stage, *destination));
    REQUIRE_FALSE(HzbResizeTestAccess::current(*first.stage, *stale));
    REQUIRE_FALSE(HzbResizeTestAccess::current(*foreign.stage, *stale));
    REQUIRE(HzbResizeTestAccess::snapshot(*first.stage) == after);
    REQUIRE(resizeRows(first.states, first.known(false)) == afterRows);
    REQUIRE(HzbResizeTestAccess::snapshot(*foreign.stage) == foreignBefore);
    REQUIRE(resizeRows(foreign.states, foreign.known()) == foreignRows);
}

//======================================================================================================================
TEST_CASE("initial HZB immediate resize remains valid inside a frame before its first graph",
          "[render][device][hzb-resize][hzb-resize-lazy-frame]") {
    auto gpu = createTestGpu();
    std::unique_ptr<HzbStage> stage;
    ResizeIdle idle{*gpu};
    TextureStateTable states;
    {
        ResizeFrame frame(*gpu);
        stage = resizeStage(*gpu);
        REQUIRE(stage->resize(64, 48, states));
        REQUIRE(HzbResizeTestAccess::generation(*stage) == 1);
        REQUIRE(states.size() == 0);
        {
            RenderGraph graph;
            graph.bindTextureStates(states);
            auto binding = stage->beginGraph(graph, frame.frame.commands);
            REQUIRE(stage->importPrevious(*binding));
        }
    }
}

//======================================================================================================================
TEST_CASE("closed unexecuted graphs permit HZB replacement while safe observer copies survive",
          "[render][device][hzb-resize][hzb-resize-graph-closure]") {
    ResizeFixture fixture;
    std::shared_ptr<HzbHistoryBinding> retained;
    {
        ResizeFrame frame(*fixture.gpu);
        {
            RenderGraph graph;
            graph.bindTextureStates(fixture.states);
            auto binding = fixture.stage->beginGraph(graph, frame.frame.commands);
            retained = fixture.stage->importPrevious(*binding);
            const auto input =
                graph.importTexture(fixture.depth.ref(), "lmx.test.unexecuted.depth");
            fixture.stage->declare(
                *binding, input,
                {.frameNumber = frame.frame.number, .activeWidth = 64, .activeHeight = 48});
            REQUIRE(fixture.stage->previousSource().built);
            const auto before = HzbResizeTestAccess::snapshot(*fixture.stage);
            const auto rows = resizeRows(fixture.states, fixture.known());
            {
                auto abandoned = fixture.stage->prepareResize(96, 80);
                REQUIRE(abandoned);
                REQUIRE(HzbResizeTestAccess::current(*fixture.stage, *abandoned));
                REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
            }
            REQUIRE(HzbResizeTestAccess::snapshot(*fixture.stage) == before);
            REQUIRE(resizeRows(fixture.states, fixture.known()) == rows);
        } // Closure is explicit; GPU idle alone would not destroy this graph or its callbacks.
    }
    fixture.gpu->waitIdle();
    const auto ownedSource = retained->source();
    const auto ownedParams = retained->recordingParams();
    REQUIRE(HzbResizeTestAccess::borrowCount(*fixture.stage) == 1);
    REQUIRE_FALSE(HzbResizeTestAccess::complete(*fixture.stage, 0));
    auto prepared = fixture.stage->prepareResize(96, 80);
    REQUIRE(prepared);
    fixture.stage->commitResize(std::move(*prepared), fixture.states);
    REQUIRE(retained->source().frameNumber == ownedSource.frameNumber);
    REQUIRE(retained->source().built == ownedSource.built);
    REQUIRE(retained->recordingParams().flags == ownedParams.flags);
    REQUIRE_FALSE(fixture.stage->previousSource().built);
    REQUIRE(HzbResizeTestAccess::borrowCount(*fixture.stage) == 0);
}
} // namespace
