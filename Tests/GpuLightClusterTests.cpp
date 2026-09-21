#include "GpuTestSupport.h"

#include "Engine/Types/LocalLight.h"
#include "Engine/Types/LocalLightMath.h"
#include "Render/LightClusterStage.h"
#include "Render/LightClusters.h"

#include <algorithm>
#include <format>

namespace {
using namespace lmx::render;

// A camera plus the jittered projection the scene pass would rasterize with; the same shape
// LightClustersTests.cpp builds its mirror inputs from.
struct TestView {
    Camera camera;
    glm::mat4 projection{1.0f};
    uint32_t width = 0;
    uint32_t height = 0;
};

//======================================================================================================================
TestView makeView(glm::vec3 position, float yaw, float pitch, float nearZ, uint32_t width,
                  uint32_t height, glm::vec2 jitter) {
    TestView view;
    view.camera.position = position;
    view.camera.yaw = yaw;
    view.camera.pitch = pitch;
    view.camera.nearZ = nearZ;
    view.camera.fovY = glm::radians(60.0f);
    view.width = width;
    view.height = height;
    view.projection = view.camera.projectionMatrix(float(width) / float(height));
    // Temporal jitter is a clip-space shear proportional to w, exactly as TemporalResolve applies.
    view.projection[2][0] = jitter.x;
    view.projection[2][1] = jitter.y;
    return view;
}

//======================================================================================================================
LightClusterParams makeParams(const TestView& view, uint32_t rowCount) {
    LightClusterParams params;
    params.view = view.camera.viewMatrix();
    params.inverseJitteredProjection = glm::inverse(view.projection);
    params.rowCount = rowCount;
    params.activeWidth = view.width;
    params.activeHeight = view.height;
    params.sliceDepth = clusterSliceDepths(view.camera.nearZ);
    return params;
}

//======================================================================================================================
// The view-space point a pixel centre reconstructs to at `distance`, derived from the projection's
// own coefficients rather than from the mirror's unprojection.
glm::vec3 pixelCentreViewPoint(const TestView& view, glm::uvec2 pixel, float distance) {
    const double ndcX = (double(pixel.x) + 0.5) / double(view.width) * 2.0 - 1.0;
    const double ndcY = 1.0 - (double(pixel.y) + 0.5) / double(view.height) * 2.0;
    const double x =
        (ndcX + double(view.projection[2][0])) * double(distance) / double(view.projection[0][0]);
    const double y =
        (ndcY + double(view.projection[2][1])) * double(distance) / double(view.projection[1][1]);
    return {float(x), float(y), -distance};
}

//======================================================================================================================
LightRow makePoint(glm::vec3 position, float range) {
    LocalLight light;
    light.position = position;
    light.range = range;
    const auto row = makeLightRow(light);
    REQUIRE(row.has_value());
    return *row;
}

//======================================================================================================================
// A tiny point light sitting exactly on a pixel centre at `distance`, in world space.
LightRow makeProbe(const TestView& view, glm::uvec2 pixel, float distance, float range) {
    const glm::vec3 viewPoint = pixelCentreViewPoint(view, pixel, distance);
    const glm::mat4 viewToWorld = glm::inverse(view.camera.viewMatrix());
    return makePoint(glm::vec3{viewToWorld * glm::vec4(viewPoint, 1.0f)}, range);
}

//======================================================================================================================
uint32_t froxelIndex(glm::uvec2 tile, uint32_t slice) {
    return (slice * kClusterTilesY + tile.y) * kClusterTilesX + tile.x;
}

//======================================================================================================================
uint32_t liveRows(const std::vector<LightRow>& rows, uint32_t rowCount) {
    uint32_t live = 0;
    for (uint32_t row = 0; row < rowCount; ++row) {
        live += rows[row].boundRadius > 0.0f ? 1u : 0u;
    }
    return live;
}

// Deterministic 64-bit LCG; the light field must not depend on a platform generator.
struct Rng {
    uint64_t state = 0;
};

//======================================================================================================================
double nextUnit(Rng& rng) {
    rng.state = rng.state * 6364136223846793005ull + 1442695040888963407ull;
    return double((rng.state >> 11) & ((1ull << 53) - 1)) / double(1ull << 53);
}

//======================================================================================================================
double nextRange(Rng& rng, double low, double high) {
    return low + (high - low) * nextUnit(rng);
}

//======================================================================================================================
// 256 mixed point and spot lights spread over the volume the views below look into. LightLab does
// not exist yet, so the field is built here and is a pure function of its seed.
std::vector<LightRow> deterministicLightField(uint64_t seed) {
    Rng rng{seed};
    std::vector<LightRow> rows;
    for (uint32_t i = 0; i < 256; ++i) {
        LocalLight light;
        light.position = {float(nextRange(rng, -25.0, 25.0)), float(nextRange(rng, -8.0, 12.0)),
                          float(nextRange(rng, -45.0, 10.0))};
        light.range = float(nextRange(rng, 0.3, 4.0));
        if (i % 3 == 0) {
            light.type = LocalLightType::Spot;
            const glm::vec3 direction{float(nextRange(rng, -1.0, 1.0)),
                                      float(nextRange(rng, -1.0, 1.0)),
                                      float(nextRange(rng, -1.0, 1.0))};
            light.direction = glm::length(direction) > 1e-3f ? glm::normalize(direction)
                                                             : glm::vec3(0.0f, -1.0f, 0.0f);
            light.innerCone = float(nextRange(rng, 0.05, 0.3));
            light.outerCone = light.innerCone + float(nextRange(rng, 0.05, 0.9));
        }
        const auto row = makeLightRow(light);
        REQUIRE(row.has_value());
        rows.push_back(*row);
    }
    return rows;
}

//======================================================================================================================
LightClusterInputs inputsFrom(const LightClusterParams& params, GraphBuffer lights, uint32_t live,
                              uint64_t frame) {
    return {.clustered = true,
            .liveLightCount = live,
            .rowCount = params.rowCount,
            .lights = lights,
            .view = params.view,
            .inverseJitteredProjection = params.inverseJitteredProjection,
            .sliceDepth = params.sliceDepth,
            .activeWidth = params.activeWidth,
            .activeHeight = params.activeHeight,
            .frameNumber = frame,
            .captureLists = true};
}

// One built grid from each side of the contract.
struct ClusterRun {
    LightClusterLists mirror;
    RetiredLightClusters gpu;
};

//======================================================================================================================
// Declares, executes and retires one clustering frame, and builds the mirror over the same inputs.
ClusterRun runClusters(rojoRHI::Device& device, LightClusterStage& stage,
                       const std::vector<LightRow>& rows, const LightClusterParams& params) {
    auto lights = device.createBuffer({.size = rows.size() * sizeof(LightRow),
                                       .storageRead = true,
                                       .label = "lmx.test.lightCluster.rows"},
                                      rows.data());
    INFO(errorOf(lights));
    REQUIRE(lights);
    auto& commands = device.beginFrame();
    const uint64_t frame = device.frameNumber();
    RenderGraph graph;
    const GraphBuffer imported = graph.importBuffer(**lights, "lmx.test.lightCluster.rows");
    const auto outputs = stage.declare(
        graph, commands, inputsFrom(params, imported, liveRows(rows, params.rowCount), frame));
    REQUIRE(outputs.declared);
    const auto compiled = graph.compile();
    INFO((compiled ? std::string{} : compiled.error().message));
    REQUIRE(compiled);
    graph.execute(commands, frame);
    device.endFrame(nullptr);
    device.waitIdle();
    stage.retireThrough(frame);
    auto retired = stage.takeRetired();
    REQUIRE(retired.size() == 1);
    return {buildLightClusters(rows, params), std::move(retired.front())};
}

//======================================================================================================================
// Exact equality of every published byte: the six counters, all kClusterCount records, and the
// defined prefix of the index list. Nothing here is a tolerance.
void requireMatchesMirror(const ClusterRun& run) {
    const auto& expected = run.mirror.counters;
    const auto& actual = run.gpu.counters;
    REQUIRE(actual.candidates == expected.candidates);
    REQUIRE(actual.assigned == expected.assigned);
    REQUIRE(actual.droppedPerCluster == expected.droppedPerCluster);
    REQUIRE(actual.droppedGlobal == expected.droppedGlobal);
    REQUIRE(actual.truncatedFroxels == expected.truncatedFroxels);
    REQUIRE(actual.maxCount == expected.maxCount);
    REQUIRE(actual.assigned + actual.droppedPerCluster + actual.droppedGlobal == actual.candidates);

    REQUIRE(run.gpu.grid.size() == run.mirror.grid.size());
    for (uint32_t froxel = 0; froxel < kClusterCount; ++froxel) {
        const ClusterRecord& mine = run.gpu.grid[froxel];
        const ClusterRecord& theirs = run.mirror.grid[froxel];
        if (mine.offset != theirs.offset || mine.count != theirs.count) {
            INFO(std::format("froxel {} (x {}, y {}, slice {})", froxel, froxel % kClusterTilesX,
                             (froxel / kClusterTilesX) % kClusterTilesY,
                             froxel / (kClusterTilesX * kClusterTilesY)));
            REQUIRE(mine.offset == theirs.offset);
            REQUIRE(mine.count == theirs.count);
        }
    }
    REQUIRE(run.gpu.indices.size() == run.mirror.indices.size());
    for (size_t entry = 0; entry < run.mirror.indices.size(); ++entry) {
        if (run.gpu.indices[entry] != run.mirror.indices[entry]) {
            INFO(std::format("index entry {}", entry));
            REQUIRE(run.gpu.indices[entry] == run.mirror.indices[entry]);
        }
    }
}

//======================================================================================================================
std::unique_ptr<LightClusterStage> makeStage(rojoRHI::Device& device) {
    auto stage = LightClusterStage::create(device);
    INFO(errorOf(stage));
    REQUIRE(stage);
    return std::move(*stage);
}

} // namespace

