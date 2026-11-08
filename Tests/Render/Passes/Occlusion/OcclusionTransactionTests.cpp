#include "Render/Passes/Visibility/VisibilityRetirement.h"
#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include <array>
#include <cstring>
#include <string_view>

namespace {
using namespace lmx;
using namespace lmx::render;

//======================================================================================================================
engine::GpuBuffer transactionBuffer(engine::GpuContext& gpu, uint64_t bytes,
                                    rojoRHI::Memory memory) {
    auto buffer = engine::GpuBuffer::create(
        gpu.device(), {.size = bytes, .memory = memory, .label = "lmx.test.occlusion.transaction"});
    REQUIRE(buffer);
    std::memset(buffer->cpuPointer(), 0, bytes);
    return std::move(*buffer);
}

struct OcclusionTransactionFixture {
    std::unique_ptr<engine::GpuContext> gpu = createTestGpu();
    engine::GpuBuffer rows = transactionBuffer(*gpu, 4, rojoRHI::Memory::Upload);
    engine::GpuBuffer arguments =
        transactionBuffer(*gpu, sizeof(rojoRHI::DrawIndexedIndirectArgs), rojoRHI::Memory::Upload);
    std::vector<VisibilityPendingReadback> production;
    std::vector<VisibilityStatus> retiredProduction;
    std::vector<ReferencePendingReadback> reference, retiredReference;
    OcclusionCheckHistory history;

    //==================================================================================================================
    void record(uint64_t generation) {
        auto backing = std::make_shared<VisibilitySlotBacking>();
        backing->states = transactionBuffer(*gpu, 4, rojoRHI::Memory::Upload);
        backing->counters = transactionBuffer(*gpu, sizeof(uint32_t) * 2 * kVisibilityCounterWords,
                                              rojoRHI::Memory::Upload);
        auto referenceBacking = std::make_shared<ReferenceBacking>();
        referenceBacking->buffer = transactionBuffer(*gpu, 256, rojoRHI::Memory::Readback);
        referenceBacking->capacity = 256;
        referenceBacking->buffer.cpuPointer()[0] = std::byte{1};
        auto frame = gpu->beginFrame();
        auto association =
            std::make_shared<OcclusionDeclarationAssociation>(*gpu, frame.number, generation);
        VisibilityPendingReadback pending;
        pending.frameNumber = frame.number;
        pending.status.frameNumber = frame.number;
        pending.status.sceneGeneration = generation;
        pending.status.occlusionCheckEnabled = true;
        pending.status.occlusionAssociation = association;
        pending.status.scene.candidates.push_back({.instanceIdentity = 1,
                                                   .instanceRow = 0,
                                                   .state = VisibilityState::Rejected,
                                                   .reason = VisibilityReason::Occluded});
        pending.rowBuffer = rows.rhi();
        pending.argumentBuffer = arguments.rhi();
        pending.backing = backing;
        pending.tickets = std::make_shared<VisibilityReadbackTickets>();
        const SyncPoint none{rojoRHI::Stage::None, rojoRHI::Access::None,
                             rojoRHI::Layout::Undefined};
        pending.tickets->counters = recordReadback(*gpu, frame.commands, backing->counters, none,
                                                   wholeBuffer(backing->counters));
        pending.tickets->states =
            recordReadback(*gpu, frame.commands, backing->states, none, {0, 0});
        pending.tickets->rows = recordReadback(*gpu, frame.commands, rows, none, {0, 0});
        pending.tickets->arguments = recordReadback(*gpu, frame.commands, arguments, none, {0, 0});
        ReferencePendingReadback direct;
        direct.frameNumber = frame.number;
        direct.sceneGeneration = generation;
        direct.width = direct.height = 1;
        direct.rowPitch = 256;
        direct.association = association;
        direct.backing = referenceBacking;
        direct.observations.push_back({1, 0});
        *direct.ticket =
            recordReadback(*gpu, frame.commands, referenceBacking->buffer, none, {0, 256});
        gpu->submitAndEndFrame(frame);
        production.push_back(std::move(pending));
        reference.push_back(std::move(direct));
    }
};
} // namespace

//======================================================================================================================
TEST_CASE("a later reference error preserves production reference and complete recovery history",
          "[render][device][occlusion-transaction]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("retirement transaction fixture requires the configured Null backend");
    OcclusionTransactionFixture fixture;
    fixture.record(41);
    fixture.record(42);
    *fixture.reference[1].ticket =
        makeReadbackTicket(Error{ErrorCode::Io, "reference causal readback error"});
    const auto firstAssociation = fixture.production[0].status.occlusionAssociation;
    const auto firstBacking = fixture.reference[0].backing;
    fixture.retiredProduction.emplace_back();
    fixture.retiredProduction.back().sceneGeneration = 99;
    fixture.retiredReference.emplace_back();
    fixture.retiredReference.back().sceneGeneration = 88;
    const std::array<OcclusionCheckObservation, 1> missing{{{1, 0, 1, true}}};
    fixture.history.observe(100, 7, false, missing);
    const auto result = takeRetiredVisibilityWithReference(
        *fixture.gpu, fixture.production, fixture.retiredProduction, &fixture.reference,
        &fixture.retiredReference, &fixture.history, fixture.gpu->frameNumber());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::Io);
    REQUIRE(result.error().message == "reference causal readback error");
    CHECK(fixture.production.size() == 2);
    CHECK(fixture.retiredProduction.size() == 1);
    CHECK(fixture.reference.size() == 2);
    CHECK(fixture.retiredReference.size() == 1);
    if (!fixture.production.empty()) {
        CHECK(fixture.production[0].status.occlusionAssociation == firstAssociation);
        CHECK_FALSE(fixture.production[0].status.isRetired);
        CHECK_FALSE(fixture.production[0].tickets->discarded);
    }
    if (!fixture.reference.empty())
        CHECK(fixture.reference[0].backing == firstBacking);
    const auto next = fixture.history.observe(101, 7, false, missing);
    CHECK(next.maximumMissingStreak == 2);
}

#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Passes/Visibility/GpuVisibility.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace lmx;
using namespace lmx::render;

// Value snapshots below serialize actual production types; no retirement or history logic is
// reimplemented. GPU ABI leaves are copied byte-for-byte; owning diagnostics are walked deeply.
template <typename... Values>
//======================================================================================================================
void txScalars(std::ostringstream& stream, Values... values) {
    ((stream << static_cast<uint64_t>(values) << ':'), ...);
}

template <typename T>
//======================================================================================================================
void txAbi(std::ostringstream& stream, std::span<const T> values) {
    static_assert(std::is_trivially_copyable_v<T>);
    txScalars(stream, values.size());
    for (const auto byte : std::as_bytes(values))
        txScalars(stream, std::to_integer<uint8_t>(byte));
}

//======================================================================================================================
void txCheck(std::ostringstream& stream, const OcclusionCheckResult& value) {
    txScalars(stream, value.enabled, value.strict, value.frameNumber, value.sceneGeneration,
              value.visibleInstances, value.falselyRejectedInstances, value.falselyRejectedPixels,
              value.invalidReferencePixels, value.unmatchedCandidates, value.maximumMissingStreak,
              value.missing.size());
    for (const auto& item : value.missing)
        txScalars(stream, item.instanceIdentity, item.instanceRow, item.visiblePixels,
                  item.consecutiveFrames);
}

