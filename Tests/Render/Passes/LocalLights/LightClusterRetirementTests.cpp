#include "Render/Passes/LocalLights/LightClusterReadback.h"

#include "Engine/View/Camera.h"
#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lmx;
using namespace lmx::render;
using light_detail::kCounterBytes;
using light_detail::kGridBytes;
using light_detail::kIndexBytes;
using light_detail::LightClusterReadback;

// How one outcome of a declaration was recorded, if at all.
enum class Outcome { Absent, Full, Empty, Short, Failed };

struct Declaration {
    bool captureLists = true;
    uint32_t assigned = 5;
    Outcome counters = Outcome::Full;
    Outcome grid = Outcome::Full;
    Outcome indices = Outcome::Full;
    uint64_t indexBytes = kIndexBytes; ///< The recorded index region when `indices` is Full.
};

// Distinct recognisable words per declaration, so a decode from the wrong source cannot pass.
//======================================================================================================================
std::array<uint32_t, kLightClusterCounterWords> counterWords(uint32_t seed, uint32_t assigned) {
    return {seed + 7, assigned, seed + 1, seed + 2, seed + 3, seed + 4, 0xdeadbeefu, 0xfeedfaceu};
}

//======================================================================================================================
uint32_t indexWord(uint32_t seed, uint32_t entry) {
    return seed * 1000u + entry;
}

//======================================================================================================================
ClusterRecord gridRecord(uint32_t seed, uint32_t froxel) {
    return {.offset = seed + froxel, .count = froxel % 7};
}

struct RetirementFixture {
    std::unique_ptr<engine::GpuContext> gpu = test::createTestGpu();
    // Mapped sources stay alive and unmodified until their tickets resolve.
    std::vector<engine::GpuBuffer> sources;

    //==================================================================================================================
    const engine::GpuBuffer& source(std::span<const std::byte> bytes) {
        auto result = engine::GpuBuffer::create(gpu->device(),
                                                {.size = bytes.size(),
                                                 .memory = rojoRHI::Memory::Upload,
                                                 .label = "lmx.test.lightCluster.retirement"},
                                                bytes);
        REQUIRE(result);
        sources.push_back(std::move(*result));
        return sources.back();
    }

    //==================================================================================================================
    std::optional<ReadbackTicket> record(rojoRHI::CommandList& commands, Outcome outcome,
                                         std::span<const std::byte> bytes, uint64_t full,
                                         uint64_t shortBytes, const char* failure) {
        const SyncPoint none{rojoRHI::Stage::None, rojoRHI::Access::None,
                             rojoRHI::Layout::Undefined};
        switch (outcome) {
        case Outcome::Absent:
            return std::nullopt;
        case Outcome::Failed:
            return makeReadbackTicket(Error{ErrorCode::Io, failure});
        case Outcome::Empty:
            return recordReadback(*gpu, commands, source(bytes), none, {0, 0});
        case Outcome::Short:
            return recordReadback(*gpu, commands, source(bytes), none, {0, shortBytes});
        case Outcome::Full:
            return recordReadback(*gpu, commands, source(bytes), none, {0, full});
        }
        return std::nullopt;
    }

    // Records one declaration's outcomes in a real frame, through the shared outcome set the
    // stage's graph callbacks write, and returns that frame.
    //==================================================================================================================
    uint64_t declare(LightClusterReadback& readback, const Declaration& declaration) {
        auto frame = gpu->beginFrame();
        const auto seed = static_cast<uint32_t>(frame.number);
        const auto tickets = readback.enqueue(frame.number, declaration.captureLists);
        const auto words = counterWords(seed, declaration.assigned);
        std::vector<ClusterRecord> grid(kClusterCount);
        for (uint32_t froxel = 0; froxel < kClusterCount; ++froxel)
            grid[froxel] = gridRecord(seed, froxel);
        std::vector<uint32_t> indices(kLightClusterIndexCapacity);
        for (uint32_t entry = 0; entry < indices.size(); ++entry)
            indices[entry] = indexWord(seed, entry);
        tickets->counters =
            record(frame.commands, declaration.counters, std::as_bytes(std::span(words)),
                   kCounterBytes, kCounterBytes - 4, "counter staging failed");
        tickets->grid =
            record(frame.commands, declaration.grid, std::as_bytes(std::span(grid)), kGridBytes,
                   kGridBytes - sizeof(ClusterRecord), "grid staging failed");
        tickets->indices =
            record(frame.commands, declaration.indices, std::as_bytes(std::span(indices)),
                   declaration.indexBytes, (std::max(declaration.assigned, 1u) - 1) * 4ull,
                   "index staging failed");
        gpu->submitAndEndFrame(frame);
        return frame.number;
    }
};

