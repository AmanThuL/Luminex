#include "Engine/Lights/LocalLightMath.h"
#include "Engine/Scene/Scene.h"
#include "Render/Graph/TransientPool.h"
#include "Render/Passes/LocalLights/LightDebugStage.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"
#include "Scenes/SceneLibrary.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"

#include <algorithm>
#include <cstring>

namespace {
using namespace lmx::render;

struct DebugFixture {
    uint32_t width = 37, height = 23, outputWidth = 61, outputHeight = 41;
    lmx::engine::Camera camera;
    LightClusterParams params;
    glm::mat4 inverseViewProjection{1.0f};
    std::vector<float> depths;
    std::vector<lmx::engine::LightRow> rows;
    LightClusterLists lists;
};

//======================================================================================================================
DebugFixture makeDebugFixture() {
    DebugFixture f;
    f.camera.position = {1, 2, 3};
    f.camera.yaw = -0.3f;
    f.camera.pitch = 0.1f;
    auto projection = f.camera.projectionMatrix(float(f.width) / f.height);
    projection[2][0] = 0.017f;
    projection[2][1] = -0.023f;
    f.params.view = f.camera.viewMatrix();
    f.params.inverseJitteredProjection = glm::inverse(projection);
    f.params.activeWidth = f.width;
    f.params.activeHeight = f.height;
    f.params.sliceDepth = clusterSliceDepths(f.camera.nearZ);
    f.inverseViewProjection = glm::inverse(projection * f.params.view);
    f.depths.resize(f.width * f.height, f.camera.nearZ / 4.0f);
    // Sky, the near sliver, exact slice boundaries and the open far slice all participate.
    for (uint32_t x = 0; x < f.width; ++x)
        f.depths[x] = x % 3 == 0 ? 0.0f : f.params.sliceDepth[x % 25];
    lmx::engine::LocalLight point;
    point.position = glm::vec3(glm::inverse(f.params.view) * glm::vec4(0, 0, -4, 1));
    point.range = 1.3f;
    f.rows.push_back(*lmx::engine::makeLightRow(point));
    lmx::engine::LocalLight spot = point;
    spot.type = lmx::engine::LocalLightType::Spot;
    spot.position = glm::vec3(glm::inverse(f.params.view) * glm::vec4(0, 0, -2, 1));
    spot.direction = glm::vec3(glm::inverse(f.params.view) * glm::vec4(0, 0, -1, 0));
    spot.range = 5;
    spot.innerCone = 0.15f;
    spot.outerCone = 0.38f;
    f.rows.push_back(*lmx::engine::makeLightRow(spot));
    f.rows.push_back({}); // a tombstone must never produce a missing light.
    f.params.rowCount = uint32_t(f.rows.size());
    f.lists = buildLightClusters(f.rows, f.params);
    return f;
}

//======================================================================================================================
std::vector<uint8_t> renderDebug(const DebugFixture& f, lmx::engine::LightDebugView mode) {
    auto gpu = createTestGpu();
    auto stage = LightDebugStage::create(*gpu);
    INFO(errorOf(stage));
    REQUIRE(stage.has_value());
    const rojoRHI::TextureData depthMip{.bytes = std::as_bytes(std::span(f.depths)),
                                        .bytesPerRow = f.width * 4};
    auto depth = lmx::engine::GpuTexture::create(gpu->device(),
                                                 {.format = rojoRHI::Format::R32Float,
                                                  .extent = {f.width, f.height, 1},
                                                  .usage = rojoRHI::TextureUsage::Sampled,
                                                  .label = "lmx.test.lightDebug.depth"},
                                                 std::span{&depthMip, 1});
    REQUIRE(depth.has_value());
    std::vector<uint8_t> source(f.outputWidth * f.outputHeight * 4, 128);
    for (size_t i = 3; i < source.size(); i += 4)
        source[i] = 255;
    const rojoRHI::TextureData sourceMip{.bytes = std::as_bytes(std::span(source)),
                                         .bytesPerRow = f.outputWidth * 4};
    auto display = lmx::engine::GpuTexture::create(gpu->device(),
                                                   {.format = rojoRHI::Format::BGRA8Unorm,
                                                    .extent = {f.outputWidth, f.outputHeight, 1},
                                                    .usage = rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.test.lightDebug.source"},
                                                   std::span{&sourceMip, 1});
    REQUIRE(display.has_value());
    auto output = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.format = rojoRHI::Format::BGRA8Unorm,
                                                   .extent = {f.outputWidth, f.outputHeight, 1},
                                                   .usage = rojoRHI::TextureUsage::RenderTarget,
                                                   .label = "lmx.test.lightDebug.output"});
    REQUIRE(output.has_value());
    auto upload = [&](std::span<const std::byte> bytes, const char* label) {
        return lmx::engine::GpuBuffer::create(
            gpu->device(),
            {.size = bytes.size(), .memory = rojoRHI::Memory::Upload, .label = label}, bytes);
    };
    auto lights = upload(std::as_bytes(std::span(f.rows)), "lmx.test.lightDebug.lights");
    auto grid = upload(std::as_bytes(std::span(f.lists.grid)), "lmx.test.lightDebug.grid");
    const uint32_t dummy = 0;
    auto indices = upload(f.lists.indices.empty() ? std::as_bytes(std::span{&dummy, 1})
                                                  : std::as_bytes(std::span(f.lists.indices)),
                          "lmx.test.lightDebug.indices");
    REQUIRE(lights.has_value());
    REQUIRE(grid.has_value());
    REQUIRE(indices.has_value());
    TextureStateTable states;
    auto gpuFrame1 = gpu->beginFrame();
    auto& commands = gpuFrame1.commands;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const auto result =
        (*stage)->declare(graph, commands,
                          {.mode = mode,
                           .depth = graph.importPersistentTexture(*depth, "depth"),
                           .display = graph.importPersistentTexture(*display, "display"),
                           .output = graph.importPersistentTexture(*output, "output"),
                           .lights = graph.importBuffer(*lights, "lights"),
                           .grid = graph.importBuffer(*grid, "grid"),
                           .indices = graph.importBuffer(*indices, "indices"),
                           .clusters = f.params,
                           .inverseViewProjection = f.inverseViewProjection,
                           .outputWidth = f.outputWidth,
                           .outputHeight = f.outputHeight});
    graph.exportTexture(result);
    auto compiled = graph.compile();
    if (!compiled)
        gpu->submitAndEndFrame(gpuFrame1);
    INFO((compiled ? "" : compiled.error().message));
    REQUIRE(compiled.has_value());
    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(gpuFrame1);
    gpu->waitIdle();
    std::vector<uint8_t> image(source.size());
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, *output, states);
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(image.data(), readbackBytes->data(), image.size());
    }
    return image;
}