//======================================================================================================================
void txCandidate(std::ostringstream& stream, const InstanceVisibility& value) {
    txScalars(stream, value.instanceIdentity, value.instanceRow, value.state, value.reason,
              value.occlusion.level, std::bit_cast<uint32_t>(value.occlusion.zBox),
              value.occlusion.outcome, value.occlusion.occluded);
    for (const auto coordinate : value.occlusion.rectangle)
        txScalars(stream, coordinate);
    for (glm::length_t axis = 0; axis < 3; ++axis)
        txScalars(stream, std::bit_cast<uint32_t>(value.worldBounds.minimum[axis]),
                  std::bit_cast<uint32_t>(value.worldBounds.maximum[axis]));
}

//======================================================================================================================
void txVisibility(std::ostringstream& stream, const VisibilityResult& value) {
    txScalars(stream, value.visible, value.rejected, value.disabled, value.candidates.size(),
              value.visibleItems.size());
    for (const auto count : value.bypassed)
        txScalars(stream, count);
    for (const auto index : value.visibleItems)
        txScalars(stream, index);
    for (const auto& candidate : value.candidates)
        txCandidate(stream, candidate);
}

//======================================================================================================================
void txCounters(std::ostringstream& stream, const VisibilityCounters& value) {
    txScalars(stream, value.occluded, value.occlusionTested, value.historyInvalid,
              value.nearCrossing, value.outsideSource, value.rectTooLarge, value.candidates,
              value.visible, value.rejected, value.disabled, value.emittedRows,
              value.emittedCommands, value.overflowedRows, value.overflowedCommands);
    for (const auto count : value.bypassed)
        txScalars(stream, count);
}

//======================================================================================================================
void txStatus(std::ostringstream& stream, const VisibilityStatus& value) {
    txScalars(stream, reinterpret_cast<uintptr_t>(value.occlusionAssociation.get()),
              value.occlusionEnabled, value.occlusionCheckEnabled, value.occlusionInvalidReason,
              value.occlusionSourceFrame, value.pyramidBytes, value.classifyMode, value.isRetired,
              value.overflow, value.checkEnabled, value.stateMismatches, value.rowMismatches,
              value.argumentMismatches, value.counterMismatches, value.frameNumber,
              value.sceneGeneration, std::bit_cast<uint64_t>(value.classifyMs),
              std::bit_cast<uint64_t>(value.prepareMs));
    txAbi(stream, std::span<const OcclusionParams>(&value.occlusionParams, 1));
    txCheck(stream, value.occlusionCheck);
    txCounters(stream, value.sceneCounters);
    txCounters(stream, value.shadowCounters);
    txVisibility(stream, value.scene);
    txVisibility(stream, value.shadow);
    const auto& stats = value.submission;
    txScalars(stream, stats.sceneCommands, stats.shadowCommands, stats.instancedRuns,
              stats.listBytes, stats.argumentBytes, stats.allocatedListBytes,
              stats.allocatedArgumentBytes, stats.allocatedUploadBytes, stats.pendingReleaseBuffers,
              stats.pendingReleaseUploadBuffers, stats.candidateBytes, stats.runBytes,
              stats.chunkBytes, stats.stateBytes, stats.counterBytes, stats.growthEvents);
}

//======================================================================================================================
void txSync(std::ostringstream& stream, const SyncPoint& value) {
    txScalars(stream, value.stage, value.access, value.layout);
}

//======================================================================================================================
void txTicket(std::ostringstream& stream, engine::GpuContext& gpu,
              const std::optional<ReadbackTicket>& value) {
    txScalars(stream, value.has_value());
    if (!value)
        return;
    txScalars(stream, value->buffer.bits, value->texture.bits, value->frame, value->region.offset,
              value->region.size);
    std::vector<uint8_t> bytes(value->region.size);
    const auto result = resolve(gpu, *value, bytes);
    txScalars(stream, result.has_value());
    if (!result) {
        txScalars(stream, result.error().code);
        stream << result.error().message.size() << ':' << result.error().message << ':';
    } else {
        txAbi(stream, std::span<const uint8_t>(bytes));
    }
}

//======================================================================================================================
void txBacking(std::ostringstream& stream, const std::shared_ptr<VisibilitySlotBacking>& value) {
    txScalars(stream, reinterpret_cast<uintptr_t>(value.get()));
    if (!value)
        return;
    txScalars(stream, value->candidates.rhi().bits, value->runs.rhi().bits,
              value->chunks.rhi().bits, value->views.rhi().bits, value->states.rhi().bits,
              value->counters.rhi().bits, value->capacity, value->used);
    txSync(stream, value->statesPhysical);
    txSync(stream, value->countersPhysical);
    for (const auto* bytes :
         {&value->candidateBytes, &value->runBytes, &value->chunkBytes, &value->viewBytes})
        txAbi(stream, std::span<const std::byte>(*bytes));
}

//======================================================================================================================
void txReference(std::ostringstream& stream, engine::GpuContext& gpu,
                 const ReferencePendingReadback& value) {
    txScalars(stream, value.frameNumber, value.sceneGeneration, value.width, value.height,
              value.rowPitch, value.strict, value.invalidPixels,
              reinterpret_cast<uintptr_t>(value.association.get()),
              reinterpret_cast<uintptr_t>(value.ticket.get()),
              reinterpret_cast<uintptr_t>(value.backing.get()), value.callbackAnchor.expired(),
              reinterpret_cast<uintptr_t>(value.recording.get()));
    if (value.recording)
        txScalars(stream, value.recording->entered, value.recording->discarded);
    if (value.backing) {
        txScalars(stream, value.backing->buffer.rhi().bits, value.backing->capacity,
                  value.backing->buffer.desc().size);
        txSync(stream, value.backing->physical);
        txAbi(stream, std::span<const std::byte>(value.backing->buffer.cpuPointer(),
                                                 value.backing->buffer.desc().size));
    }
    if (value.ticket)
        txTicket(stream, gpu, *value.ticket);
    txScalars(stream, value.observations.size());
    for (const auto& item : value.observations)
        txScalars(stream, item.instanceIdentity, item.instanceRow, item.visiblePixels,
                  item.occluded);
}

struct TxOwnerSnapshot {
    std::string production, reference;
    OcclusionCheckHistory history;
    size_t productionPending = 0, productionReady = 0;
    size_t referencePending = 0, referenceReady = 0;
    std::vector<std::shared_ptr<const DeclarationRecordingState>> referenceRecording;
};
} // namespace

