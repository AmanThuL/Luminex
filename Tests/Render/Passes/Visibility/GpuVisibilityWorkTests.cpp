#include "Render/Passes/Visibility/GpuVisibility.h"

#include "Support/GpuTestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace {
namespace render = lmx::render;

struct WorkFixture {
    std::unique_ptr<lmx::engine::GpuContext> gpu;
    rojoRHI::Device* device = nullptr;
    std::unique_ptr<render::GpuVisibility> visibility;
    std::unique_ptr<render::DrawSubmission> submission;
    std::unique_ptr<render::TransientPool> pool;
    lmx::engine::GpuBuffer meshes;
    std::vector<lmx::engine::GpuBuffer> instances;
    std::vector<lmx::engine::InstanceRow> rows;
    std::vector<lmx::engine::DrawItem> items;
    render::FrustumPlanes planes;
    rojoRHI::Buffer lastRows{}, lastArgs{};
    std::shared_ptr<std::optional<render::ReadbackTicket>> lastStateGuard;
    std::array<rojoRHI::Buffer, 3> previousRows{}, previousArguments{};
    std::array<render::SyncPoint, 3> rowFrontiers{}, argumentFrontiers{};
    render::SyncPoint lastRowPhysical{}, lastArgumentPhysical{};
    const lmx::engine::GpuBuffer* lastRowsOwner = nullptr;
    const lmx::engine::GpuBuffer* lastArgsOwner = nullptr;

    //==================================================================================================================
    explicit WorkFixture(uint32_t count) {
        gpu = createTestGpu();
        device = &gpu->device();
        auto stage = render::GpuVisibility::create(*gpu);
        INFO(errorOf(stage));
        REQUIRE(stage);
        visibility = std::move(*stage);
        submission = std::make_unique<render::DrawSubmission>(*device);
        pool = std::make_unique<render::TransientPool>(*gpu);
        lmx::engine::MeshRow mesh{.firstIndex = 17, .indexCount = 9};
        auto buffer = lmx::engine::GpuBuffer::create(*device,
                                                     {.size = sizeof(mesh),
                                                      .memory = rojoRHI::Memory::Upload,
                                                      .label = "lmx.test.visibility.mesh"},
                                                     std::as_bytes(std::span{&mesh, 1}));
        REQUIRE(buffer);
        meshes = std::move(*buffer);
        planes.valid = true;
        planes.planes.fill({1, 0, 0, -1});
        resize(count);
    }
    //==================================================================================================================
    ~WorkFixture() { gpu->waitIdle(); }