//======================================================================================================================
TEST_CASE("the GPU froxel grid equals the mirror over a 256-light field", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);
    auto rows = deterministicLightField(0x9E3779B97F4A7C15ull);
    // A free slot must never be listed, on either side.
    rows.push_back(LightRow{});

    // A 1280x720 output, the same output at half render scale, and an odd extent that divides
    // neither 16 nor 9, so the pixel-aligned rectangles include straddling tiles.
    const TestView views[3] = {
        makeView({0.0f, 1.5f, 6.0f}, 0.0f, -0.1f, 0.1f, 1280, 720, {0.0f, 0.0f}),
        makeView({0.0f, 1.5f, 6.0f}, 0.0f, -0.1f, 0.1f, 640, 360, {0.0021f, -0.0013f}),
        makeView({-3.0f, 2.0f, 2.0f}, -0.7f, 0.05f, 0.08f, 1283, 721, {-0.0017f, 0.0009f})};
    for (const auto& view : views) {
        INFO(std::format("extent {}x{}", view.width, view.height));
        const auto params = makeParams(view, uint32_t(rows.size()));
        const auto run = runClusters(**device, *stage, rows, params);
        // Nonvacuous, and not swamped by either overflow: this case is about the geometry.
        REQUIRE(run.mirror.counters.assigned > 1000);
        REQUIRE(run.mirror.counters.droppedPerCluster == 0);
        REQUIRE(run.mirror.counters.droppedGlobal == 0);
        requireMatchesMirror(run);
    }
}