namespace lmx::render {
struct OcclusionTransactionTestAccess {
    static TxOwnerSnapshot capture(GpuVisibility&, OcclusionReference&);
    static Result<void> declareReference(OcclusionReference&, RenderGraph&, rojoRHI::CommandList&,
                                         const SceneView&, const OcclusionReferenceInputs&);
    static ReadbackTicket deferReferenceError(OcclusionReference&, size_t);
    static void restoreRecordedReference(OcclusionReference&, size_t, ReadbackTicket);
    static void checkReferencePitchesAndLeases(engine::GpuContext&, OcclusionReference&,
                                               const std::vector<uint64_t>&);
};

//======================================================================================================================
TxOwnerSnapshot OcclusionTransactionTestAccess::capture(GpuVisibility& production,
                                                        OcclusionReference& reference) {
    std::ostringstream a, b;
    std::vector<std::shared_ptr<const DeclarationRecordingState>> referenceRecording;
    txScalars(a, reinterpret_cast<uintptr_t>(&production.m_gpu), production.m_epoch,
              production.m_pending.size(), production.m_retired.size());
    for (const auto& entry : production.m_pending) {
        txStatus(a, entry.status);
        txScalars(a, entry.frameNumber, entry.rowBuffer.bits, entry.argumentBuffer.bits,
                  reinterpret_cast<uintptr_t>(entry.projection.get()),
                  reinterpret_cast<uintptr_t>(entry.tickets.get()), entry.callbackAnchor.expired());
        if (entry.projection)
            txStatus(a, entry.projection->status);
        txAbi(a, std::span<const VisibilityParams>(&entry.params, 1));
        txAbi(a, std::span<const CandidateRecord>(entry.tables.candidates));
        txAbi(a, std::span<const RunRecord>(entry.tables.runs));
        txAbi(a, std::span<const ChunkRecord>(entry.tables.chunks));
        txAbi(a, std::span<const VisibilityViewParams>(entry.tables.views));
        txAbi(a, std::span<const rojoRHI::DrawIndexedIndirectArgs>(entry.geometry));
        txScalars(a, entry.expected.size());
        for (const auto& expected : entry.expected)
            txCandidate(a, expected);
        txBacking(a, entry.backing);
        if (entry.tickets) {
            txScalars(a, entry.tickets->discarded, entry.tickets->entered);
            txTicket(a, production.m_gpu, entry.tickets->counters);
            txTicket(a, production.m_gpu, entry.tickets->states);
            txTicket(a, production.m_gpu, entry.tickets->rows);
            txTicket(a, production.m_gpu, entry.tickets->arguments);
        }
    }
    for (const auto& status : production.m_retired)
        txStatus(a, status);
    for (const auto& slot : production.m_slots)
        txBacking(a, slot);
    txScalars(a, production.m_emptyInstances.rhi().bits, production.m_emptyMeshes.rhi().bits,
              production.m_capacityOverride.has_value());
    if (production.m_capacityOverride)
        for (const auto capacity : *production.m_capacityOverride)
            txScalars(a, capacity);
    txScalars(b, reinterpret_cast<uintptr_t>(&reference.m_gpu), reference.m_epoch,
              reference.m_pending.size(), reference.m_retired.size());
    for (const auto& entry : reference.m_pending) {
        txReference(b, reference.m_gpu, entry);
        referenceRecording.push_back(entry.recording);
    }
    for (const auto& entry : reference.m_retired)
        txReference(b, reference.m_gpu, entry);
    for (const auto& slot : reference.m_slots) {
        txScalars(b, reinterpret_cast<uintptr_t>(slot.get()));
        if (slot) {
            txScalars(b, slot->buffer.rhi().bits, slot->capacity, slot->buffer.desc().size);
            txSync(b, slot->physical);
        }
    }
    return {a.str(),
            b.str(),
            reference.m_history,
            production.m_pending.size(),
            production.m_retired.size(),
            reference.m_pending.size(),
            reference.m_retired.size(),
            std::move(referenceRecording)};
}

//======================================================================================================================
Result<void> OcclusionTransactionTestAccess::declareReference(
    OcclusionReference& reference, RenderGraph& graph, rojoRHI::CommandList& commands,
    const SceneView& view, const OcclusionReferenceInputs& inputs) {
    return reference.declareCombined(graph, commands, view, inputs);
}

//======================================================================================================================
ReadbackTicket OcclusionTransactionTestAccess::deferReferenceError(OcclusionReference& reference,
                                                                   size_t index) {
    REQUIRE(index < reference.m_pending.size());
    auto& target = reference.m_pending[index].ticket;
    REQUIRE(target);
    REQUIRE(*target);
    auto recorded = **target;
    *target = makeReadbackTicket(Error{ErrorCode::Io, "owned reference deferred causal error"});
    return recorded;
}

//======================================================================================================================
void OcclusionTransactionTestAccess::restoreRecordedReference(OcclusionReference& reference,
                                                              size_t index,
                                                              ReadbackTicket recorded) {
    REQUIRE(index < reference.m_pending.size());
    // This deliberate fixture intervention restores the retained REAL operation, not a synthetic
    // success or a production driver-healing path.
    *reference.m_pending[index].ticket = std::move(recorded);
}
} // namespace lmx::render