    //==================================================================================================================
    void resize(uint32_t count) {
        rows.resize(count);
        items.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            rows[i] = {};
            const float x = i % 3 == 0 ? std::nextafter(1.0f, 0.0f) : std::nextafter(1.0f, 2.0f);
            rows[i].worldBoundsMin = rows[i].worldBoundsMax = {x, 0, 0};
            items[i].instanceRow = i;
            items[i].mesh.firstIndex = 17;
            items[i].mesh.indexCount = 9;
        }
    }

    //==================================================================================================================
    uint64_t submit(render::SubmissionMode mode, bool enabled = true,
                    std::optional<std::array<uint32_t, 3>> capacities = std::nullopt) {
        auto input = lmx::engine::GpuBuffer::create(
            *device,
            {.size = std::max<size_t>(1, rows.size()) * sizeof(lmx::engine::InstanceRow),
             .memory = rojoRHI::Memory::Upload,
             .label = "lmx.test.visibility.instances"},
            std::as_bytes(std::span(rows)));
        REQUIRE(input);
        instances.push_back(std::move(*input));
        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        struct CloseFrame {
            lmx::engine::GpuContext& gpu;
            lmx::engine::Frame& frame;
            bool closed = false;
            //==========================================================================================================
            ~CloseFrame() {
                if (!closed)
                    gpu.submitAndEndFrame(frame);
            }
            //==========================================================================================================
            void submit() {
                gpu.submitAndEndFrame(frame);
                closed = true;
            }
        } closeFrame{*gpu, commandsFrame};
        const auto frame = gpu->frameNumber();
        if (frame >= 3)
            REQUIRE(visibility->retireThrough(frame - 3));
        pool->beginFrame();
        render::SceneView view;
        view.items = items;
        view.tables.instanceRows = rows;
        view.tables.instanceCapacity = static_cast<uint32_t>(rows.size());
        view.tables.instances = &instances.back();
        view.tables.meshes = &meshes;
        view.submission = mode;
        view.visibilityEnabled = enabled;
        view.classifyMode = render::ClassifyMode::Gpu;
        view.classifyCheck = true;
        REQUIRE(submission->prepare(commands, frame, view, {}, {}));
        lastRowsOwner = submission->scene().rows;
        lastArgsOwner = submission->scene().arguments;
        lastRows = lastRowsOwner->rhi();
        lastArgs = lastArgsOwner->rhi();
        render::RenderGraph graph(*pool);
        const auto instanceHandle = graph.importBuffer(instances.back(), "lmx.scene.instances");
        const auto meshHandle = graph.importBuffer(meshes, "lmx.scene.meshes");
        const render::SyncPoint copy{rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite,
                                     rojoRHI::Layout::Undefined};
        const render::SyncPoint none{rojoRHI::Stage::None, rojoRHI::Access::None,
                                     rojoRHI::Layout::Undefined};
        const auto slot = frame % previousRows.size();
        const auto rowHandle =
            graph.importBuffer(*lastRowsOwner, "lmx.draw.rows", render::BufferUse::StorageWrite,
                               previousRows[slot] == lastRows ? rowFrontiers[slot] : none);
        const auto argumentHandle = graph.importBuffer(
            *lastArgsOwner, "lmx.draw.args", render::BufferUse::StorageWrite,
            previousArguments[slot] == lastArgs ? argumentFrontiers[slot] : none);
        render::CopyPassDesc initialize;
        initialize.bufferDestinations = {rowHandle, argumentHandle};
        graph.addCopyPass(
            "lmx.test.visibility.workGuards", std::move(initialize),
            [&, rowHandle, argumentHandle](const render::PassResources& resources) {
                REQUIRE(resources.buffer(rowHandle) == lastRows);
                REQUIRE(resources.buffer(argumentHandle) == lastArgs);
                commands.fillBuffer(lastRows, 0, lastRowsOwner->desc().size, 0xcdcdcdcdu);
                commands.fillBuffer(lastArgs, 0, lastArgsOwner->desc().size, 0xcdcdcdcdu);
            });
        lastStateGuard =
            capacities ? std::make_shared<std::optional<render::ReadbackTicket>>() : nullptr;
        render::VisibilityStatus status;
        status.frameNumber = frame;
        status.classifyMode = render::ClassifyMode::Gpu;
        status.checkEnabled = true;
        status.submission = submission->stats();
        for (uint32_t i = 0; i < rows.size(); ++i)
            status.scene.candidates.push_back(
                {.instanceRow = i,
                 .worldBounds = {rows[i].worldBoundsMin, rows[i].worldBoundsMax}});
        status.shadow.candidates = status.scene.candidates;
        visibility->setCapacityOverride(capacities);
        const auto declared = visibility->declare(graph, commands, view,
                                                  {.planes = planes,
                                                   .submission = submission->prepared(),
                                                   .instances = instanceHandle,
                                                   .meshes = meshHandle,
                                                   .rows = render::nextVersion(rowHandle),
                                                   .arguments = render::nextVersion(argumentHandle),
                                                   .stateGuardReadback = lastStateGuard,
                                                   .rowPhysical = copy,
                                                   .argumentPhysical = copy},
                                                  status);
        REQUIRE(declared);
        graph.execute(commands, frame);
        // These copies and the exported emit callback have actually recorded on this list.
        // Match their bounded write capability, retaining the full canary contribution for holes.
        lastRowPhysical = lastArgumentPhysical = copy;
        const auto rowCapacity = capacities ? (*capacities)[0] : lastRowsOwner->desc().size / 4;
        const auto argumentCapacity =
            capacities ? (*capacities)[1]
                       : lastArgsOwner->desc().size / sizeof(rojoRHI::DrawIndexedIndirectArgs);
        const auto stateCapacity = capacities ? (*capacities)[2] : rows.size() * 2;
        const bool rowWrite =
            !rows.empty() && rowCapacity &&
            (mode != render::SubmissionMode::Batched || (stateCapacity && argumentCapacity));
        const bool argumentWrite = !rows.empty() && argumentCapacity;
        if (rowWrite) {
            lastRowPhysical.stage = lastRowPhysical.stage | rojoRHI::Stage::ComputeShader;
            lastRowPhysical.access = lastRowPhysical.access | rojoRHI::Access::ShaderWrite;
        }
        if (argumentWrite) {
            lastArgumentPhysical.stage = lastArgumentPhysical.stage | rojoRHI::Stage::ComputeShader;
            lastArgumentPhysical.access =
                lastArgumentPhysical.access | rojoRHI::Access::ShaderWrite;
        }
        previousRows[slot] = lastRows;
        previousArguments[slot] = lastArgs;
        rowFrontiers[slot] = lastRowPhysical;
        argumentFrontiers[slot] = lastArgumentPhysical;
        closeFrame.submit();
        return frame;
    }

    //==================================================================================================================
    std::vector<render::VisibilityStatus> drain() {
        gpu->waitIdle();
        REQUIRE(visibility->retireThrough(gpu->frameNumber()));
        return visibility->takeRetired();
    }
};