//======================================================================================================================
// One guard on each side of a froxel face and of a slice boundary. The pixel-aligned rectangle and
// the reversed-Z table are exactly where fp32 rounding could make the two implementations part.
TEST_CASE("froxel face and slice boundary probes equal the mirror", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);

    // 100 columns over 16 tiles: tile 0 owns pixels 0-6 and tile 1 starts at pixel 7, so pixel 6's
    // centre lies past tile 0's exact fractional edge.
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 100, 57, {0.0f, 0.0f});
    REQUIRE(clusterTileEdges(0, kClusterTilesX, view.width) == glm::uvec2{0, 7});
    auto params = makeParams(view, 0);

    const uint32_t slice = 12;
    const float nearDistance = view.camera.nearZ / params.sliceDepth[slice];
    const float farDistance = view.camera.nearZ / params.sliceDepth[slice + 1];
    // A millimetre light is far narrower than the half pixel separating the two tiles at this
    // distance, so each face probe belongs to exactly one froxel column.
    std::vector<LightRow> rows{
        makeProbe(view, {6, 3}, nearDistance, 0.001f),           // the last pixel of tile 0
        makeProbe(view, {7, 3}, nearDistance, 0.001f),           // the first pixel of tile 1
        makeProbe(view, {40, 20}, nearDistance, 0.001f),         // on the slice's near boundary
        makeProbe(view, {40, 20}, farDistance, 0.001f),          // on its far boundary
        makeProbe(view, {40, 20}, farDistance * 1.02f, 0.001f)}; // one guard past it
    params.rowCount = uint32_t(rows.size());
    const auto run = runClusters(**device, *stage, rows, params);

    // The probes must actually discriminate: each lands in its own tile, and the boundary probes
    // land in neighbouring slices.
    const glm::uvec2 extent{view.width, view.height};
    const auto listed = [&](const LightClusterLists& lists, glm::uvec2 tile, uint32_t depthSlice,
                            uint32_t row) {
        const auto& record = lists.grid[froxelIndex(tile, depthSlice)];
        for (uint32_t i = 0; i < (record.count & ~kClusterTruncatedBit); ++i) {
            if (lists.indices[record.offset + i] == row) {
                return true;
            }
        }
        return false;
    };
    // Each face probe reaches the froxel of the pixel that owns it -- including pixel 6, whose
    // centre lies past tile 0's exact fractional edge -- and neither reaches across the grid.
    // Neighbouring froxel boxes genuinely overlap, since each is the AABB of a widening frustum,
    // so the guard that discriminates is a distant column rather than the next one.
    REQUIRE(clusterTile({6, 3}, {0, 0}, extent) == glm::uvec2{0, 0});
    REQUIRE(clusterTile({7, 3}, {0, 0}, extent) == glm::uvec2{1, 0});
    REQUIRE(listed(run.mirror, {0, 0}, slice, 0));
    REQUIRE(listed(run.mirror, {1, 0}, slice, 1));
    REQUIRE_FALSE(listed(run.mirror, {8, 4}, slice, 0));
    REQUIRE_FALSE(listed(run.mirror, {8, 4}, slice, 1));
    // The boundary probes land in neighbouring slices, and the guard past the far boundary is not
    // claimed by the slice it left.
    REQUIRE(clusterSlice(params.sliceDepth[slice], params.sliceDepth) == slice);
    REQUIRE(clusterSlice(params.sliceDepth[slice + 1], params.sliceDepth) == slice + 1);
    const glm::uvec2 centreTile = clusterTile({40, 20}, {0, 0}, extent);
    REQUIRE(listed(run.mirror, centreTile, slice, 2));
    REQUIRE(listed(run.mirror, centreTile, slice + 1, 3));
    REQUIRE(listed(run.mirror, centreTile, slice + 1, 4));
    REQUIRE_FALSE(listed(run.mirror, centreTile, slice, 4));
    requireMatchesMirror(run);
}

