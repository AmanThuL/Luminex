#include "Engine/Scene/Scene.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"
#include "Support/GpuTemporalTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"

#include <algorithm>

//======================================================================================================================
TEST_CASE("GPU visibility declares writers and precise draw consumers in both layouts",
          "[gpu][visibility][gpu-classify][visibility-graph]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        auto gpu = createTestGpu();
        auto scene = lmx::test::loadCatalogScene(gpu->device(), "visibility-lab", 1024);
        REQUIRE(scene);
        auto renderer = Renderer::create(*gpu, kSize, kSize, true);
        REQUIRE(renderer);
        TransientPool pool(gpu->device());
        auto& commands = gpu->device().beginFrame();
        REQUIRE((*scene)->prepareFrame(gpu->device().frameNumber()));
        std::vector<lmx::engine::DrawItem> items;
        auto view = buildSceneView(**scene, items, false);
        view.classifyMode = ClassifyMode::Gpu;
        view.submission = mode;
        view.bloomEnabled = false;
        pool.beginFrame();
        RenderGraph graph(pool);
        const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
        graph.presentTexture((*renderer)->declarePasses(graph, commands, camera, view));
        const auto record = graph.compileFrame(gpu->device().frameNumber());
        INFO((record ? "" : record.error().message));
        REQUIRE(record);
        const auto& debug = record->debug;
        for (const auto label : {"lmx.pass.visibility.reset", "lmx.pass.visibility.classify",
                                 "lmx.pass.visibility.emit"}) {
            const auto pass = std::ranges::find(debug.passes, label, &DebugPass::label);
            REQUIRE(pass != debug.passes.end());
            REQUIRE_FALSE(pass->cullReason);
        }
        const auto scan =
            std::ranges::find(debug.passes, "lmx.pass.visibility.scan", &DebugPass::label);
        REQUIRE((scan != debug.passes.end()) == (mode == SubmissionMode::Batched));
        if (scan != debug.passes.end())
            REQUIRE_FALSE(scan->cullReason);
        const auto transition = [&](std::string_view name, lmx::render::BufferUse to) {
            return std::ranges::any_of(debug.transitions, [&](const auto& barrier) {
                return debug.resources[barrier.resource].name == name &&
                       barrier.kind == GraphResourceKind::Buffer &&
                       barrier.bufferFrom == lmx::render::BufferUse::StorageWrite &&
                       barrier.bufferTo == to;
            });
        };
        REQUIRE(transition("lmx.draw.rows", lmx::render::BufferUse::ShaderRead));
        REQUIRE(transition("lmx.draw.args", lmx::render::BufferUse::IndirectArgument));
        graph.execute(commands, gpu->device().frameNumber());
        gpu->device().endFrame(nullptr);
        gpu->device().waitIdle();
        const auto name = mode == SubmissionMode::Batched ? "frame-visibility-batched.txt"
                                                          : "frame-visibility-indirect.txt";
        requireMatchesGolden(dumpCompiledFrame(*record), name);
    }
}

//======================================================================================================================
TEST_CASE("empty GPU views preserve readable diagnostics as graph sinks",
          "[gpu][visibility][gpu-classify][visibility-graph]") {
    using namespace lmx::render;
    auto gpu = createTestGpu();
    auto scene = lmx::test::loadCatalogScene(gpu->device(), "visibility-lab", 5);
    REQUIRE(scene);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto& commands = gpu->device().beginFrame();
    REQUIRE((*scene)->prepareFrame(gpu->device().frameNumber()));
    std::vector<lmx::engine::DrawItem> items;
    auto view = buildSceneView(**scene, items, false);
    view.items = {};
    view.classifyMode = ClassifyMode::Gpu;
    view.submission = SubmissionMode::Batched;
    view.bloomEnabled = false;
    TransientPool pool(gpu->device());
    pool.beginFrame();
    RenderGraph graph(pool);
    graph.exportTexture((*renderer)->declarePasses(graph, commands, lmx::engine::Camera{}, view));
    const auto record = graph.compileFrame(gpu->device().frameNumber());
    INFO((record ? "" : record.error().message));
    REQUIRE(record);
    const auto& debug = record->debug;
    const auto reset =
        std::ranges::find(debug.passes, "lmx.pass.visibility.reset", &DebugPass::label);
    REQUIRE(reset != debug.passes.end());
    REQUIRE_FALSE(reset->cullReason);
    REQUIRE(std::ranges::any_of(debug.sinks, [&](const auto& sink) {
        return debug.resources[sink.resource].name == "lmx.draw.counters" &&
               sink.kind == SinkKind::Readback;
    }));
    graph.execute(commands, gpu->device().frameNumber());
    gpu->device().endFrame(nullptr);
    gpu->device().waitIdle();
}