//======================================================================================================================
void requireExact(const render::VisibilityStatus& status) {
    CAPTURE(status.frameNumber, status.stateMismatches, status.rowMismatches,
            status.argumentMismatches, status.counterMismatches);
    REQUIRE(status.isRetired);
    REQUIRE(status.checkPassed());
    REQUIRE(status.submission.listBytes ==
            (uint64_t{status.sceneCounters.emittedRows} + status.shadowCounters.emittedRows) *
                sizeof(uint32_t));
}
} // namespace

//======================================================================================================================
TEST_CASE("GPU work generation exactly matches ordered fp32 oracle and bypass precedence",
          "[gpu][visibility]") {
    WorkFixture fixture(513);
    fixture.rows[2].flags = lmx::engine::kInstanceBoundsUnreliable;
    fixture.rows[3].model[0][0] = std::numeric_limits<float>::quiet_NaN();
    fixture.rows[4].worldBoundsMin.x = std::numeric_limits<float>::infinity();
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched})
        for (bool enabled : {true, false}) {
            fixture.submit(mode, enabled);
            auto results = fixture.drain();
            REQUIRE(results.size() == 1);
            requireExact(results[0]);
            REQUIRE_FALSE(results[0].overflow);
            REQUIRE(results[0].sceneCounters.candidates == 513);
            REQUIRE(results[0].shadowCounters.emittedRows == 513);
        }
}

//======================================================================================================================
TEST_CASE("GPU dense scan carries across more than 256 chunks deterministically",
          "[gpu][visibility]") {
    WorkFixture fixture(256 * 256 + 1);
    std::vector<uint32_t> previous;
    for (uint32_t frame = 0; frame < 2; ++frame) {
        fixture.submit(render::SubmissionMode::Batched);
        const auto results = fixture.drain();
        REQUIRE(results.size() == 1);
        requireExact(results[0]);
        std::vector<uint32_t> bytes(fixture.lastRowsOwner->desc().size / 4);
        {
            REQUIRE(fixture.lastRowsOwner->rhi() == fixture.lastRows);
            const auto readbackBytes = lmx::render::readbackNow(
                *fixture.gpu, *fixture.lastRowsOwner, fixture.lastRowPhysical,
                {0, fixture.lastRowsOwner->desc().size});
            REQUIRE(readbackBytes);
            std::memcpy(bytes.data(), readbackBytes->data(), fixture.lastRowsOwner->desc().size);
        }
        if (frame)
            REQUIRE(bytes == previous);
        previous = std::move(bytes);
    }
}