//======================================================================================================================
TEST_CASE("the open slice's conservative rule equals the mirror on the GPU",
          "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);

    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    const std::vector<LightRow> rows{
        makePoint({0.0f, 0.0f, -500.0f}, 20.0f),   // far, on the view axis
        makeProbe(view, {1150, 40}, 400.0f, 1.0f), // far, off axis on one side
        makeProbe(view, {130, 680}, 400.0f, 1.0f), // far, off axis on the other
        makePoint({0.0f, 0.0f, -50.0f}, 10.0f),    // ends before the open slice begins
        makePoint({0.0f, 0.0f, -90.0f}, 10.5f)};   // just reaches past 100 m
    const auto params = makeParams(view, uint32_t(rows.size()));
    const auto run = runClusters(**device, *stage, rows, params);

    const uint32_t open = kClusterSliceCount - 1;
    const auto& axis = run.mirror.grid[froxelIndex({8, 4}, open)];
    REQUIRE((axis.count & ~kClusterTruncatedBit) > 0);
    requireMatchesMirror(run);
}

//======================================================================================================================
TEST_CASE("a near plane collapsing slices equals the mirror on the GPU", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);

    // nearZ 0.5 is past the first exponential boundaries, so the slices they close are degenerate.
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.5f, 1280, 720, {0.0f, 0.0f});
    const std::vector<LightRow> rows{makePoint({0.0f, 0.0f, 0.0f}, 60.0f)};
    const auto params = makeParams(view, 1);
    uint32_t degenerate = 0;
    for (uint32_t slice = 0; slice + 1 < kClusterSliceCount; ++slice) {
        degenerate += params.sliceDepth[slice] == params.sliceDepth[slice + 1] ? 1u : 0u;
    }
    REQUIRE(degenerate >= 1);

    const auto run = runClusters(**device, *stage, rows, params);
    REQUIRE(run.mirror.counters.assigned > 0);
    requireMatchesMirror(run);
}