//======================================================================================================================
uint32_t pixelCluster(const DebugFixture& f, uint32_t x, uint32_t y) {
    const auto tile = clusterTile({x, y}, {0, 0}, {f.width, f.height});
    return (clusterSlice(f.depths[y * f.width + x], f.params.sliceDepth) * kClusterTilesY +
            tile.y) *
               kClusterTilesX +
           tile.x;
}
} // namespace

//======================================================================================================================
TEST_CASE("light missed view identifies only geometrically reaching missing rows",
          "[gpu][light-debug]") {
    for (const uint32_t missingRow : {0u, 1u}) {
        for (bool truncated : {false, true}) {
            auto f = makeDebugFixture();
            // Delete one point or spot index, retaining each other ascending entry and its real
            // count.
            std::vector<uint32_t> retained;
            for (auto& record : f.lists.grid) {
                const uint32_t begin = uint32_t(retained.size());
                bool removed = false;
                for (uint32_t entry = 0; entry < record.count; ++entry) {
                    const auto row = f.lists.indices[record.offset + entry];
                    if (row == missingRow)
                        removed = true;
                    else
                        retained.push_back(row);
                }
                record.offset = begin;
                record.count = uint32_t(retained.size()) - begin;
                if (truncated && removed)
                    record.count |= kClusterTruncatedBit;
            }
            f.lists.indices = std::move(retained);
            const auto image = renderDebug(f, lmx::engine::LightDebugView::Missed);
            uint32_t marked = 0, black = 0;
            for (uint32_t y = 0; y < f.outputHeight; ++y)
                for (uint32_t x = 0; x < f.outputWidth; ++x) {
                    const uint32_t sx = x * f.width / f.outputWidth,
                                   sy = y * f.height / f.outputHeight;
                    const float depth = f.depths[sy * f.width + sx];
                    const glm::vec4 h = f.inverseViewProjection *
                                        glm::vec4((float(sx) + 0.5f) * 2 / f.width - 1,
                                                  1 - (float(sy) + 0.5f) * 2 / f.height, depth, 1);
                    const bool missing = depth > 0 && lmx::engine::lightReaches(f.rows[missingRow],
                                                                                glm::vec3(h) / h.w);
                    const size_t at = (y * f.outputWidth + x) * 4;
                    REQUIRE(image[at] == 0);
                    REQUIRE(image[at + 1] == (missing && truncated ? 255 : 0));
                    REQUIRE(image[at + 2] == (missing ? 255 : 0));
                    REQUIRE(image[at + 3] == 255);
                    missing ? ++marked : ++black;
                }
            REQUIRE(marked > 0);
            REQUIRE(black > 0);
        }
    }
}

