#include "Render/Passes/Visibility/VisibilityRetirement.h"

#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Renderer/SceneView.h"
#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include <cstring>
#include <string_view>

namespace {
using namespace lmx;
using namespace lmx::render;

//======================================================================================================================
engine::GpuBuffer mappedBuffer(engine::GpuContext& gpu, uint64_t bytes) {
    auto result =
        engine::GpuBuffer::create(gpu.device(), {.size = bytes,
                                                 .memory = rojoRHI::Memory::Upload,
                                                 .label = "lmx.test.visibility.retirement"});
    REQUIRE(result);
    std::memset(result->cpuPointer(), 0, bytes);
    return std::move(*result);
}

// The fixture's hand-built entries borrow mapped Upload buffers recorded in otherwise empty frames.
// They exercise only retirement's host-side transaction -- staging, decoding and publication --
// which needs no GPU work, so the cases run once, on the Null configuration hosted CI builds,
// rather than repeating identical host logic on every device backend.
struct RetirementFixture {
    std::unique_ptr<engine::GpuContext> gpu = createTestGpu();
    engine::GpuBuffer rows = mappedBuffer(*gpu, 4);
    engine::GpuBuffer arguments = mappedBuffer(*gpu, sizeof(rojoRHI::DrawIndexedIndirectArgs));

