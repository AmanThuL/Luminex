#include "Engine/Scene/Scene.h"
#include "GpuTemporalTestSupport.h"
#include "Render/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"

#include <algorithm>

//======================================================================================================================
TEST_CASE("GPU visibility declares writers and precise draw consumers in both layouts",
          "[gpu][visibility][gpu-classify][visibility-graph]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        auto device = createDevice();
        REQUIRE(device);
        auto scene = lmx::scenes::loadVisibilityLabScene(**device, 1024);
        REQUIRE(scene);
        auto renderer = Renderer::create(**device, kSize, kSize, true);
        REQUIRE(renderer);
        TransientPool pool(**device);
        auto& commands = (*device)->beginFrame();
        REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
        std::vector<lmx::engine::DrawItem> items;
        auto view = buildSceneView(**scene, items, ShadowFilter::PCF, false);
        view.classifyMode = ClassifyMode::Gpu;
        view.submission = mode;
        view.bloomEnabled = false;
        pool.beginFrame();
        RenderGraph graph(pool);
        const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
        graph.presentTexture((*renderer)->declarePasses(graph, commands, camera, view));
        const auto record = graph.compileFrame((*device)->frameNumber());
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
        const auto transition = [&](std::string_view name, BufferUse to) {
            return std::ranges::any_of(debug.transitions, [&](const auto& barrier) {
                return debug.resources[barrier.resource].name == name &&
                       barrier.kind == GraphResourceKind::Buffer &&
                       barrier.bufferFrom == BufferUse::StorageWrite && barrier.bufferTo == to;
            });
        };
        REQUIRE(transition("lmx.draw.rows", BufferUse::ShaderRead));
        REQUIRE(transition("lmx.draw.args", BufferUse::IndirectArgument));
        graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        const auto name = mode == SubmissionMode::Batched ? "frame-visibility-batched.txt"
                                                          : "frame-visibility-indirect.txt";
        requireMatchesGolden(dumpCompiledFrame(*record), name);
    }
}

//======================================================================================================================
TEST_CASE("empty GPU views preserve readable diagnostics as graph sinks",
          "[gpu][visibility][gpu-classify][visibility-graph]") {
    using namespace lmx::render;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = lmx::scenes::loadVisibilityLabScene(**device, 5);
    REQUIRE(scene);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
    std::vector<lmx::engine::DrawItem> items;
    auto view = buildSceneView(**scene, items, ShadowFilter::PCF, false);
    view.items = {};
    view.classifyMode = ClassifyMode::Gpu;
    view.submission = SubmissionMode::Batched;
    view.bloomEnabled = false;
    TransientPool pool(**device);
    pool.beginFrame();
    RenderGraph graph(pool);
    graph.exportTexture((*renderer)->declarePasses(graph, commands, lmx::engine::Camera{}, view));
    const auto record = graph.compileFrame((*device)->frameNumber());
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
    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}