namespace {
struct TxMatrixFrame {
    engine::GpuContext& gpu;
    engine::Frame frame;
    bool closed = false;
    //==================================================================================================================
    explicit TxMatrixFrame(engine::GpuContext& owner) : gpu(owner), frame(owner.beginFrame()) {}
    //==================================================================================================================
    void close() {
        if (!closed) {
            gpu.submitAndEndFrame(frame);
            closed = true;
        }
    }
    //==================================================================================================================
    ~TxMatrixFrame() { close(); }
};

struct TxOwnerFixture {
    std::unique_ptr<engine::GpuContext> gpu = createTestGpu();
    std::unique_ptr<GpuVisibility> production;
    std::unique_ptr<OcclusionReference> reference;
    std::vector<std::unique_ptr<DrawSubmission>> submissions;
    std::vector<std::unique_ptr<TransientPool>> pools;
    SceneView view;
    //==================================================================================================================
    TxOwnerFixture() {
        auto p = GpuVisibility::create(*gpu);
        auto r = OcclusionReference::create(*gpu);
        REQUIRE(p);
        REQUIRE(r);
        production = std::move(*p);
        reference = std::move(*r);
        view.classifyMode = ClassifyMode::Gpu;
        view.temporal.sceneGeneration = 7;
    }
    //==================================================================================================================
    ~TxOwnerFixture() { gpu->waitIdle(); }
    //==================================================================================================================
    std::unique_ptr<RenderGraph> graph() {
        auto pool = std::make_unique<TransientPool>(*gpu);
        pool->beginFrame();
        auto result = std::make_unique<RenderGraph>(*pool);
        pools.push_back(std::move(pool));
        return result;
    }
    //==================================================================================================================
    GpuVisibilityOutputs declare(RenderGraph& graph, TxMatrixFrame& frame,
                                 std::shared_ptr<const OcclusionDeclarationAssociation> identity,
                                 bool checked = true) {
        auto submission = std::make_unique<DrawSubmission>(gpu->device());
        REQUIRE(submission->prepare(frame.frame.commands, frame.frame.number, view, {}, {}));
        VisibilityStatus status;
        status.frameNumber = frame.frame.number;
        status.sceneGeneration = 7;
        status.occlusionAssociation = std::move(identity);
        status.occlusionCheckEnabled = checked;
        status.classifyMode = ClassifyMode::Gpu;
        status.submission = submission->stats();
        const FrustumPlanes planes = extractFrustumPlanes(glm::mat4(1));
        auto result = production->declare(
            graph, frame.frame.commands, view,
            {.planes = planes,
             .submission = submission->prepared(),
             .rows = graph.importBuffer(*submission->scene().rows, "lmx.test.tx.rows"),
             .arguments = graph.importBuffer(*submission->scene().arguments, "lmx.test.tx.args")},
            status);
        REQUIRE(result);
        submissions.push_back(std::move(submission));
        return std::move(*result);
    }
    //==================================================================================================================
    void referenceFor(RenderGraph& graph, TxMatrixFrame& frame,
                      std::shared_ptr<const OcclusionDeclarationAssociation> identity,
                      bool strict = false, uint32_t width = 65) {
        REQUIRE(OcclusionTransactionTestAccess::declareReference(
            *reference, graph, frame.frame.commands, view,
            {.viewProjection = glm::mat4(1),
             .width = width,
             .height = 2,
             .association = std::move(identity),
             .strictView = strict}));
    }
    //==================================================================================================================
    void record(bool spare = false) {
        TxMatrixFrame frame(*gpu);
        {
            auto g = graph();
            auto identity = createOcclusionDeclarationAssociation(*gpu, frame.frame.number, 7);
            declare(*g, frame, identity);
            referenceFor(*g, frame, identity, true);
            if (spare)
                referenceFor(*g, frame,
                             createOcclusionDeclarationAssociation(*gpu, frame.frame.number, 7));
            g->execute(frame.frame.commands, frame.frame.number);
        }
        frame.close();
        gpu->waitIdle();
    }
};

//======================================================================================================================
void checkHistoryCopies(const OcclusionCheckHistory& before, const OcclusionCheckHistory& after,
                        uint64_t frame, uint64_t generation) {
    const std::array<OcclusionCheckObservation, 2> observations{
        {{1, 0, 7, true}, {2, 256, 11, true}}};
    for (const auto query : {frame, frame + 1, frame + 3}) {
        auto left = before;
        auto right = after;
        std::ostringstream a, b;
        txCheck(a, left.observe(query, generation, false, observations));
        txCheck(b, right.observe(query, generation, false, observations));
        CHECK(a.str() == b.str());
        txCheck(a, left.observe(query + 1, generation + 1, true, observations));
        txCheck(b, right.observe(query + 1, generation + 1, true, observations));
        CHECK(a.str() == b.str());
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("actual owner retirement preserves Ready projections observations and history on error",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    fixture.record(true);
    REQUIRE(VisibilityRetirementAccess::retire(*fixture.production, fixture.reference.get(),
                                               fixture.gpu->frameNumber()));
    fixture.record();
    fixture.record();
    const auto realOperation =
        OcclusionTransactionTestAccess::deferReferenceError(*fixture.reference, 1);
    const auto before =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    REQUIRE(before.productionPending == 2);
    REQUIRE(before.productionReady == 1);
    REQUIRE(before.referencePending == 2);
    REQUIRE(before.referenceReady == 1);
    const auto failed = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::Io);
    CHECK(failed.error().message == "owned reference deferred causal error");
    const auto after =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(after.production == before.production);
    CHECK(after.reference == before.reference);
    checkHistoryCopies(before.history, after.history, 3, 7);
    OcclusionTransactionTestAccess::restoreRecordedReference(*fixture.reference, 1, realOperation);
    const auto retried = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE(retried);
    REQUIRE(retried->size() == 3);
    for (const auto& status : *retried) {
        CHECK(status.isRetired);
        CHECK(status.occlusionCheck.enabled);
    }
    const auto noFallback = fixture.reference->check((*retried)[0]);
    REQUIRE(noFallback);
    CHECK_FALSE(*noFallback);
    const auto successful =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(successful.referenceReady == 1);
    const auto again = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE(again);
    CHECK(again->empty());
    const auto unchanged =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(unchanged.referenceReady == 1);
    checkHistoryCopies(successful.history, unchanged.history, 4, 7);
}

//======================================================================================================================
TEST_CASE("same-frame same-scene owners join exact opaque associations in opposite order",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    TxMatrixFrame frame(*fixture.gpu);
    auto first = fixture.graph();
    auto second = fixture.graph();
    const auto a = createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7);
    const auto b = createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7);
    REQUIRE(a != b);
    auto left = fixture.declare(*first, frame, a);
    auto right = fixture.declare(*second, frame, b);
    fixture.referenceFor(*second, frame, b, false, 65);
    fixture.referenceFor(*first, frame, a, true, 80);
    first->execute(frame.frame.commands, frame.frame.number);
    second->execute(frame.frame.commands, frame.frame.number);
    first.reset();
    second.reset();
    frame.close();
    fixture.gpu->waitIdle();
    const auto joined = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), frame.frame.number);
    REQUIRE(joined);
    REQUIRE(joined->size() == 2);
    CHECK((*joined)[0].occlusionAssociation == a);
    CHECK((*joined)[1].occlusionAssociation == b);
    CHECK((*joined)[0].occlusionCheck.strict);
    CHECK_FALSE((*joined)[1].occlusionCheck.strict);
    CHECK(left.projection->status.occlusionCheck.strict);
    CHECK_FALSE(right.projection->status.occlusionCheck.strict);
}

//======================================================================================================================
TEST_CASE("missing exact reference association fails without consuming either actual owner",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    TxMatrixFrame frame(*fixture.gpu);
    {
        auto graph = fixture.graph();
        fixture.declare(*graph, frame,
                        createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7));
        fixture.referenceFor(
            *graph, frame,
            createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7));
        graph->execute(frame.frame.commands, frame.frame.number);
    }
    frame.close();
    fixture.gpu->waitIdle();
    const auto before =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    const auto result = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), frame.frame.number);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    const auto after =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(before.production == after.production);
    CHECK(before.reference == after.reference);
    checkHistoryCopies(before.history, after.history, frame.frame.number, 7);
}