//======================================================================================================================
void requireDecoded(const RetiredLightClusters& retired, uint64_t frame, bool captureLists,
                    uint32_t assigned) {
    const auto seed = static_cast<uint32_t>(frame);
    REQUIRE(retired.frameNumber == frame);
    REQUIRE(retired.counters.candidates == seed + 7);
    REQUIRE(retired.counters.assigned == assigned);
    REQUIRE(retired.counters.droppedPerCluster == seed + 1);
    REQUIRE(retired.counters.droppedGlobal == seed + 2);
    REQUIRE(retired.counters.truncatedFroxels == seed + 3);
    REQUIRE(retired.counters.maxCount == seed + 4);
    if (!captureLists) {
        REQUIRE(retired.grid.empty());
        REQUIRE(retired.indices.empty());
        return;
    }
    REQUIRE(retired.grid.size() == kClusterCount);
    for (const uint32_t froxel : {0u, 1u, kClusterCount / 2, kClusterCount - 1}) {
        REQUIRE(retired.grid[froxel].offset == gridRecord(seed, froxel).offset);
        REQUIRE(retired.grid[froxel].count == gridRecord(seed, froxel).count);
    }
    REQUIRE(retired.indices.size() == assigned);
    for (uint32_t entry = 0; entry < assigned; ++entry)
        REQUIRE(retired.indices[entry] == indexWord(seed, entry));
}

//======================================================================================================================
void requireError(const Result<void>& result, ErrorCode code, std::string_view text) {
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == code);
    INFO(result.error().message);
    REQUIRE(result.error().message.find(text) != std::string::npos);
}
} // namespace

//======================================================================================================================
TEST_CASE("light-cluster retirement preserves causal errors and unconsumed frames",
          "[render][device]") {
    struct FailureCase {
        const char* name;
        Declaration failed;
        const char* message;
    };
    const std::array failures{
        FailureCase{
            "a recorded counter failure", {.counters = Outcome::Failed}, "counter staging failed"},
        FailureCase{"a recorded grid failure", {.grid = Outcome::Failed}, "grid staging failed"},
        FailureCase{"recorded counter and grid failures report the counters first",
                    {.counters = Outcome::Failed, .grid = Outcome::Failed},
                    "counter staging failed"},
        FailureCase{"a recorded index failure with a defined prefix",
                    {.indices = Outcome::Failed},
                    "index staging failed"},
        FailureCase{"a recorded index failure with an empty prefix",
                    {.assigned = 0, .indices = Outcome::Failed},
                    "index staging failed"},
        FailureCase{"a recorded list failure nobody asked to capture",
                    {.captureLists = false, .indices = Outcome::Failed},
                    "index staging failed"},
    };
    RetirementFixture fixture;
    for (const auto& failure : failures) {
        DYNAMIC_SECTION(failure.name) {
            LightClusterReadback readback;
            const auto first = fixture.declare(readback, {});
            const auto failed = fixture.declare(readback, failure.failed);
            const auto later = fixture.declare(readback, {.assigned = 9});
            // The first declaration commits before the failure; the failed and later ones stay
            // pending, and a repeat neither duplicates the first nor reports a different error.
            for (uint32_t repeat = 0; repeat < 2; ++repeat) {
                const auto result = readback.retireThrough(*fixture.gpu, later);
                REQUIRE_FALSE(result);
                REQUIRE(result.error().code == ErrorCode::Io);
                REQUIRE(result.error().message == failure.message);
                REQUIRE_FALSE(readback.hasPendingSlot(first));
                REQUIRE(readback.hasPendingSlot(failed));
                REQUIRE(readback.hasPendingSlot(later));
            }
            const auto retired = readback.takeRetired();
            REQUIRE(retired.size() == 1);
            requireDecoded(retired.front(), first, true, 5);
            REQUIRE(readback.takeRetired().empty());
            requireError(readback.retireThrough(*fixture.gpu, failed), ErrorCode::Io,
                         failure.message);
            REQUIRE(readback.hasPendingSlot(later));
            REQUIRE(readback.takeRetired().empty());
        }
    }
}

