//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementSampleTests.cpp
/// @brief Tests production declaration sampling before GPU classification retires.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Performance/MeasurementSample.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GraphTestSupport.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx;

//======================================================================================================================
TEST_CASE("Measurement samples real declarations with mixed and disabled populations",
          "[app][measurement][measurement-declaration]") {
    for (const auto classifier : {render::ClassifyMode::Cpu, render::ClassifyMode::Gpu}) {
        for (const auto submission :
             {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
            for (const uint32_t enabled : {2u, 0u}) {
                CAPTURE(classifier, submission, enabled);
                FakeDevice device;
                device.frame = 1;
                engine::Scene scene;
                scene.boundingSphere = {0, 0, 0, 2};
                const auto mesh = scene.addMesh(engine::makeCube(), "lmx.test.measurement.cube");
                const auto material = scene.addMaterial({});
                for (uint32_t i = 0; i < 3; ++i) {
                    scene.addObject({.mesh = mesh, .material = material});
                    scene.setObjectEnabled(i, i < enabled);
                }
                REQUIRE(scene.finalize(device));
                auto renderer = render::Renderer::create(device, 32, 32);
                REQUIRE(renderer);
                auto& commands = device.beginFrame();
                REQUIRE(scene.prepareFrame(device.frameNumber()));
                std::vector<engine::DrawItem> items;
                auto view = render::buildSceneView(scene, items, false);
                view.classifyMode = classifier;
                view.submission = submission;
                view.temporal.enabled = false;
                view.localLightMode = engine::LocalLightMode::Off;
                render::TransientPool pool(device);
                pool.beginFrame();
                render::RenderGraph graph(pool);
                graph.exportTexture(
                    (*renderer)->declarePasses(graph, commands, engine::Camera{}, view));
                const auto record = graph.compileFrame(device.frameNumber());
                REQUIRE(record);
                const auto& declaration = (*renderer)->visibilityStatus();
                REQUIRE_FALSE(declaration.isRetired);
                REQUIRE(declaration.scene.candidates.size() == 3);
                const auto sample = app::measurementCpuSample(
                    0, 0, 0, declaration, scene.tableStats(), *record, false,
                    (*renderer)->temporalStatus(), (*renderer)->lightingStatus());
                CHECK(sample.candidates == enabled);
                CHECK(sample.sceneCounters.disabled == 3 - enabled);
                CHECK(sample.shadowCounters.candidates == enabled);
                CHECK(sample.shadowCounters.disabled == 3 - enabled);
                app::MeasurementPlan plan;
                plan.warmupFrames = 0;
                plan.measuredFrames = 1;
                plan.unscored = true;
                plan.classify = classifier == render::ClassifyMode::Gpu ? "gpu" : "cpu";
                plan.submission =
                    submission == render::SubmissionMode::Batched ? "batched" : "indirect";
                plan.localLightMode = "off";
                plan.startingPopulation = app::measurementPopulation(scene);
                app::MeasurementRun run;
                REQUIRE(run.start(plan, {}));
                CHECK(run.recordCpu(sample));
                if (classifier == render::ClassifyMode::Gpu) {
                    CHECK(declaration.sceneCounters.visible == 0);
                    CHECK(declaration.sceneCounters.rejected == 0);
                    CHECK(declaration.sceneCounters.emittedRows == 0);
                    CHECK(declaration.sceneCounters.emittedCommands == 0);
                    CHECK(run.samples().size() == 1);
                    if (!run.samples().empty()) {
                        CHECK_FALSE(run.samples().front().visibility);
                        CHECK_FALSE(run.samples().front().retired);
                    }
                    CHECK(run.state() == app::MeasurementState::Draining);
                }
            }
        }
    }
}