//======================================================================================================================
TEST_CASE("retained mapped causal retry advances the actual complete recovery history once",
          "[render][device][occlusion-matrix]") {
    OcclusionTransactionFixture fixture;
    fixture.record(7);
    auto initial = takeRetiredVisibilityWithReference(
        *fixture.gpu, fixture.production, fixture.retiredProduction, &fixture.reference,
        &fixture.retiredReference, &fixture.history, fixture.gpu->frameNumber());
    REQUIRE(initial);
    REQUIRE(initial->size() == 1);
    REQUIRE((*initial)[0].occlusionCheck.maximumMissingStreak == 1);
    fixture.record(7);
    fixture.record(7);
    const auto recorded = **fixture.reference[1].ticket;
    *fixture.reference[1].ticket =
        makeReadbackTicket(Error{ErrorCode::Io, "retry retains actual mapped operation"});
    const auto history = fixture.history;
    const auto observations = fixture.reference[0].observations;
    const auto projection = std::make_shared<VisibilityDeclarationProjection>();
    projection->status = fixture.production[0].status;
    fixture.production[0].projection = projection;
    const auto projected = projection->status;
    const auto tickets = fixture.production[0].tickets;
    const auto backing = fixture.reference[0].backing;
    const auto failed = takeRetiredVisibilityWithReference(
        *fixture.gpu, fixture.production, fixture.retiredProduction, &fixture.reference,
        &fixture.retiredReference, &fixture.history, fixture.gpu->frameNumber());
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::Io);
    CHECK(failed.error().message == "retry retains actual mapped operation");
    REQUIRE(fixture.production.size() == 2);
    REQUIRE(fixture.reference.size() == 2);
    CHECK(fixture.production[0].tickets == tickets);
    CHECK_FALSE(tickets->discarded);
    CHECK(fixture.reference[0].backing == backing);
    REQUIRE(fixture.reference[0].observations.size() == observations.size());
    for (size_t i = 0; i < observations.size(); ++i) {
        CHECK(fixture.reference[0].observations[i].visiblePixels == observations[i].visiblePixels);
        CHECK(fixture.reference[0].observations[i].occluded == observations[i].occluded);
    }
    std::ostringstream a, b;
    txStatus(a, projected);
    txStatus(b, projection->status);
    CHECK(a.str() == b.str());
    checkHistoryCopies(history, fixture.history, 2, 7);
    *fixture.reference[1].ticket = recorded;
    const auto retried = takeRetiredVisibilityWithReference(
        *fixture.gpu, fixture.production, fixture.retiredProduction, &fixture.reference,
        &fixture.retiredReference, &fixture.history, fixture.gpu->frameNumber());
    REQUIRE(retried);
    REQUIRE(retried->size() == 2);
    CHECK((*retried)[0].occlusionCheck.maximumMissingStreak == 2);
    CHECK((*retried)[1].occlusionCheck.maximumMissingStreak == 3);
    const auto committed = fixture.history;
    const auto again = takeRetiredVisibilityWithReference(
        *fixture.gpu, fixture.production, fixture.retiredProduction, &fixture.reference,
        &fixture.retiredReference, &fixture.history, fixture.gpu->frameNumber());
    REQUIRE(again);
    CHECK(again->empty());
    checkHistoryCopies(committed, fixture.history, 4, 7);
}

//======================================================================================================================
TEST_CASE("prepared actual owner batches abandon without publishing either producer",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    fixture.record();
    const auto before =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    {
        auto production = fixture.production->prepareRetiredTake(fixture.gpu->frameNumber());
        REQUIRE(production);
        REQUIRE(production->statuses().size() == 1);
        CHECK(production->statuses()[0].isRetired);
        auto reference =
            fixture.reference->prepareJoin(fixture.gpu->frameNumber(), production->statuses());
        REQUIRE(reference);
        const auto during =
            OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
        CHECK(during.production == before.production);
        CHECK(during.reference == before.reference);
        checkHistoryCopies(before.history, during.history, 2, 7);
    }
    const auto after =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(after.production == before.production);
    CHECK(after.reference == before.reference);
    checkHistoryCopies(before.history, after.history, 2, 7);
}

//======================================================================================================================
TEST_CASE("prepared owner tokens bind actual owner context mutation epoch move and consumption",
          "[render][device][occlusion-matrix][occlusion-matrix-null]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("forked misuse diagnostics require the configured Null backend");
    TxOwnerFixture fixture;
    fixture.record();
    auto production = fixture.production->prepareRetiredTake(fixture.gpu->frameNumber());
    REQUIRE(production);
    auto reference =
        fixture.reference->prepareJoin(fixture.gpu->frameNumber(), production->statuses());
    REQUIRE(reference);
    auto otherProduction = GpuVisibility::create(*fixture.gpu);
    auto otherReference = OcclusionReference::create(*fixture.gpu);
    REQUIRE(otherProduction);
    REQUIRE(otherReference);
    REQUIRE(abortDiagnostic([&] {
                (void)(*otherProduction)->commitRetiredTake(std::move(*production));
            }).find("foreign") != std::string::npos);
    REQUIRE(abortDiagnostic([&] {
                (*otherReference)->commitJoin(std::move(*reference));
            }).find("foreign") != std::string::npos);
    TxOwnerFixture foreign;
    REQUIRE(abortDiagnostic([&] {
                (void)VisibilityRetirementAccess::take(*fixture.production, foreign.reference.get(),
                                                       fixture.gpu->frameNumber());
            }).find("context") != std::string::npos);
    auto movedProduction = std::move(*production);
    auto movedReference = std::move(*reference);
    // The moved-from tokens are the subject: committing them must report consumption.
    REQUIRE(abortDiagnostic([&] {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                (void)fixture.production->commitRetiredTake(std::move(*production));
            }).find("consumed") != std::string::npos);
    REQUIRE(abortDiagnostic([&] {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                fixture.reference->commitJoin(std::move(*reference));
            }).find("consumed") != std::string::npos);
    fixture.reference->commitJoin(std::move(movedReference));
    const auto statuses = fixture.production->commitRetiredTake(std::move(movedProduction));
    REQUIRE(statuses.size() == 1);
    REQUIRE(abortDiagnostic([&] {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                (void)fixture.production->commitRetiredTake(std::move(movedProduction));
            }).find("consumed") != std::string::npos);
    REQUIRE(abortDiagnostic([&] {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                fixture.reference->commitJoin(std::move(movedReference));
            }).find("consumed") != std::string::npos);

    fixture.record();
    auto staleProduction = fixture.production->prepareRetiredTake(fixture.gpu->frameNumber());
    REQUIRE(staleProduction);
    auto staleReference =
        fixture.reference->prepareJoin(fixture.gpu->frameNumber(), staleProduction->statuses());
    REQUIRE(staleReference);
    fixture.record();
    REQUIRE(abortDiagnostic([&] {
                (void)fixture.production->commitRetiredTake(std::move(*staleProduction));
            }).find("stale") != std::string::npos);
    REQUIRE(abortDiagnostic([&] {
                fixture.reference->commitJoin(std::move(*staleReference));
            }).find("stale") != std::string::npos);
    const auto joined = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE(joined);
    CHECK(joined->size() == 2);
}

