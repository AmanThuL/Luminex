#include "Render/Graph/GraphDump.h"
#include "Support/GoldenFile.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"

#include "Engine/Scene/Scene.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"

#include <algorithm>

//======================================================================================================================
TEST_CASE("occlusion graph roots every HZB mip and reads the immediately preceding pyramid",
          "[gpu][hzb][occlusion-graph]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    using lmx::test::requireMatchesGolden;
    auto gpu = createTestGpu();
    auto scene = lmx::test::loadCatalogScene(gpu->device(), "visibility-lab", 1024);
    REQUIRE(scene);
    auto first = Renderer::create(*gpu, 64, 64, true);
    REQUIRE(first);
    auto steady = Renderer::create(*gpu, 64, 64, true);
    REQUIRE(steady);
    TransientPool pool(*gpu);
    const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
    // The display target carries a shader-read final use, which a presenting graph cannot lower,
    // so the dumped frames compile without executing, as the temporal goldens do. The first dump
    // is one renderer's first frame; the steady dump is another renderer's second frame, after its
    // first frame executed and recorded the pyramid chain and publication it reads.
    for (uint32_t frame = 0; frame < 3; ++frame) {
        const bool executed = frame == 1;
        const uint32_t dumpedFrame = frame == 0 ? 0 : 1;
        auto& renderer = frame == 0 ? first : steady;
        auto gpuFrame1 = gpu->beginFrame();
        auto& commands = gpuFrame1.commands;
        REQUIRE((*scene)->prepareFrame(gpu->frameNumber()));
        std::vector<lmx::engine::DrawItem> items;
        auto view = buildSceneView(**scene, items, false);
        view.classifyMode = ClassifyMode::Gpu;
        view.submission = SubmissionMode::Indirect;
        view.occlusionEnabled = true;
        view.bloomEnabled = false;
        pool.beginFrame();
        RenderGraph graph(pool);
        const auto declared = (*renderer)->declarePasses(graph, commands, camera, view);
        if (!declared)
            gpu->submitAndEndFrame(gpuFrame1);
        REQUIRE(declared);
        if (executed) {
            graph.exportTexture(*declared);
            graph.execute(commands, gpu->frameNumber());
            gpu->submitAndEndFrame(gpuFrame1);
            (*scene)->commitFrame();
            gpu->waitIdle();
            continue;
        }
        graph.presentTexture(*declared);
        const auto record = graph.compileFrame(dumpedFrame + 1);
        INFO((record ? "" : record.error().message));
        REQUIRE(record);
        const auto& debug = record->debug;
        for (const auto label :
             {"lmx.pass.hzb.level0", "lmx.pass.hzb.level1", "lmx.pass.hzb.publish"}) {
            const auto pass = std::ranges::find(debug.passes, label, &DebugPass::label);
            REQUIRE(pass != debug.passes.end());
            REQUIRE_FALSE(pass->cullReason);
        }
        const auto classify =
            std::ranges::find(debug.passes, "lmx.pass.visibility.classify", &DebugPass::label);
        REQUIRE(classify != debug.passes.end());
        const auto sourceName = dumpedFrame == 0 ? "lmx.render.hzb1" : "lmx.render.hzb0";
        REQUIRE(std::ranges::any_of(classify->uses, [&](const auto& use) {
            return debug.resources[use.resource].name == sourceName &&
                   use.role == UseRole::ShaderRead;
        }));
        gpu->submitAndEndFrame(gpuFrame1);
        (*scene)->commitFrame();
        gpu->waitIdle();
        requireMatchesGolden(dumpCompiledFrame(*record), dumpedFrame == 0
                                                             ? "frame-occlusion-first.txt"
                                                             : "frame-occlusion-steady.txt");
    }
}
