#include "GpuTestSupport.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
namespace render = lmx::render;

struct WorkFixture {
    std::unique_ptr<rojoRHI::Device> device;
    std::unique_ptr<render::GpuVisibility> visibility;
    std::unique_ptr<render::DrawSubmission> submission;
    std::unique_ptr<render::TransientPool> pool;
    std::unique_ptr<rojoRHI::Buffer> meshes;
    std::vector<std::unique_ptr<rojoRHI::Buffer>> instances;
    std::vector<lmx::engine::InstanceRow> rows;
    std::vector<lmx::engine::DrawItem> items;
    render::FrustumPlanes planes;
    rojoRHI::Buffer* lastRows = nullptr;
    rojoRHI::Buffer* lastArgs = nullptr;
    rojoRHI::Buffer* lastStates = nullptr;

    //==================================================================================================================
    explicit WorkFixture(uint32_t count) {
        auto created = rojoRHI::createDevice();
        REQUIRE(created);
        device = std::move(*created);
        auto stage = render::GpuVisibility::create(*device);
        INFO(errorOf(stage));
        REQUIRE(stage);
        visibility = std::move(*stage);
        submission = std::make_unique<render::DrawSubmission>(*device);
        pool = std::make_unique<render::TransientPool>(*device);
        lmx::engine::MeshRow mesh{.firstIndex = 17, .indexCount = 9};
        auto buffer = device->createBuffer(
            {.size = sizeof(mesh), .label = "lmx.test.visibility.mesh"}, &mesh);
        REQUIRE(buffer);
        meshes = std::move(*buffer);
        planes.valid = true;
        planes.planes.fill({1, 0, 0, -1});
        resize(count);
    }
    //==================================================================================================================
    ~WorkFixture() { device->waitIdle(); }

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
        auto input = device->createBuffer(
            {.size = std::max<size_t>(1, rows.size()) * sizeof(lmx::engine::InstanceRow),
             .label = "lmx.test.visibility.instances"},
            rows.empty() ? nullptr : rows.data());
        REQUIRE(input);
        instances.push_back(std::move(*input));
        auto& commands = device->beginFrame();
        const auto frame = device->frameNumber();
        if (frame >= 3)
            visibility->retireThrough(frame - 3);
        pool->beginFrame();
        render::SceneView view;
        view.items = items;
        view.tables.instanceRows = rows;
        view.tables.instanceCapacity = static_cast<uint32_t>(rows.size());
        view.tables.instances = instances.back().get();
        view.tables.meshes = meshes.get();
        view.submission = mode;
        view.visibilityEnabled = enabled;
        view.classifyMode = render::ClassifyMode::Gpu;
        view.classifyCheck = true;
        REQUIRE(submission->prepare(frame, view, {}, {}));
        lastRows = submission->scene().rows;
        lastArgs = submission->scene().arguments;
        std::vector<uint32_t> rowSentinels(lastRows->size() / 4, 0xcdcdcdcdu);
        std::vector<uint32_t> argSentinels(lastArgs->size() / 4, 0xcdcdcdcdu);
        lastRows->write(0, rowSentinels.data(), lastRows->size());
        lastArgs->write(0, argSentinels.data(), lastArgs->size());
        render::RenderGraph graph(*pool);
        const auto instanceHandle = graph.importBuffer(*instances.back(), "lmx.scene.instances");
        const auto meshHandle = graph.importBuffer(*meshes, "lmx.scene.meshes");
        const auto rowHandle =
            graph.importBuffer(*lastRows, "lmx.draw.rows", rojoRHI::BufferUse::StorageWrite);
        const auto argumentHandle =
            graph.importBuffer(*lastArgs, "lmx.draw.args", rojoRHI::BufferUse::StorageWrite);
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
        visibility->declare(graph, commands, view, planes, submission->prepared(), instanceHandle,
                            meshHandle, rowHandle, argumentHandle, status);
        if (capacities) {
            const auto record = graph.compileFrame(frame);
            REQUIRE(record);
            const auto resource = std::ranges::find(record->debug.resources, "lmx.draw.states",
                                                    &render::DebugResource::name);
            const auto pass = std::ranges::find(
                record->debug.passes, "lmx.pass.visibility.classify", &render::DebugPass::label);
            REQUIRE(resource != record->debug.resources.end());
            REQUIRE(pass != record->debug.passes.end());
            const auto stateResource =
                static_cast<uint32_t>(resource - record->debug.resources.begin());
            const auto classifyPass = static_cast<uint32_t>(pass - record->debug.passes.begin());
            auto state = graph.passResources(classifyPass).buffer({stateResource, 0});
            REQUIRE(state);
            lastStates = *state;
            // Fixture-only GPU initialization leaves production buffer flags and passes intact.
            commands.bufferBarrier(*lastStates, rojoRHI::BufferUse::StorageRead,
                                   rojoRHI::BufferUse::CopyDestination);
            commands.beginCopyPass("lmx.test.visibility.stateGuard");
            commands.fillBuffer(*lastStates, 0, lastStates->size(), 0xcd);
            commands.endCopyPass();
            commands.bufferBarrier(*lastStates, rojoRHI::BufferUse::CopyDestination,
                                   rojoRHI::BufferUse::StorageWrite);
        }
        graph.execute(commands, frame);
        device->endFrame(nullptr);
        return frame;
    }

    //==================================================================================================================
    std::vector<render::VisibilityStatus> drain() {
        device->waitIdle();
        visibility->retireThrough(device->frameNumber());
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
        std::vector<uint32_t> bytes(fixture.lastRows->size() / 4);
        fixture.lastRows->readback(bytes.data(), fixture.lastRows->size());
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
            REQUIRE(fixture.lastStates);
            std::vector<uint32_t> states(fixture.lastStates->size() / 4);
            fixture.lastStates->readback(states.data(), fixture.lastStates->size());
            for (size_t i = capacities[2]; i < states.size(); ++i)
                REQUIRE(states[i] == 0xcdcdcdcdu);
            std::vector<uint32_t> rows(fixture.lastRows->size() / 4);
            fixture.lastRows->readback(rows.data(), fixture.lastRows->size());
            for (size_t i = capacities[0]; i < rows.size(); ++i)
                REQUIRE(rows[i] == 0xcdcdcdcdu);
            std::vector<uint32_t> args(fixture.lastArgs->size() / 4);
            fixture.lastArgs->readback(args.data(), fixture.lastArgs->size());
            for (size_t i = capacities[1] * 5; i < args.size(); ++i)
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