//======================================================================================================================
TEST_CASE("unexecuted light-cluster declarations never publish zero counters", "[render][device]") {
    RetirementFixture fixture;
    SECTION("no declaration leaves nothing pending") {
        LightClusterReadback readback;
        for (uint64_t frame = 0; frame < 3; ++frame)
            REQUIRE_FALSE(readback.hasPendingSlot(frame));
        REQUIRE(readback.retireThrough(*fixture.gpu, 1000));
        REQUIRE(readback.takeRetired().empty());
    }
    SECTION("a declaration after the completed frame stays pending") {
        LightClusterReadback readback;
        const auto completed = fixture.declare(readback, {});
        const auto later = fixture.declare(readback, {.assigned = 9});
        REQUIRE(readback.retireThrough(*fixture.gpu, completed));
        REQUIRE_FALSE(readback.hasPendingSlot(completed));
        REQUIRE(readback.hasPendingSlot(later));
        auto retired = readback.takeRetired();
        REQUIRE(retired.size() == 1);
        requireDecoded(retired.front(), completed, true, 5);
        REQUIRE(readback.retireThrough(*fixture.gpu, later));
        retired = readback.takeRetired();
        REQUIRE(retired.size() == 1);
        requireDecoded(retired.front(), later, true, 9);
    }
    SECTION("missing or incomplete counters are an error, not zeros") {
        for (const auto counters : {Outcome::Absent, Outcome::Empty, Outcome::Short}) {
            LightClusterReadback readback;
            const auto frame =
                fixture.declare(readback, {.captureLists = false, .counters = counters});
            const auto result = readback.retireThrough(*fixture.gpu, frame);
            if (counters == Outcome::Absent)
                requireError(result, ErrorCode::InvalidArgument, "counters readback was never");
            else
                requireError(result, ErrorCode::Parse, "counters readback holds");
            REQUIRE(readback.hasPendingSlot(frame));
            REQUIRE(readback.takeRetired().empty());
        }
    }
    SECTION("capture-disabled lists may be absent and are never published") {
        LightClusterReadback readback;
        const auto absent = fixture.declare(
            readback, {.captureLists = false, .grid = Outcome::Absent, .indices = Outcome::Absent});
        const auto present = fixture.declare(readback, {.captureLists = false});
        const auto empty = fixture.declare(
            readback, {.captureLists = false, .grid = Outcome::Empty, .indices = Outcome::Empty});
        REQUIRE(readback.retireThrough(*fixture.gpu, empty));
        const auto retired = readback.takeRetired();
        REQUIRE(retired.size() == 3);
        requireDecoded(retired[0], absent, false, 5);
        requireDecoded(retired[1], present, false, 5);
        requireDecoded(retired[2], empty, false, 5);
    }
    SECTION("captured lists require both recorded outcomes, even with nothing assigned") {
        for (const uint32_t assigned : {5u, 0u}) {
            LightClusterReadback missingGrid;
            const auto gridFrame =
                fixture.declare(missingGrid, {.assigned = assigned, .grid = Outcome::Absent});
            requireError(missingGrid.retireThrough(*fixture.gpu, gridFrame),
                         ErrorCode::InvalidArgument, "grid readback was never");
            REQUIRE(missingGrid.hasPendingSlot(gridFrame));
            LightClusterReadback missingIndices;
            const auto indexFrame =
                fixture.declare(missingIndices, {.assigned = assigned, .indices = Outcome::Absent});
            requireError(missingIndices.retireThrough(*fixture.gpu, indexFrame),
                         ErrorCode::InvalidArgument, "indices readback was never");
            REQUIRE(missingIndices.hasPendingSlot(indexFrame));
        }
    }
    SECTION("an empty prefix accepts a recorded empty or full index outcome") {
        LightClusterReadback readback;
        const auto empty = fixture.declare(readback, {.assigned = 0, .indices = Outcome::Empty});
        const auto full = fixture.declare(readback, {.assigned = 0});
        REQUIRE(readback.retireThrough(*fixture.gpu, full));
        const auto retired = readback.takeRetired();
        REQUIRE(retired.size() == 2);
        requireDecoded(retired[0], empty, true, 0);
        requireDecoded(retired[1], full, true, 0);
    }
    SECTION("empty or short outcomes cannot justify captured data") {
        const std::array<Declaration, 4> insufficient{
            Declaration{.grid = Outcome::Empty}, Declaration{.grid = Outcome::Short},
            Declaration{.indices = Outcome::Empty}, Declaration{.indices = Outcome::Short}};
        for (const auto& declaration : insufficient) {
            LightClusterReadback readback;
            const auto frame = fixture.declare(readback, declaration);
            requireError(readback.retireThrough(*fixture.gpu, frame), ErrorCode::Parse,
                         declaration.grid != Outcome::Full ? "grid readback holds"
                                                           : "indices readback holds");
            REQUIRE(readback.hasPendingSlot(frame));
            REQUIRE(readback.takeRetired().empty());
        }
        LightClusterReadback readback;
        const auto exact = fixture.declare(readback, {.indexBytes = 5 * sizeof(uint32_t)});
        REQUIRE(readback.retireThrough(*fixture.gpu, exact));
        const auto retired = readback.takeRetired();
        REQUIRE(retired.size() == 1);
        requireDecoded(retired.front(), exact, true, 5);
    }
}