//======================================================================================================================
TEST_CASE("a tile owning no pixel lists nothing on the GPU either", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);

    // Ten columns over sixteen tiles: six tiles own no pixel at all.
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 10, 8, {0.0f, 0.0f});
    const std::vector<LightRow> rows{makePoint({0.0f, 0.0f, 0.0f}, 60.0f)};
    const auto params = makeParams(view, 1);
    const auto run = runClusters(**device, *stage, rows, params);
    REQUIRE(run.mirror.counters.assigned > 0);
    requireMatchesMirror(run);
}

//======================================================================================================================
TEST_CASE("capacity overrides reproduce both overflow kinds on the GPU", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);

    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    std::vector<LightRow> rows;
    for (uint32_t i = 0; i < 10; ++i) {
        rows.push_back(makePoint({0.0f, 0.0f, 0.0f}, 1000.0f));
    }

    SECTION("per-cluster overflow keeps the lowest rows") {
        stage->setCapacityOverride(4, kLightClusterIndexCapacity);
        auto params = makeParams(view, uint32_t(rows.size()));
        params.perClusterCap = 4;
        const auto run = runClusters(**device, *stage, rows, params);
        REQUIRE(run.mirror.counters.droppedPerCluster == kClusterCount * 6);
        REQUIRE(run.mirror.counters.droppedGlobal == 0);
        requireMatchesMirror(run);
    }
    SECTION("global overflow truncates the first non-fitting froxel") {
        stage->setCapacityOverride(kMaxLightsPerCluster, 95);
        auto params = makeParams(view, uint32_t(rows.size()));
        params.globalCapacity = 95;
        const auto run = runClusters(**device, *stage, rows, params);
        REQUIRE(run.mirror.counters.assigned == 95);
        REQUIRE(run.mirror.counters.droppedGlobal == kClusterCount * 10 - 95);
        REQUIRE(run.mirror.counters.truncatedFroxels == kClusterCount - 9);
        requireMatchesMirror(run);
    }
    SECTION("both caps at once") {
        stage->setCapacityOverride(3, 40);
        auto params = makeParams(view, uint32_t(rows.size()));
        params.perClusterCap = 3;
        params.globalCapacity = 40;
        const auto run = runClusters(**device, *stage, rows, params);
        REQUIRE(run.mirror.counters.droppedPerCluster > 0);
        REQUIRE(run.mirror.counters.droppedGlobal > 0);
        requireMatchesMirror(run);
    }
}

//======================================================================================================================
TEST_CASE("the stage declares nothing without clustered lights", "[gpu][light-cluster]") {
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = makeStage(**device);
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    const std::vector<LightRow> rows{makePoint({0.0f, 0.0f, -2.0f}, 4.0f)};
    const auto params = makeParams(view, 1);

    auto lights = (*device)->createBuffer(
        {.size = sizeof(LightRow), .storageRead = true, .label = "lmx.test.lightCluster.rows"},
        rows.data());
    REQUIRE(lights);
    for (const bool clustered : {false, true}) {
        RenderGraph graph;
        const GraphBuffer imported = graph.importBuffer(**lights, "lmx.test.lightCluster.rows");
        // Clustered with no live light, and one live light with the mode off: neither declares.
        auto inputs = inputsFrom(params, imported, clustered ? 0u : 1u, 0);
        inputs.clustered = clustered;
        auto& commands = (*device)->beginFrame();
        const auto outputs = stage->declare(graph, commands, inputs);
        REQUIRE_FALSE(outputs.declared);
        const auto compiled = graph.compile();
        INFO((compiled ? std::string{} : compiled.error().message));
        REQUIRE(compiled);
        REQUIRE(compiled->passes.empty());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
    }
    stage->retireThrough(1000);
    REQUIRE(stage->takeRetired().empty());
}