//======================================================================================================================
TEST_CASE("actual callbacks distinguish borrowed expired partial and recorded empty retirement",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    SECTION("automatic abandonment waits for callback-only anchor expiry") {
        TxMatrixFrame frame(*fixture.gpu);
        auto graph = fixture.graph();
        const auto declaration = fixture.declare(
            *graph, frame,
            createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7), false);
        const auto tickets = declaration.readbackIntent.lock();
        REQUIRE(tickets);
        frame.close();
        fixture.gpu->waitIdle();
        const auto borrowed = fixture.production->prepareRetiredTake(frame.frame.number);
        REQUIRE_FALSE(borrowed);
        CHECK(borrowed.error().code == ErrorCode::InvalidArgument);
        CHECK_FALSE(tickets->discarded);
        CHECK_FALSE(tickets->entered);
        graph.reset();
        auto expired = fixture.production->prepareRetiredTake(frame.frame.number);
        REQUIRE(expired);
        CHECK(expired->statuses().empty());
        const auto dropped = fixture.production->commitRetiredTake(std::move(*expired));
        CHECK(dropped.empty());
        CHECK(tickets->discarded);
    }
    SECTION("automatic reference abandonment also waits for the exact callback anchor") {
        TxMatrixFrame frame(*fixture.gpu);
        auto graph = fixture.graph();
        fixture.referenceFor(
            *graph, frame,
            createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7));
        frame.close();
        fixture.gpu->waitIdle();
        const auto borrowed = fixture.reference->prepareJoin(frame.frame.number, {});
        REQUIRE_FALSE(borrowed);
        CHECK(borrowed.error().code == ErrorCode::InvalidArgument);
        graph.reset();
        auto expired = fixture.reference->prepareJoin(frame.frame.number, {});
        REQUIRE(expired);
        fixture.reference->commitJoin(std::move(*expired));
        const auto after =
            OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
        CHECK(after.referencePending == 0);
        CHECK(after.referenceReady == 0);
    }
    SECTION("real reset prefix is entered incomplete work and cannot be discarded") {
        TxMatrixFrame frame(*fixture.gpu);
        std::shared_ptr<const VisibilityReadbackTickets> tickets;
        {
            auto graph = fixture.graph();
            const auto declaration = fixture.declare(
                *graph, frame,
                createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7), false);
            tickets = declaration.readbackIntent.lock();
            const auto recorded = HzbGraphTestAccess::recordThrough(
                *graph, frame.frame.commands, frame.frame.number, "lmx.pass.visibility.reset");
            REQUIRE(recorded);
            REQUIRE(*recorded == 1);
        }
        frame.close();
        fixture.gpu->waitIdle();
        REQUIRE(tickets);
        CHECK(tickets->entered);
        CHECK_FALSE(tickets->discarded);
        const auto partial = fixture.production->prepareRetiredTake(frame.frame.number);
        REQUIRE_FALSE(partial);
        CHECK(partial.error().code == ErrorCode::InvalidArgument);
        CHECK(partial.error().message.find("incomplete") != std::string::npos);
        CHECK_FALSE(tickets->discarded);
    }
    SECTION("an actual classify outcome with later channels missing is a Result failure") {
        TxMatrixFrame frame(*fixture.gpu);
        std::shared_ptr<const VisibilityReadbackTickets> tickets;
        {
            auto graph = fixture.graph();
            const auto declaration = fixture.declare(
                *graph, frame,
                createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7), false);
            tickets = declaration.readbackIntent.lock();
            const auto recorded = HzbGraphTestAccess::recordThrough(
                *graph, frame.frame.commands, frame.frame.number, "lmx.pass.visibility.classify");
            REQUIRE(recorded);
            REQUIRE(*recorded == 2);
        }
        frame.close();
        fixture.gpu->waitIdle();
        REQUIRE(tickets);
        REQUIRE(tickets->states);
        CHECK_FALSE(tickets->counters);
        CHECK(tickets->entered);
        CHECK_FALSE(tickets->discarded);
        const auto partial = fixture.production->prepareRetiredTake(frame.frame.number);
        REQUIRE_FALSE(partial);
        CHECK(partial.error().code == ErrorCode::InvalidArgument);
        CHECK(partial.error().message.find("incomplete") != std::string::npos);
        CHECK_FALSE(tickets->discarded);
    }
    SECTION("real raster prefix without its mapped copy is not zero reference background") {
        TxMatrixFrame frame(*fixture.gpu);
        {
            auto graph = fixture.graph();
            fixture.referenceFor(
                *graph, frame,
                createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7));
            const auto recorded = HzbGraphTestAccess::recordThrough(
                *graph, frame.frame.commands, frame.frame.number, "lmx.pass.occlusion.reference");
            REQUIRE(recorded);
            REQUIRE(*recorded == 1);
        }
        frame.close();
        fixture.gpu->waitIdle();
        const auto partial = fixture.reference->prepareJoin(frame.frame.number, {});
        REQUIRE_FALSE(partial);
        CHECK(partial.error().code == ErrorCode::InvalidArgument);
        CHECK(partial.error().message.find("incomplete") != std::string::npos);
    }
    SECTION("actual empty kernels and mapped reference copy remain recorded successes") {
        fixture.record();
        const auto result = VisibilityRetirementAccess::take(
            *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
        REQUIRE(result);
        REQUIRE(result->size() == 1);
        CHECK((*result)[0].isRetired);
        CHECK((*result)[0].occlusionCheck.enabled);
        CHECK((*result)[0].occlusionCheck.visibleInstances == 0);
        CHECK((*result)[0].occlusionCheck.invalidReferencePixels == 0);
    }
}

namespace lmx::render {
//======================================================================================================================
void OcclusionTransactionTestAccess::checkReferencePitchesAndLeases(
    engine::GpuContext& gpu, OcclusionReference& reference, const std::vector<uint64_t>& frames) {
    REQUIRE(reference.m_pending.size() == 4);
    REQUIRE(frames.size() == 4);
    const std::array<uint32_t, 4> widths{80, 64, 65, 65};
    const std::array<uint32_t, 4> pitches{512, 256, 512, 512};
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& entry = reference.m_pending[i];
        CHECK(entry.frameNumber == frames[i]);
        CHECK(entry.width == widths[i]);
        CHECK(entry.height == 2);
        CHECK(entry.rowPitch == pitches[i]);
        REQUIRE(entry.ticket);
        REQUIRE(*entry.ticket);
        CHECK((**entry.ticket).buffer == entry.backing->buffer.rhi());
        CHECK((**entry.ticket).region.size == uint64_t{pitches[i]} * 2);
        CHECK(entry.backing->physical.stage == rojoRHI::Stage::Copy);
        CHECK(entry.backing->physical.access == rojoRHI::Access::CopyWrite);
    }
    const auto old = reference.m_pending[0].backing;
    const auto replacement = reference.m_pending[3].backing;
    REQUIRE(old != replacement);
    CHECK(old->buffer.rhi() != replacement->buffer.rhi());
    CHECK(old->capacity == replacement->capacity);
    CHECK(reference.m_slots[0] == replacement);
    const auto oldTicket = **reference.m_pending[0].ticket;
    const auto replacementTicket = **reference.m_pending[3].ticket;
    // GPU completion is real. Only padding outside copied active pixels is test-owned data;
    // poisoning it checks the mapped decoder and keeps the two retained allocations distinct.
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& entry = reference.m_pending[i];
        auto* bytes = entry.backing->buffer.cpuPointer();
        if (entry.rowPitch > entry.width * 4)
            for (uint32_t y = 0; y < entry.height; ++y)
                std::memset(bytes + uint64_t{y} * entry.rowPitch + uint64_t{entry.width} * 4,
                            i == 0 ? 0x3d : 0x7e, entry.rowPitch - entry.width * 4);
    }
    REQUIRE(reference.retireThrough(frames.back()));
    CHECK(reference.m_pending.empty());
    REQUIRE(reference.m_retired.size() == 4);
    for (const auto& retired : reference.m_retired)
        CHECK(retired.invalidPixels == 0);
    std::vector<uint8_t> oldBytes(old->capacity), newBytes(replacement->capacity);
    REQUIRE(resolve(gpu, oldTicket, oldBytes));
    REQUIRE(resolve(gpu, replacementTicket, newBytes));
    CHECK(oldBytes[size_t{80} * 4] == 0x3d);
    CHECK(newBytes[size_t{65} * 4] == 0x7e);
}
} // namespace lmx::render