//======================================================================================================================
TEST_CASE("GPU capacities drop rows and commands without writing guard words",
          "[gpu][visibility]") {
    WorkFixture fixture(513);
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched})
        for (const auto capacities :
             {std::array<uint32_t, 3>{19, 2, 1026}, std::array<uint32_t, 3>{1026, 1026, 31},
              std::array<uint32_t, 3>{0, 0, 0}}) {
            fixture.submit(mode, true, capacities);
            const auto results = fixture.drain();
            REQUIRE(results.size() == 1);
            requireExact(results[0]);
            REQUIRE(results[0].overflow);
            REQUIRE(fixture.lastStateGuard);
            REQUIRE(*fixture.lastStateGuard);
            std::vector<uint32_t> states((*fixture.lastStateGuard)->region.size / 4);
            REQUIRE(render::resolve(*fixture.gpu, **fixture.lastStateGuard,
                                    render::resolveBytes(std::span(states))));
            for (size_t i = capacities[2]; i < states.size(); ++i)
                REQUIRE(states[i] == 0xcdcdcdcdu);
            std::vector<uint32_t> rows(fixture.lastRowsOwner->desc().size / 4);
            {
                REQUIRE(fixture.lastRowsOwner->rhi() == fixture.lastRows);
                const auto readbackBytes = lmx::render::readbackNow(
                    *fixture.gpu, *fixture.lastRowsOwner, fixture.lastRowPhysical,
                    {0, fixture.lastRowsOwner->desc().size});
                REQUIRE(readbackBytes);
                std::memcpy(rows.data(), readbackBytes->data(), fixture.lastRowsOwner->desc().size);
            }
            for (size_t i = capacities[0]; i < rows.size(); ++i)
                REQUIRE(rows[i] == 0xcdcdcdcdu);
            std::vector<uint32_t> args(fixture.lastArgsOwner->desc().size / 4);
            {
                REQUIRE(fixture.lastArgsOwner->rhi() == fixture.lastArgs);
                const auto readbackBytes = lmx::render::readbackNow(
                    *fixture.gpu, *fixture.lastArgsOwner, fixture.lastArgumentPhysical,
                    {0, fixture.lastArgsOwner->desc().size});
                REQUIRE(readbackBytes);
                std::memcpy(args.data(), readbackBytes->data(), fixture.lastArgsOwner->desc().size);
            }
            for (size_t i = capacities[1] * 6; i < args.size(); ++i)
                REQUIRE(args[i] == 0xcdcdcdcdu);
        }
}

//======================================================================================================================
TEST_CASE("GPU diagnostics retire the exact overlapping frame through growth and final drain",
          "[gpu][visibility]") {
    WorkFixture fixture(7);
    std::vector<uint64_t> frames;
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == 2)
            fixture.resize(4097);
        fixture.rows[0].worldBoundsMin.x = fixture.rows[0].worldBoundsMax.x = i % 2 ? 2.0f : 0.0f;
        frames.push_back(fixture.submit(i % 2 ? render::SubmissionMode::Batched
                                              : render::SubmissionMode::Indirect));
    }
    auto results = fixture.drain();
    REQUIRE(results.size() == 4);
    for (uint32_t i = 0; i < 4; ++i) {
        requireExact(results[i]);
        REQUIRE(results[i].frameNumber == frames[i]);
        REQUIRE(results[i].sceneCounters.candidates == (i < 2 ? 7 : 4097));
        REQUIRE(results[i].scene.candidates[0].state ==
                (i % 2 ? render::VisibilityState::Visible : render::VisibilityState::Rejected));
    }
    REQUIRE(fixture.visibility->takeRetired().empty());
}

//======================================================================================================================
TEST_CASE("GPU safe visibility preserves ordered cancellation that an FMA would reject",
          "[gpu][visibility]") {
    WorkFixture fixture(3);
    fixture.planes.planes.fill({-0.1f, 1.0f, 0, 0});
    const std::array<float, 3> positions{10.0f, std::nextafter(10.0f, 11.0f),
                                         std::nextafter(10.0f, 9.0f)};
    for (uint32_t i = 0; i < 3; ++i)
        fixture.rows[i].worldBoundsMin = fixture.rows[i].worldBoundsMax = {positions[i], 1, 0};
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        fixture.submit(mode);
        const auto results = fixture.drain();
        REQUIRE(results.size() == 1);
        requireExact(results[0]);
        REQUIRE(results[0].scene.visibleItems == std::vector<uint32_t>{0, 2});
    }
}

