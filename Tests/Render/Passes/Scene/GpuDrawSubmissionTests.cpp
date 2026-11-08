//----------------------------------------------------------------------------------------------------------------------
/// @file GpuDrawSubmissionTests.cpp
/// @brief Reads actual paced GPU draw destinations through overlap, growth and slot reuse.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Gpu/Readback.h"
#include "Render/Passes/Scene/DrawSubmission.h"
#include "Render/Renderer/SceneView.h"
#include "Support/TestGpu.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

using namespace lmx;
using namespace lmx::render;

//======================================================================================================================
TEST_CASE("draw submission copies paced rows and arguments across overlap and growth",
          "[gpu][scene-submission]") {
    for (auto mode : {SubmissionMode::Direct, SubmissionMode::Indirect, SubmissionMode::Batched}) {
        CAPTURE(mode);
        auto gpu = createTestGpu(
            {.validation = trackerAvailable(), .scopeTimings = false, .enableCapture = false});
        DrawSubmission submission(gpu->device());
        std::vector<engine::InstanceRow> rows(32);
        std::array<engine::DrawItem, 4> items;
        SceneView view;
        view.items = items;
        view.tables.instanceRows = rows;
        view.submission = mode;
        VisibilityResult scene, shadow;
        struct Snapshot {
            ReadbackTicket rows, arguments;
            std::vector<uint32_t> expectedRows;
            std::vector<rojoRHI::DrawIndexedIndirectArgs> expectedArguments;
        };
        const auto compare = [&](const Snapshot& snapshot) {
            CAPTURE(snapshot.rows.frame);
            std::vector<uint32_t> actualRows(snapshot.expectedRows.size());
            auto rowResult = resolve(*gpu, snapshot.rows, resolveBytes(std::span{actualRows}));
            INFO(errorOf(rowResult));
            REQUIRE(rowResult.has_value());
            REQUIRE(actualRows == snapshot.expectedRows);
            std::vector<rojoRHI::DrawIndexedIndirectArgs> actualArguments(
                snapshot.expectedArguments.size());
            auto argumentResult =
                resolve(*gpu, snapshot.arguments, resolveBytes(std::span{actualArguments}));
            INFO(errorOf(argumentResult));
            REQUIRE(argumentResult.has_value());
            REQUIRE(std::memcmp(actualArguments.data(), snapshot.expectedArguments.data(),
                                actualArguments.size() * 24) == 0);
        };
        std::vector<Snapshot> snapshots;
        std::array<rojoRHI::Buffer, 3> slotRows{}, slotArguments{};
        std::array<uint64_t, 3> slotFrames{};
        std::array<bool, 3> reused{};
        std::vector<uint64_t> actualFrames;
        // The readiness barriers do not issue shader/indirect reads. CopyWrite is the real
        // producer used by these preserving readbacks, all recorded on the prepared frame.
        constexpr SyncPoint copied{rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite,
                                   rojoRHI::Layout::Undefined};
        for (uint32_t ordinal = 0; ordinal < 9; ++ordinal) {
            auto frame = gpu->beginFrame();
            CAPTURE(frame.number);
            actualFrames.push_back(frame.number);
            REQUIRE(frame.number == gpu->frameNumber());
            if (actualFrames.size() > 1)
                REQUIRE(frame.number == actualFrames[actualFrames.size() - 2] + 1);
            std::optional<Snapshot> old;
            if (ordinal == 3) {
                const auto& previous = snapshots.back();
                old.emplace();
                old->expectedRows = previous.expectedRows;
                old->expectedArguments = previous.expectedArguments;
                old->rows = recordReadback(*gpu, frame.commands, *submission.scene().rows, copied,
                                           {0, old->expectedRows.size() * sizeof(uint32_t)});
                old->arguments = recordReadback(*gpu, frame.commands, *submission.scene().arguments,
                                                copied, {0, old->expectedArguments.size() * 24});
                REQUIRE(old->rows.frame == frame.number);
                REQUIRE(old->arguments.frame == frame.number);
                // Previous frame is still inside the three-frame pacing window at this growth.
                REQUIRE(frame.number < actualFrames[actualFrames.size() - 2] + 3);
            }
            view.tables.instanceCapacity = ordinal < 3 ? 0 : 9;
            for (uint32_t i = 0; i < items.size(); ++i) {
                items[i].instanceRow = (ordinal * 3 + i * 5) % rows.size();
                items[i].mesh.firstIndex = ordinal * 3;
                items[i].mesh.indexCount = 6;
            }
            if (ordinal) {
                scene.visibleItems = {2, 0, 3};
                shadow.visibleItems = {1, 3, 0, 2};
            }
            auto result = submission.prepare(frame.commands, frame.number, view, scene, shadow);
            if (!result)
                gpu->submitAndEndFrame(frame);
            INFO(errorOf(result));
            REQUIRE(result.has_value());
            const auto stats = submission.stats();
            REQUIRE(submission.scene().rows->desc().memory == rojoRHI::Memory::Gpu);
            REQUIRE(submission.scene().arguments->desc().memory == rojoRHI::Memory::Gpu);
            REQUIRE(submission.scene().rows->desc().usage == rojoRHI::BufferUsage::ShaderWrite);
            REQUIRE(submission.scene().arguments->desc().usage ==
                    (rojoRHI::BufferUsage::ShaderWrite | rojoRHI::BufferUsage::IndirectArgs));
            REQUIRE_FALSE(submission.rowUse().has_value());
            REQUIRE_FALSE(submission.argumentUse().has_value());
            const uint64_t capacity = ordinal == 0 ? 1 : ordinal < 3 ? 8 : 32;
            REQUIRE(stats.allocatedListBytes == capacity * 4 * 3);
            REQUIRE(stats.allocatedArgumentBytes == capacity * 24 * 3);
            REQUIRE(stats.allocatedUploadBytes == capacity * 28 * 3);
            REQUIRE(stats.pendingReleaseBuffers ==
                    (ordinal == 1 || ordinal == 2 || ordinal == 3 || ordinal == 4 ? 6 : 0));
            REQUIRE(stats.pendingReleaseUploadBuffers == stats.pendingReleaseBuffers);
            if (ordinal == 0) {
                REQUIRE(stats.listBytes == 0);
                REQUIRE(stats.argumentBytes == 0);
                REQUIRE(submission.scene().runs.empty());
                REQUIRE(submission.shadow().runs.empty());
            } else {
                Snapshot snapshot;
                snapshot.expectedRows = {items[2].instanceRow, items[0].instanceRow,
                                         items[3].instanceRow, items[1].instanceRow,
                                         items[3].instanceRow, items[0].instanceRow,
                                         items[2].instanceRow};
                if (mode == SubmissionMode::Batched) {
                    snapshot.expectedArguments = {{.drawId = 0,
                                                   .indexCount = 6,
                                                   .instanceCount = 3,
                                                   .firstIndex = ordinal * 3,
                                                   .baseVertex = 0,
                                                   .firstInstance = 0},
                                                  {.drawId = 3,
                                                   .indexCount = 6,
                                                   .instanceCount = 4,
                                                   .firstIndex = ordinal * 3,
                                                   .baseVertex = 0,
                                                   .firstInstance = 0}};
                } else {
                    for (uint32_t firstEntry = 0; firstEntry < 7; ++firstEntry)
                        snapshot.expectedArguments.push_back({.drawId = firstEntry,
                                                              .indexCount = 6,
                                                              .instanceCount = 1,
                                                              .firstIndex = ordinal * 3,
                                                              .baseVertex = 0,
                                                              .firstInstance = 0});
                }
                REQUIRE(stats.listBytes == 28);
                REQUIRE(stats.argumentBytes == snapshot.expectedArguments.size() * 24);
                snapshot.rows = recordReadback(*gpu, frame.commands, *submission.scene().rows,
                                               copied, {0, stats.listBytes});
                snapshot.arguments =
                    recordReadback(*gpu, frame.commands, *submission.scene().arguments, copied,
                                   {0, stats.argumentBytes});
                REQUIRE(snapshot.rows.frame == frame.number);
                REQUIRE(snapshot.arguments.frame == frame.number);
                snapshots.push_back(std::move(snapshot));
            }
            if (ordinal >= 3) {
                const auto slot = frame.number % 3;
                const auto rowBuffer = submission.scene().rows->rhi();
                const auto argumentBuffer = submission.scene().arguments->rhi();
                if (slotFrames[slot]) {
                    REQUIRE(frame.number == slotFrames[slot] + 3);
                    REQUIRE(rowBuffer == slotRows[slot]);
                    REQUIRE(argumentBuffer == slotArguments[slot]);
                    reused[slot] = true;
                }
                slotRows[slot] = rowBuffer;
                slotArguments[slot] = argumentBuffer;
                slotFrames[slot] = frame.number;
            }
            gpu->submitAndEndFrame(frame);
            // Complete the extra old-generation copy while its source owners are retained.
            if (old) {
                compare(*old);
                REQUIRE(gpu->frameNumber() == frame.number);
            }
        }
        REQUIRE(std::ranges::all_of(reused, [](bool value) { return value; }));
        for (const auto& snapshot : snapshots)
            compare(snapshot);
        REQUIRE(gpu->frameNumber() == actualFrames.back());
        SECTION("capacity refusal uses the actual device limits and preserves prepared data") {
            const auto& limits = gpu->device().capabilities().limits;
            const uint64_t limit = std::min(limits.maxBufferSize, limits.maxAddrOffset) / 24;
            if (limit / 2 < std::numeric_limits<uint32_t>::max()) {
                auto frame = gpu->beginFrame();
                const auto before = submission.stats();
                const auto rowBuffer = submission.scene().rows->rhi();
                view.tables.instanceCapacity = static_cast<uint32_t>(limit / 2 + 1);
                auto refused =
                    submission.prepare(frame.commands, frame.number, view, scene, shadow);
                gpu->submitAndEndFrame(frame);
                REQUIRE_FALSE(refused.has_value());
                REQUIRE(refused.error().code == ErrorCode::InvalidArgument);
                REQUIRE(refused.error().message.contains("rowBytes="));
                REQUIRE(refused.error().message.contains("argumentBytes="));
                REQUIRE(refused.error().message.contains("maxBufferSize="));
                REQUIRE(refused.error().message.contains("maxAddrOffset="));
                REQUIRE(submission.scene().rows->rhi() == rowBuffer);
                REQUIRE(submission.stats().growthEvents == before.growthEvents);
                REQUIRE(submission.stats().allocatedUploadBytes == before.allocatedUploadBytes);
            }
        }
        gpu->waitIdle();
    }
}