//======================================================================================================================
TEST_CASE("an unexecuted light-cluster declaration is discarded and frees its slot",
          "[render][device]") {
    SECTION("a wholly unrecorded declaration is discarded and its slot recurs") {
        auto gpu = test::createTestGpu();
        auto stage = LightClusterStage::create(*gpu);
        INFO(test::errorOf(stage));
        REQUIRE(stage);
        const std::array<engine::LightRow, 1> rows{};
        auto lights = engine::GpuBuffer::create(gpu->device(),
                                                {.size = sizeof(rows),
                                                 .memory = rojoRHI::Memory::Upload,
                                                 .label = "lmx.test.lightCluster.discard.rows"},
                                                std::as_bytes(std::span(rows)));
        REQUIRE(lights);
        const engine::Camera camera;
        const auto declare = [&](RenderGraph& graph, rojoRHI::CommandList& commands,
                                 uint64_t frame) {
            const auto outputs = (*stage)->declare(
                graph, commands,
                {.clustered = true,
                 .liveLightCount = 1,
                 .rowCount = 1,
                 .lights = graph.importBuffer(*lights, "lmx.test.lightCluster.discard.rows"),
                 .view = camera.viewMatrix(),
                 .inverseJitteredProjection = glm::inverse(camera.projectionMatrix(16.0f / 9.0f)),
                 .sliceDepth = clusterSliceDepths(camera.nearZ),
                 .activeWidth = 16,
                 .activeHeight = 9,
                 .frameNumber = frame,
                 .captureLists = true});
            REQUIRE(outputs.declared);
        };

        // The graph is declared but never executed, as when compilation fails or the frame is
        // abandoned: retirement reports it as never recorded until it is discarded.
        auto frame = gpu->beginFrame();
        const uint64_t abandonedFrame = frame.number;
        RenderGraph abandoned;
        declare(abandoned, frame.commands, abandonedFrame);
        requireError((*stage)->retireThrough(abandonedFrame), ErrorCode::InvalidArgument,
                     "counters readback was never");
        (*stage)->discardUnrecorded(abandonedFrame);
        REQUIRE(abortDiagnostic([&] {
                    (*stage)->discardUnrecorded(abandonedFrame);
                }).find("pending declaration") != std::string::npos);
        gpu->submitAndEndFrame(frame);
        REQUIRE((*stage)->retireThrough(abandonedFrame));
        REQUIRE((*stage)->takeRetired().empty());

        // Later frames retire normally, and the third reuses the abandoned paced slot.
        uint64_t last = abandonedFrame;
        for (uint32_t step = 0; step < 4; ++step) {
            auto next = gpu->beginFrame();
            last = next.number;
            RenderGraph graph;
            declare(graph, next.commands, next.number);
            const auto compiled = graph.compile();
            if (!compiled)
                gpu->submitAndEndFrame(next);
            INFO((compiled ? std::string{} : compiled.error().message));
            REQUIRE(compiled);
            graph.execute(next.commands, next.number);
            gpu->submitAndEndFrame(next);
            gpu->waitIdle();
            REQUIRE((*stage)->retireThrough(last));
            const auto retired = (*stage)->takeRetired();
            REQUIRE(retired.size() == 1);
            REQUIRE(retired.front().frameNumber == last);
            REQUIRE(retired.front().grid.size() == kClusterCount);
        }
        REQUIRE(last >= abandonedFrame + 3);
    }
    SECTION("a partly recorded declaration cannot be discarded and keeps failing") {
        RetirementFixture fixture;
        LightClusterReadback readback;
        const auto frame = fixture.declare(
            readback,
            {.counters = Outcome::Failed, .grid = Outcome::Absent, .indices = Outcome::Absent});
        REQUIRE(abortDiagnostic([&] {
                    readback.discardUnrecorded(frame);
                }).find("wholly unrecorded") != std::string::npos);
        requireError(readback.retireThrough(*fixture.gpu, frame), ErrorCode::Io,
                     "counter staging failed");
        REQUIRE(readback.hasPendingSlot(frame));
        REQUIRE(abortDiagnostic([&] {
                    readback.discardUnrecorded(frame + 1);
                }).find("pending declaration") != std::string::npos);
    }
}