//======================================================================================================================
TEST_CASE("light missed clean lists stay opaque black including sky", "[gpu][light-debug]") {
    const auto image = renderDebug(makeDebugFixture(), lmx::engine::LightDebugView::Missed);
    for (size_t i = 0; i < image.size(); i += 4) {
        REQUIRE(image[i] == 0);
        REQUIRE(image[i + 1] == 0);
        REQUIRE(image[i + 2] == 0);
        REQUIRE(image[i + 3] == 255);
    }
}

//======================================================================================================================
TEST_CASE("light count and overflow views use exact pixel-aligned froxel records",
          "[gpu][light-debug]") {
    auto f = makeDebugFixture();
    constexpr std::array<uint32_t, 7> counts{0, 1, 4, 5, 16, 64, 128};
    for (uint32_t i = 0; i < kClusterCount; ++i)
        f.lists.grid[i].count = counts[i % counts.size()] | (i % 3 == 0 ? kClusterTruncatedBit : 0);
    for (auto mode : {lmx::engine::LightDebugView::Count, lmx::engine::LightDebugView::Overflow}) {
        const auto image = renderDebug(f, mode);
        for (uint32_t y = 0; y < f.outputHeight; ++y)
            for (uint32_t x = 0; x < f.outputWidth; ++x) {
                const uint32_t sx = x * f.width / f.outputWidth, sy = y * f.height / f.outputHeight;
                const auto record = f.lists.grid[pixelCluster(f, sx, sy)];
                std::array<uint8_t, 4> expected{0, 0, 0, 255};
                if (f.depths[sy * f.width + sx] > 0) {
                    const uint32_t count = record.count & ~kClusterTruncatedBit;
                    if (mode == lmx::engine::LightDebugView::Overflow)
                        expected = record.count & kClusterTruncatedBit
                                       ? std::array<uint8_t, 4>{255, 0, 255, 255}
                                       : std::array<uint8_t, 4>{32, 32, 32, 255};
                    else if (count > 64)
                        expected = {0, 0, 255, 255};
                    else if (count > 16)
                        expected = {0, 255, 255, 255};
                    else if (count > 4)
                        expected = {0, 255, 0, 255};
                    else if (count > 0)
                        expected = {255, 0, 0, 255};
                }
                for (uint32_t c = 0; c < 4; ++c)
                    REQUIRE(image[(y * f.outputWidth + x) * 4 + c] == expected[c]);
            }
    }
}

//======================================================================================================================
TEST_CASE("zero-live light debug leaves the graph unchanged after removal", "[gpu][light-debug]") {
    auto gpu = createTestGpu();
    auto scene = lmx::test::loadCatalogScene(gpu->device(), "light-lab", 64, 0);
    REQUIRE(scene.has_value());
    const auto ids = (*scene)->localLights();
    const std::vector<lmx::engine::LightId> removed(ids.begin(), ids.end());
    for (const auto id : removed)
        REQUIRE((*scene)->removeLight(id));
    auto renderer = Renderer::create(*gpu, 64, 48, true);
    REQUIRE(renderer.has_value());
    auto gpuFrame3 = gpu->beginFrame();
    auto& commands = gpuFrame3.commands;
    REQUIRE((*scene)->prepareFrame(gpu->frameNumber()).has_value());
    std::vector<lmx::engine::DrawItem> items;
    auto view = buildSceneView(**scene, items, false);
    view.localLightMode = lmx::engine::LocalLightMode::Clustered;
    view.lightDebugView = lmx::engine::LightDebugView::Missed;
    view.temporal.enabled = false;
    REQUIRE(view.tables.liveLightCount == 0);
    REQUIRE(view.tables.lightRowCount == 64);
    TransientPool pool(*gpu);
    pool.beginFrame();
    RenderGraph graph(pool);
    const auto declared = (*renderer)->declarePasses(
        graph, commands, lmx::engine::cameraFromScene((*scene)->initialCamera), view);
    if (!declared)
        gpu->submitAndEndFrame(gpuFrame3);
    INFO(errorOf(declared));
    REQUIRE(declared.has_value());
    graph.exportTexture(*declared);
    const auto record = graph.compileFrame(gpu->frameNumber());
    if (!record)
        gpu->submitAndEndFrame(gpuFrame3);
    REQUIRE(record.has_value());
    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(gpuFrame3);
    gpu->waitIdle();
    for (const auto& pass : record->debug.passes)
        REQUIRE_FALSE(pass.label.starts_with("lmx.pass.light."));
}