    //==================================================================================================================
    VisibilityPendingReadback record(uint64_t generation) {
        auto backing = std::make_shared<VisibilitySlotBacking>();
        backing->states = mappedBuffer(*gpu, 4);
        backing->counters = mappedBuffer(*gpu, 2 * kVisibilityCounterWords * sizeof(uint32_t));
        backing->capacity = 1;
        auto frame = gpu->beginFrame();
        VisibilityPendingReadback pending;
        pending.status.frameNumber = frame.number;
        pending.status.sceneGeneration = generation;
        pending.frameNumber = frame.number;
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
        gpu->submitAndEndFrame(frame);
        return pending;
    }
};

//======================================================================================================================
std::optional<ReadbackTicket>& channel(VisibilityReadbackTickets& tickets, uint32_t index) {
    switch (index) {
    case 0:
        return tickets.counters;
    case 1:
        return tickets.states;
    case 2:
        return tickets.rows;
    default:
        return tickets.arguments;
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("visibility retirement preserves the entire completed batch on causal failure",
          "[render][device]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("transaction fixture requires the configured Null backend");
    for (uint32_t failed = 0; failed < 4; ++failed) {
        RetirementFixture fixture;
        std::vector<VisibilityPendingReadback> pending;
        pending.push_back(fixture.record(41));
        pending.push_back(fixture.record(42));
        pending.push_back(fixture.record(43));
        const auto completed = pending[1].frameNumber;
        auto& failedTicket = channel(*pending[1].tickets, failed);
        failedTicket = makeReadbackTicket(Error{ErrorCode::Io, "actual causal staging error"});
        const auto firstTickets = pending[0].tickets;
        const auto laterBacking = pending[2].backing;
        std::vector<VisibilityStatus> retired(1);
        retired[0].sceneGeneration = 7;
        for (uint32_t repeat = 0; repeat < 2; ++repeat) {
            const auto result = retireVisibilityThrough(*fixture.gpu, pending, retired, completed);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().code == ErrorCode::Io);
            REQUIRE(result.error().message == "actual causal staging error");
            REQUIRE(pending.size() == 3);
            REQUIRE(pending[0].tickets == firstTickets);
            REQUIRE(pending[2].backing == laterBacking);
            REQUIRE_FALSE(pending[0].status.isRetired);
            REQUIRE_FALSE(pending[0].tickets->discarded);
            REQUIRE(pending[0].status.sceneGeneration == 41);
            REQUIRE(pending[1].status.sceneGeneration == 42);
            REQUIRE(pending[2].status.sceneGeneration == 43);
            REQUIRE(retired.size() == 1);
            REQUIRE(retired[0].sceneGeneration == 7);
        }
    }
}

//======================================================================================================================
TEST_CASE("visibility retirement distinguishes unrecorded intent from recorded empty work",
          "[render][device]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("transaction fixture requires the configured Null backend");
    RetirementFixture fixture;
    for (uint32_t mask = 0; mask < 16; ++mask) {
        auto entry = fixture.record(mask);
        for (uint32_t bit = 0; bit < 4; ++bit)
            if (!(mask & (1u << bit)))
                channel(*entry.tickets, bit).reset();
        std::vector<VisibilityPendingReadback> pending{entry};
        std::vector<VisibilityStatus> retired;
        if (mask && mask != 15) {
            REQUIRE(abortDiagnostic([&] {
                        (void)retireVisibilityThrough(*fixture.gpu, pending, retired,
                                                      fixture.gpu->frameNumber());
                    }).find("partial") != std::string::npos);
            REQUIRE(pending.size() == 1);
            REQUIRE(retired.empty());
        } else {
            REQUIRE(retireVisibilityThrough(*fixture.gpu, pending, retired,
                                            fixture.gpu->frameNumber()));
            REQUIRE(pending.empty());
            REQUIRE(retired.size() == (mask == 15 ? 1 : 0));
            REQUIRE(entry.tickets->discarded == (mask == 0));
        }
    }
    std::vector<VisibilityPendingReadback> pending;
    pending.push_back(fixture.record(101));
    pending.push_back(fixture.record(102));
    std::vector<VisibilityStatus> retired;
    REQUIRE(retireVisibilityThrough(*fixture.gpu, pending, retired, fixture.gpu->frameNumber()));
    REQUIRE(retired.size() == 2);
    REQUIRE(retired[0].sceneGeneration == 101);
    REQUIRE(retired[1].sceneGeneration == 102);
    REQUIRE(retired[0].isRetired);
    REQUIRE(retired[1].isRetired);
    REQUIRE(retireVisibilityThrough(*fixture.gpu, pending, retired, fixture.gpu->frameNumber()));
    REQUIRE(retired.size() == 2);
}

//======================================================================================================================
TEST_CASE("visibility retirement validates undecoded tickets and preserves source generations",
          "[render][device]") {
    if (std::string_view(LMX_SHADER_LIBRARY_EXTENSION) != ".nulllib")
        SKIP("transaction fixture requires the configured Null backend");
    RetirementFixture fixture;
    for (uint32_t wrong = 0; wrong < 4; ++wrong) {
        auto entry = fixture.record(201);
        entry.status.checkEnabled = false;
        auto identityFrame = fixture.gpu->beginFrame();
        const auto identityFrameNumber = identityFrame.number;
        fixture.gpu->submitAndEndFrame(identityFrame);
        fixture.gpu->waitForFrame(identityFrameNumber);
        REQUIRE(identityFrameNumber != entry.frameNumber);
        REQUIRE(fixture.gpu->completedFrame() >= identityFrameNumber);
        channel(*entry.tickets, wrong)->frame = identityFrameNumber;
        std::vector<VisibilityPendingReadback> pending{entry};
        std::vector<VisibilityStatus> retired;
        REQUIRE(abortDiagnostic([&] {
                    (void)retireVisibilityThrough(*fixture.gpu, pending, retired,
                                                  fixture.gpu->frameNumber());
                }).find("identity") != std::string::npos);
        REQUIRE(pending[0].status.sceneGeneration == 201);
        REQUIRE(retired.empty());
    }

    auto stage = GpuVisibility::create(*fixture.gpu);
    REQUIRE(stage);
    TransientPool pool(*fixture.gpu);
    TransientPool repeatedPool(*fixture.gpu);
    DrawSubmission submission(fixture.gpu->device());
    SceneView view;
    FrustumPlanes planes;
    auto frame = fixture.gpu->beginFrame();
    const auto prepared = submission.prepare(frame.commands, frame.number, view, {}, {});
    if (!prepared)
        fixture.gpu->submitAndEndFrame(frame);
    REQUIRE(prepared);
    pool.beginFrame();
    RenderGraph first(pool), second(pool);
    auto declare = [&](RenderGraph& graph) {
        VisibilityStatus status;
        status.frameNumber = frame.number;
        const auto rows = graph.importBuffer(*submission.scene().rows, "lmx.draw.rows");
        const auto args = graph.importBuffer(*submission.scene().arguments, "lmx.draw.args");
        return (*stage)->declare(graph, frame.commands, view,
                                 {.planes = planes,
                                  .submission = submission.prepared(),
                                  .rows = rows,
                                  .arguments = args},
                                 status);
    };
    auto firstIntent = declare(first);
    auto secondIntent = declare(second);
    if (!firstIntent || !secondIntent)
        fixture.gpu->submitAndEndFrame(frame);
    REQUIRE(firstIntent);
    REQUIRE(secondIntent);
    auto firstTickets = firstIntent->readbackIntent.lock();
    auto secondTickets = secondIntent->readbackIntent.lock();
    REQUIRE(firstTickets);
    REQUIRE(secondTickets);
    REQUIRE(firstTickets != secondTickets);
    const auto firstLowered = first.lower(frame.number);
    const auto secondLowered = second.lower(frame.number);
    if (!firstLowered || !secondLowered)
        fixture.gpu->submitAndEndFrame(frame);
    REQUIRE(firstLowered);
    REQUIRE(secondLowered);
    auto stateBacking = [](const LoweredFrame& lowered) {
        const auto found = std::ranges::find(lowered.record.debug.resources, "lmx.draw.states",
                                             &DebugResource::name);
        REQUIRE(found != lowered.record.debug.resources.end());
        const auto index = static_cast<size_t>(found - lowered.record.debug.resources.begin());
        return lowered.resources[index].buffer;
    };
    const auto firstBacking = stateBacking(*firstLowered);
    const auto secondBacking = stateBacking(*secondLowered);
    REQUIRE(firstBacking.size == secondBacking.size);
    REQUIRE(firstBacking.buffer != secondBacking.buffer);
    (*stage)->discardUnrecorded(*firstIntent);
    REQUIRE(firstTickets->discarded);
    REQUIRE_FALSE(secondTickets->discarded);
    REQUIRE(abortDiagnostic([&] {
                first.execute(frame.commands, frame.number);
            }).find("discarded") != std::string::npos);
    second.execute(frame.commands, frame.number);
    fixture.gpu->submitAndEndFrame(frame);
    REQUIRE((*stage)->retireThrough(frame.number));
    auto retired = (*stage)->takeRetired();
    REQUIRE(retired.size() == 1);
    REQUIRE(retired[0].frameNumber == frame.number);
    REQUIRE((*stage)->retireThrough(frame.number));
    REQUIRE((*stage)->takeRetired().empty());
    auto repeatedFrame = fixture.gpu->beginFrame();
    const auto repeatedPrepared =
        submission.prepare(repeatedFrame.commands, repeatedFrame.number, view, {}, {});
    if (!repeatedPrepared)
        fixture.gpu->submitAndEndFrame(repeatedFrame);
    REQUIRE(repeatedPrepared);
    pool.beginFrame();
    repeatedPool.beginFrame();
    RenderGraph third(pool), fourth(repeatedPool);
    auto repeated = [&](RenderGraph& graph, uint64_t generation) {
        VisibilityStatus status;
        status.frameNumber = repeatedFrame.number;
        status.sceneGeneration = generation;
        const auto rowsHandle = graph.importBuffer(*submission.scene().rows, "lmx.draw.rows");
        const auto argsHandle = graph.importBuffer(*submission.scene().arguments, "lmx.draw.args");
        return (*stage)->declare(graph, repeatedFrame.commands, view,
                                 {.planes = planes,
                                  .submission = submission.prepared(),
                                  .rows = rowsHandle,
                                  .arguments = argsHandle},
                                 status);
    };
    const auto thirdIntent = repeated(third, 301);
    const auto fourthIntent = repeated(fourth, 302);
    if (!thirdIntent || !fourthIntent)
        fixture.gpu->submitAndEndFrame(repeatedFrame);
    REQUIRE(thirdIntent);
    REQUIRE(fourthIntent);
    VisibilityStatus thirdStatus, fourthStatus;
    thirdStatus.frameNumber = fourthStatus.frameNumber = repeatedFrame.number;
    thirdStatus.sceneGeneration = 301;
    fourthStatus.sceneGeneration = 302;
    thirdStatus.submission.candidateBytes = 17;
    fourthStatus.submission.candidateBytes = 23;
    (*stage)->recordDeclarationStatus(*thirdIntent, thirdStatus);
    (*stage)->recordDeclarationStatus(*fourthIntent, fourthStatus);
    fourthStatus.submission.candidateBytes = 29;
    thirdStatus.submission.candidateBytes = 31;
    (*stage)->recordDeclarationStatus(*fourthIntent, fourthStatus);
    (*stage)->recordDeclarationStatus(*thirdIntent, thirdStatus);
    third.execute(repeatedFrame.commands, repeatedFrame.number);
    fourth.execute(repeatedFrame.commands, repeatedFrame.number);
    fixture.gpu->submitAndEndFrame(repeatedFrame);
    REQUIRE((*stage)->retireThrough(repeatedFrame.number));
    const auto repeatedRetired = (*stage)->takeRetired();
    REQUIRE(repeatedRetired.size() == 2);
    REQUIRE(repeatedRetired[0].frameNumber == repeatedFrame.number);
    REQUIRE(repeatedRetired[1].frameNumber == repeatedFrame.number);
    REQUIRE(repeatedRetired[0].sceneGeneration == 301);
    REQUIRE(repeatedRetired[1].sceneGeneration == 302);
    REQUIRE(repeatedRetired[0].submission.candidateBytes == 31);
    REQUIRE(repeatedRetired[1].submission.candidateBytes == 29);
    REQUIRE(repeatedRetired[0].isRetired);
    REQUIRE(repeatedRetired[1].isRetired);
    REQUIRE(abortDiagnostic([&] {
                (*stage)->discardUnrecorded(*thirdIntent);
            }).find("declaration") != std::string::npos);
    REQUIRE(abortDiagnostic([&] { (*stage)->discardUnrecorded({}); }).find("token") !=
            std::string::npos);
}