//======================================================================================================================
TEST_CASE("GPU disabled rows contribute only disabled counters and no work in either view",
          "[gpu][visibility][disabled-instance]") {
    WorkFixture fixture(513);
    uint32_t disabled = 0;
    for (uint32_t i = 0; i < fixture.rows.size(); ++i) {
        if (i % 5 == 0) {
            fixture.rows[i].flags = lmx::engine::kInstanceDisabled;
            ++disabled;
        }
    }
    fixture.rows[0].model[0][0] = std::numeric_limits<float>::quiet_NaN();
    fixture.rows[5].flags |= lmx::engine::kInstanceBoundsUnreliable;
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        for (bool culling : {true, false}) {
            fixture.submit(mode, culling);
            const auto results = fixture.drain();
            REQUIRE(results.size() == 1);
            const auto& result = results[0];
            requireExact(result);
            REQUIRE_FALSE(result.overflow);
            REQUIRE(result.submission.counterBytes == 2 * 21 * sizeof(uint32_t));
            REQUIRE(result.sceneCounters.candidates == 513 - disabled);
            REQUIRE(result.shadowCounters.candidates == 513 - disabled);
            REQUIRE(result.sceneCounters.disabled == disabled);
            REQUIRE(result.shadowCounters.disabled == disabled);
            REQUIRE(result.shadowCounters.emittedRows == 513 - disabled);
            for (const auto* view : {&result.scene, &result.shadow}) {
                REQUIRE(view->candidates.size() == 513);
                REQUIRE(view->disabled == disabled);
                for (uint32_t index : view->visibleItems)
                    REQUIRE(index % 5 != 0);
                for (uint32_t i = 0; i < 513; i += 5) {
                    REQUIRE(view->candidates[i].state == render::VisibilityState::Rejected);
                    REQUIRE(view->candidates[i].reason == render::VisibilityReason::AuthoredOff);
                }
            }
            if (mode == render::SubmissionMode::Indirect) {
                std::vector<uint32_t> rows(fixture.lastRowsOwner->desc().size / 4);
                {
                    REQUIRE(fixture.lastRowsOwner->rhi() == fixture.lastRows);
                    const auto readbackBytes = lmx::render::readbackNow(
                        *fixture.gpu, *fixture.lastRowsOwner, fixture.lastRowPhysical,
                        {0, fixture.lastRowsOwner->desc().size});
                    REQUIRE(readbackBytes);
                    std::memcpy(rows.data(), readbackBytes->data(),
                                fixture.lastRowsOwner->desc().size);
                }
                std::vector<rojoRHI::DrawIndexedIndirectArgs> args(
                    fixture.lastArgsOwner->desc().size / sizeof(rojoRHI::DrawIndexedIndirectArgs));
                {
                    REQUIRE(fixture.lastArgsOwner->rhi() == fixture.lastArgs);
                    const auto readbackBytes = lmx::render::readbackNow(
                        *fixture.gpu, *fixture.lastArgsOwner, fixture.lastArgumentPhysical,
                        {0, fixture.lastArgsOwner->desc().size});
                    REQUIRE(readbackBytes);
                    std::memcpy(args.data(), readbackBytes->data(),
                                fixture.lastArgsOwner->desc().size);
                }
                for (uint32_t i = 0; i < 513; i += 5) {
                    REQUIRE(rows[i] == 0xcdcdcdcdu);
                    REQUIRE(rows[513 + i] == 0xcdcdcdcdu);
                    REQUIRE(args[i].instanceCount == 0);
                    REQUIRE(args[513 + i].instanceCount == 0);
                }
            }
        }
    }
    for (auto& row : fixture.rows)
        row.flags |= lmx::engine::kInstanceDisabled;
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        fixture.submit(mode, false);
        const auto results = fixture.drain();
        REQUIRE(results.size() == 1);
        requireExact(results[0]);
        for (const auto* counts : {&results[0].sceneCounters, &results[0].shadowCounters}) {
            REQUIRE(counts->disabled == 513);
            REQUIRE(counts->candidates == 0);
            REQUIRE(counts->rejected == 0);
            REQUIRE(counts->emittedRows == 0);
            REQUIRE(counts->emittedCommands == 0);
        }
    }
}
