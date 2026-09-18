#include "GpuTemporalTestSupport.h"

#include "Scene/Scene.h"

#include <algorithm>

//======================================================================================================================
TEST_CASE("occlusion graph roots every HZB mip and reads the immediately preceding pyramid",
          "[gpu][hzb][occlusion-graph]") {
    using namespace lmx::render;
    using namespace lmx::rhi;
    auto device = createDevice();
    REQUIRE(device);
    auto scene = lmx::scene::loadVisibilityLabScene(**device, 1024);
    REQUIRE(scene);
    auto renderer = Renderer::create(**device, 64, 64, true);
    REQUIRE(renderer);
    TransientPool pool(**device);
    const auto camera = lmx::scene::cameraFromScene((*scene)->initialCamera);
    for (uint32_t frame = 0; frame < 2; ++frame) {
        auto& commands = (*device)->beginFrame();
        REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
        std::vector<DrawItem> items;
        auto view = (*scene)->view(items, ShadowFilter::PCF, false);
        view.classifyMode = ClassifyMode::Gpu;
        view.submission = SubmissionMode::Indirect;
        view.occlusionEnabled = true;
        view.bloomEnabled = false;
        pool.beginFrame();
        RenderGraph graph(pool);
        graph.presentTexture((*renderer)->declarePasses(graph, commands, camera, view));
        const auto record = graph.compileFrame((*device)->frameNumber());
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
        const auto sourceName = frame == 0 ? "lmx.render.hzb1" : "lmx.render.hzb0";
        REQUIRE(std::ranges::any_of(classify->uses, [&](const auto& use) {
            return debug.resources[use.resource].name == sourceName &&
                   use.role == UseRole::ShaderRead;
        }));
        graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*scene)->commitFrame();
        (*device)->waitIdle();
        requireMatchesGolden(dumpCompiledFrame(*record), frame == 0 ? "frame-occlusion-first.txt"
                                                                    : "frame-occlusion-steady.txt");
    }
}