//======================================================================================================================
TEST_CASE("mapped reference rows ignore padding and retain observed owners at equal capacity",
          "[render][device][occlusion-matrix]") {
    TxOwnerFixture fixture;
    std::vector<uint64_t> frames;
    for (const uint32_t width : {80u, 64u, 65u, 65u}) {
        TxMatrixFrame frame(*fixture.gpu);
        {
            auto graph = fixture.graph();
            fixture.referenceFor(
                *graph, frame,
                createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7), false,
                width);
            graph->execute(frame.frame.commands, frame.frame.number);
        }
        frame.close();
        fixture.gpu->waitIdle();
        frames.push_back(frame.frame.number);
    }
    OcclusionTransactionTestAccess::checkReferencePitchesAndLeases(*fixture.gpu, *fixture.reference,
                                                                   frames);
}

#include "Engine/View/Camera.h"
#include "Support/SceneTableTestSupport.h"

#include <glm/gtc/constants.hpp>

//======================================================================================================================
TEST_CASE("native reference keeps real material and wire coverage across eight input variants",
          "[gpu][occlusion-matrix-native]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) == ".nulllib")
        SKIP("Null records commands; raster coverage requires a separately admitted real backend");
    auto gpu = createTestGpu();
    auto production = GpuVisibility::create(*gpu);
    auto reference = OcclusionReference::create(*gpu);
    REQUIRE(production);
    REQUIRE(reference);
    engine::MeshData quad{.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                                       {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                                       {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                                       {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
                          .indices = {0, 1, 2, 0, 2, 3}};
    auto mesh =
        test::fixtureMesh(gpu->device(), std::move(quad), "lmx.test.occlusionMatrix.native");
    REQUIRE(mesh);
    test::FixtureDrawItem item;
    item.mesh = &*mesh;
    test::FixtureSceneView authored;
    authored.items = std::span{&item, 1};
    engine::Camera camera;
    camera.position = {0, 0, 4};
    camera.fovY = glm::half_pi<float>();
    uint32_t variants = 0;
    for (const bool wire : {false, true})
        for (const bool masked : {false, true})
            for (const bool doubleSided : {false, true}) {
                CAPTURE(wire, masked, doubleSided);
                authored.wireframe = wire;
                item.material.alphaMode =
                    masked ? engine::AlphaMode::Mask : engine::AlphaMode::Opaque;
                item.material.doubleSided = doubleSided;
                item.material.albedo.a = 1;
                TxMatrixFrame frame(*gpu);
                auto view = authored.prepare(*gpu);
                view.classifyMode = ClassifyMode::Gpu;
                view.visibilityEnabled = false;
                DrawSubmission submission(gpu->device());
                REQUIRE(submission.prepare(frame.frame.commands, frame.frame.number, view, {}, {}));
                TransientPool pool(*gpu);
                pool.beginFrame();
                RenderGraph graph(pool);
                const uint32_t width = masked ? 65 : 64;
                const auto matrix =
                    camera.projectionMatrix(static_cast<float>(width) / 64) * camera.viewMatrix();
                const auto planes = extractFrustumPlanes(matrix);
                const auto identity = createOcclusionDeclarationAssociation(
                    *gpu, frame.frame.number, view.temporal.sceneGeneration);
                VisibilityStatus status;
                status.frameNumber = frame.frame.number;
                status.sceneGeneration = view.temporal.sceneGeneration;
                status.occlusionAssociation = identity;
                status.occlusionCheckEnabled = true;
                status.classifyMode = ClassifyMode::Gpu;
                status.submission = submission.stats();
                for (const auto& draw : view.items) {
                    const auto& row = view.tables.instanceRows[draw.instanceRow];
                    status.scene.candidates.push_back(
                        {.instanceIdentity = draw.instanceIdentity,
                         .instanceRow = draw.instanceRow,
                         .worldBounds = {row.worldBoundsMin, row.worldBoundsMax}});
                }
                status.shadow.candidates = status.scene.candidates;
                const auto declaration =
                    (*production)
                        ->declare(graph, frame.frame.commands, view,
                                  {.planes = planes,
                                   .submission = submission.prepared(),
                                   .instances = graph.importBuffer(*view.tables.instances,
                                                                   "lmx.test.native.instances"),
                                   .meshes = graph.importBuffer(*view.tables.meshes,
                                                                "lmx.test.native.meshes"),
                                   .rows = graph.importBuffer(*submission.scene().rows,
                                                              "lmx.test.native.rows"),
                                   .arguments = graph.importBuffer(*submission.scene().arguments,
                                                                   "lmx.test.native.arguments")},
                                  status);
                REQUIRE(declaration);
                REQUIRE(OcclusionTransactionTestAccess::declareReference(**reference, graph,
                                                                         frame.frame.commands, view,
                                                                         {.viewProjection = matrix,
                                                                          .width = width,
                                                                          .height = 64,
                                                                          .association = identity,
                                                                          .strictView = true}));
                graph.execute(frame.frame.commands, frame.frame.number);
                frame.close();
                gpu->waitIdle();
                const auto joined = VisibilityRetirementAccess::take(**production, reference->get(),
                                                                     frame.frame.number);
                REQUIRE(joined);
                REQUIRE(joined->size() == 1);
                const auto& check = (*joined)[0].occlusionCheck;
                CHECK((*joined)[0].isRetired);
                CHECK((*joined)[0].occlusionAssociation == identity);
                CHECK(check.enabled);
                CHECK(check.visibleInstances == 1);
                CHECK(check.invalidReferencePixels == 0);
                CHECK(check.unmatchedCandidates == 0);
                CHECK(check.falselyRejectedInstances == 0);
                CHECK(check.passed());
                ++variants;
            }
    CHECK(variants == 8);
    gpu->waitIdle();
}

namespace {
//======================================================================================================================
void checkExpiredBoundarySnapshot(const TxOwnerSnapshot& before, const TxOwnerSnapshot& after,
                                  uint64_t frame) {
    CHECK(after.production == before.production);
    CHECK(after.reference == before.reference);
    CHECK(after.productionPending == before.productionPending);
    CHECK(after.productionReady == before.productionReady);
    CHECK(after.referencePending == before.referencePending);
    CHECK(after.referenceReady == before.referenceReady);
    checkHistoryCopies(before.history, after.history, frame, 7);
}
} // namespace

//======================================================================================================================
TEST_CASE("expired actual owner discards stay unpublished during preparation and abandonment",
          "[render][device][occlusion-matrix][occlusion-boundary]") {
    TxOwnerFixture fixture;
    std::shared_ptr<const VisibilityReadbackTickets> intent;
    std::shared_ptr<VisibilityDeclarationProjection> projection;
    uint64_t expiredFrame = 0;
    {
        TxMatrixFrame frame(*fixture.gpu);
        expiredFrame = frame.frame.number;
        auto graph = fixture.graph();
        const auto identity =
            createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7);
        const auto declaration = fixture.declare(*graph, frame, identity);
        intent = declaration.readbackIntent.lock();
        projection = declaration.projection;
        fixture.referenceFor(*graph, frame, identity);
        graph.reset(); // Destroy the exact unentered callbacks, not their shared flag pointees.
        frame.close();
        fixture.gpu->waitIdle();
    }
    REQUIRE(intent);
    REQUIRE(projection);
    const auto before =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    REQUIRE(before.productionPending == 1);
    REQUIRE(before.referencePending == 1);
    REQUIRE(before.productionReady == 0);
    REQUIRE(before.referenceReady == 0);
    REQUIRE(before.referenceRecording.size() == 1);
    const auto referenceFlag = before.referenceRecording[0];
    REQUIRE(referenceFlag);
    CHECK_FALSE(intent->entered);
    CHECK_FALSE(intent->discarded);
    CHECK_FALSE(referenceFlag->entered);
    CHECK_FALSE(referenceFlag->discarded);
    CHECK_FALSE(projection->status.isRetired);
    {
        auto production = fixture.production->prepareRetiredTake(expiredFrame);
        REQUIRE(production);
        CHECK(production->statuses().empty());
        auto reference = fixture.reference->prepareJoin(expiredFrame, production->statuses());
        REQUIRE(reference);
        const auto during =
            OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
        checkExpiredBoundarySnapshot(before, during, expiredFrame + 1);
        CHECK_FALSE(intent->discarded);
        CHECK_FALSE(referenceFlag->discarded);
        CHECK_FALSE(projection->status.isRetired);
    }
    const auto abandoned =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    checkExpiredBoundarySnapshot(before, abandoned, expiredFrame + 1);
    CHECK_FALSE(intent->discarded);
    CHECK_FALSE(referenceFlag->discarded);
    const auto committed = VisibilityRetirementAccess::take(*fixture.production,
                                                            fixture.reference.get(), expiredFrame);
    REQUIRE(committed);
    CHECK(committed->empty());
    CHECK(intent->discarded);
    CHECK(referenceFlag->discarded);
    CHECK_FALSE(intent->entered);
    CHECK_FALSE(referenceFlag->entered);
    CHECK_FALSE(projection->status.isRetired);
    const auto after =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(after.productionPending == 0);
    CHECK(after.referencePending == 0);
    CHECK(after.productionReady == 0);
    CHECK(after.referenceReady == 0);
    checkHistoryCopies(before.history, after.history, expiredFrame + 1, 7);
    const auto again = VisibilityRetirementAccess::take(*fixture.production,
                                                        fixture.reference.get(), expiredFrame);
    REQUIRE(again);
    CHECK(again->empty());
    const auto repeated =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(repeated.productionPending == 0);
    CHECK(repeated.referencePending == 0);
    CHECK(repeated.productionReady == 0);
    CHECK(repeated.referenceReady == 0);
    CHECK(intent->discarded);
    CHECK(referenceFlag->discarded);
    checkHistoryCopies(after.history, repeated.history, expiredFrame + 1, 7);
}

//======================================================================================================================
TEST_CASE("expired actual intents remain untouched before a later reference error and retry",
          "[render][device][occlusion-matrix][occlusion-boundary]") {
    TxOwnerFixture fixture;
    fixture.record(true);
    REQUIRE(VisibilityRetirementAccess::retire(*fixture.production, fixture.reference.get(),
                                               fixture.gpu->frameNumber()));
    std::shared_ptr<const VisibilityReadbackTickets> intent;
    std::shared_ptr<VisibilityDeclarationProjection> projection;
    {
        TxMatrixFrame frame(*fixture.gpu);
        auto graph = fixture.graph();
        const auto identity =
            createOcclusionDeclarationAssociation(*fixture.gpu, frame.frame.number, 7);
        const auto declaration = fixture.declare(*graph, frame, identity);
        intent = declaration.readbackIntent.lock();
        projection = declaration.projection;
        fixture.referenceFor(*graph, frame, identity);
        graph.reset();
        frame.close();
        fixture.gpu->waitIdle();
    }
    REQUIRE(intent);
    REQUIRE(projection);
    const auto expired =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    REQUIRE(expired.referenceRecording.size() == 1);
    const auto referenceFlag = expired.referenceRecording[0];
    REQUIRE(referenceFlag);
    CHECK_FALSE(intent->discarded);
    CHECK_FALSE(referenceFlag->discarded);
    CHECK_FALSE(intent->entered);
    CHECK_FALSE(referenceFlag->entered);
    std::ostringstream savedProjection;
    txStatus(savedProjection, projection->status);
    // Both later pairs enter the real callbacks and retain their actual recorded copy operations.
    fixture.record();
    fixture.record();
    const auto realOperation =
        OcclusionTransactionTestAccess::deferReferenceError(*fixture.reference, 2);
    const auto before =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    REQUIRE(before.productionPending == 3);
    REQUIRE(before.referencePending == 3);
    REQUIRE(before.productionReady == 1);
    REQUIRE(before.referenceReady == 1);
    REQUIRE(before.referenceRecording.size() == 3);
    CHECK(before.referenceRecording[0] == referenceFlag);
    const auto failed = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::Io);
    CHECK(failed.error().message == "owned reference deferred causal error");
    const auto after =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    checkExpiredBoundarySnapshot(before, after, fixture.gpu->frameNumber() + 1);
    CHECK_FALSE(intent->discarded);
    CHECK_FALSE(referenceFlag->discarded);
    CHECK_FALSE(intent->entered);
    CHECK_FALSE(referenceFlag->entered);
    std::ostringstream failedProjection;
    txStatus(failedProjection, projection->status);
    CHECK(failedProjection.str() == savedProjection.str());
    OcclusionTransactionTestAccess::restoreRecordedReference(*fixture.reference, 2, realOperation);
    const auto retried = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE(retried);
    REQUIRE(retried->size() == 3);
    for (const auto& status : *retried) {
        CHECK(status.isRetired);
        CHECK(status.occlusionCheck.enabled);
    }
    CHECK(intent->discarded);
    CHECK(referenceFlag->discarded);
    CHECK_FALSE(intent->entered);
    CHECK_FALSE(referenceFlag->entered);
    std::ostringstream discardedProjection;
    txStatus(discardedProjection, projection->status);
    CHECK(discardedProjection.str() == savedProjection.str());
    const auto successful =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(successful.productionPending == 0);
    CHECK(successful.referencePending == 0);
    CHECK(successful.productionReady == 0);
    CHECK(successful.referenceReady == 1);
    const auto again = VisibilityRetirementAccess::take(
        *fixture.production, fixture.reference.get(), fixture.gpu->frameNumber());
    REQUIRE(again);
    CHECK(again->empty());
    const auto repeated =
        OcclusionTransactionTestAccess::capture(*fixture.production, *fixture.reference);
    CHECK(repeated.productionPending == 0);
    CHECK(repeated.referencePending == 0);
    CHECK(repeated.referenceReady == 1);
    CHECK(intent->discarded);
    CHECK(referenceFlag->discarded);
    checkHistoryCopies(successful.history, repeated.history, fixture.gpu->frameNumber() + 1, 7);
}