//======================================================================================================================
TEST_CASE("paced cluster slots retain exact shrinking lists and raster barriers",
          "[gpu][light-cluster][clustered-consumer]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto stage = makeStage(**device);
    const auto view = makeView({0.0f, 1.5f, 6.0f}, 0.0f, -0.1f, 0.1f, 101, 59, {0, 0});
    auto rows = deterministicLightField(0x9E3779B97F4A7C15ull);
    const std::array<uint32_t, 6> counts{256, 192, 128, 64, 8, 1};
    std::vector<LightClusterLists> expected;
    std::vector<std::unique_ptr<rojoRHI::Buffer>> storage;
    auto target = (*device)->createTexture({.width = 1,
                                            .height = 1,
                                            .format = rojoRHI::Format::RGBA8Unorm,
                                            .renderTarget = true,
                                            .label = "lmx.test.clusterRasterConsumer"});
    REQUIRE(target);
    uint32_t retiredCount = 0;
    uint64_t firstFrame = 0;
    const auto checkRetired = [&] {
        for (auto& retired : stage->takeRetired()) {
            REQUIRE(retired.frameNumber >= firstFrame);
            REQUIRE(retired.frameNumber - firstFrame < expected.size());
            requireMatchesMirror({expected[retired.frameNumber - firstFrame], std::move(retired)});
            ++retiredCount;
        }
    };
    uint32_t step = 0;
    for (const uint32_t count : counts) {
        const auto params = makeParams(view, count);
        expected.push_back(buildLightClusters(rows, params));
        auto buffer = (*device)->createBuffer({.size = rows.size() * sizeof(LightRow),
                                               .storageRead = true,
                                               .label = "lmx.test.shrinkingLightRows"},
                                              rows.data());
        REQUIRE(buffer);
        storage.push_back(std::move(*buffer));
        auto& commands = (*device)->beginFrame();
        const auto frame = (*device)->frameNumber();
        if (step == 0)
            firstFrame = frame;
        if (frame >= 3) {
            stage->retireThrough(frame - 3);
            checkRetired();
            REQUIRE(retiredCount == (step < 3 ? 0 : step - 2));
        }
        RenderGraph graph;
        auto inputs =
            inputsFrom(params, graph.importBuffer(*storage.back(), "lmx.test.rows"), count, frame);
        inputs.shaderReadsOutputs = true;
        const auto output = stage->declare(graph, commands, inputs);
        auto colour = graph.importTexture(**target, rojoRHI::Format::RGBA8Unorm, "lmx.test.colour");
        PassDesc consumer;
        consumer.bufferReads = {output.grid, output.indices};
        consumer.color = ColorAttachment{.handle = colour};
        graph.addPass("lmx.test.consumeLights", std::move(consumer), [](const PassResources&) {});
        graph.exportTexture(nextVersion(colour));
        const auto record = graph.compileFrame(frame);
        REQUIRE(record);
        if (step >= 3) {
            for (const auto name : {"lmx.light.grid", "lmx.light.indices"}) {
                REQUIRE(std::ranges::any_of(record->debug.transitions, [&](const auto& barrier) {
                    return record->debug.resources[barrier.resource].name == name &&
                           barrier.bufferFrom == rojoRHI::BufferUse::ShaderRead &&
                           barrier.bufferTo == rojoRHI::BufferUse::StorageWrite;
                }));
            }
        }
        graph.execute(commands, frame);
        (*device)->endFrame(nullptr);
        ++step;
    }
    (*device)->waitIdle();
    stage->retireThrough((*device)->frameNumber());
    checkRetired();
    REQUIRE(retiredCount == counts.size());
}
