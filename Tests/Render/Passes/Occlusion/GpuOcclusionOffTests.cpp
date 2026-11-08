//----------------------------------------------------------------------------------------------------------------------
/// @file GpuOcclusionOffTests.cpp
/// @brief Checks production occlusion with temporal reconstruction off, at full resolution.
//----------------------------------------------------------------------------------------------------------------------
// These are the temporal-off counterparts of GpuOcclusionTemporalTests: the same scenes, the same
// production classifier and independent reference, with no jitter, motion, reactive mask or scaled
// render extent. They prove the occlusion join alone and claim nothing about temporal history.
#include "Engine/Scene/Scene.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include <algorithm>
#include <cstring>
#include <functional>

using namespace lmx::render;
namespace engine = lmx::engine;

namespace {
//======================================================================================================================
engine::MeshData occlusionQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}
//======================================================================================================================
engine::Scene occludedScene(float targetX = 0) {
    engine::Scene result;
    result.name = "lmx.test.occlusion.scene";
    result.boundingSphere = {0, 0, -3, 4};
    const auto mesh = result.addMesh(occlusionQuad(), "lmx.test.occlusion.quad");
    const auto material =
        result.addMaterial({.albedo = {0, 0, 0, 1}, .emissive = {0.25f, 0.1f, 0.05f}});
    result.addObject(
        {.position = {0, 0, -2}, .scale = {0.8f, 0.8f, 0.8f}, .mesh = mesh, .material = material});
    result.addObject({.position = {targetX, 0, -4},
                      .scale = {0.1f, 0.1f, 0.1f},
                      .mesh = mesh,
                      .material = material});
    return result;
}

// The frame's colour, copied on its own command list after its passes: a readback in a frame of
// its own would leave a gap in the occlusion source clock that the next frame would see.
struct FrameTargets {
    std::vector<uint8_t> hdr;
};

//======================================================================================================================
std::vector<uint8_t> resolvedBytes(lmx::engine::GpuContext& gpu, const ReadbackTicket& ticket) {
    std::vector<uint8_t> bytes(ticket.region.size);
    const auto resolved = resolve(gpu, ticket, bytes);
    INFO(errorOf(resolved));
    REQUIRE(resolved);
    return bytes;
}

//======================================================================================================================
VisibilityStatus checkedFrame(lmx::engine::GpuContext& gpu, engine::Scene& scene,
                              Renderer& renderer, const engine::Camera& camera,
                              std::function<void(SceneView&)> configure = {},
                              std::vector<uint32_t>* depthReadback = nullptr,
                              FrameTargets* targets = nullptr) {
    auto gpuFrame1 = gpu.beginFrame();
    auto& commands = gpuFrame1.commands;
    REQUIRE(scene.prepareFrame(gpu.frameNumber()));
    std::vector<engine::DrawItem> items;
    auto view = buildSceneView(scene, items, false);
    view.classifyMode = ClassifyMode::Gpu;
    view.classifyCheck = true;
    view.occlusionEnabled = true;
    view.occlusionCheck = true;
    view.temporal.enabled = false;
    view.temporal.sceneGeneration = 1;
    view.bloomEnabled = false;
    if (configure)
        configure(view);
    const auto rendered = renderer.render(commands, camera, view);
    if (!rendered)
        gpu.submitAndEndFrame(gpuFrame1);
    INFO(errorOf(rendered));
    REQUIRE(rendered);
    ReadbackTicket depthTicket, hdrTicket;
    auto& states = renderer.textureStates();
    if (depthReadback)
        depthTicket = recordReadback(gpu, commands, renderer.depthTexture(), states);
    if (targets) {
        // With temporal off the display transform samples the scene colour directly.
        requireRecordedUse(states, renderer.hdrColorTexture(), TextureUse::ShaderRead);
        hdrTicket = recordReadback(gpu, commands, renderer.hdrColorTexture(), states);
    }
    gpu.submitAndEndFrame(gpuFrame1);
    scene.commitFrame();
    gpu.waitIdle();
    if (depthReadback) {
        const auto& depth = renderer.depthTexture();
        depthReadback->resize(uint64_t{depth.desc().extent.width} * depth.desc().extent.height);
        const auto resolved = resolve(gpu, depthTicket, resolveBytes(std::span(*depthReadback)));
        INFO(errorOf(resolved));
        REQUIRE(resolved);
    }
    if (targets)
        targets->hdr = resolvedBytes(gpu, hdrTicket);
    REQUIRE(renderer.drainVisibilityAfterIdle());
    auto retired = renderer.takeRetiredVisibility();
    REQUIRE(retired.size() == 1);
    auto status = std::move(retired.front());
    REQUIRE(status.isRetired);
    CAPTURE(status.frameNumber, status.stateMismatches, status.rowMismatches,
            status.argumentMismatches, status.counterMismatches,
            status.occlusionCheck.falselyRejectedInstances,
            status.occlusionCheck.maximumMissingStreak);
    REQUIRE(status.checkPassed());
    if (status.occlusionCheckEnabled) {
        REQUIRE(status.occlusionCheck.enabled);
        REQUIRE(status.occlusionCheck.passed());
    }
    REQUIRE(status.shadowCounters.occluded == 0);
    return status;
}
} // namespace

//======================================================================================================================
TEST_CASE("temporal-off occlusion preserves static color depth and independent IDs",
          "[gpu][occlusion-integration][temporal-off]") {
    auto gpu = createTestGpu();
    auto scene = occludedScene();
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = Renderer::create(*gpu, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    std::vector<uint32_t> depth, occludedDepth;
    FrameTargets baselineTargets, occludedTargets;
    const auto baseline = checkedFrame(
        *gpu, scene, **renderer, camera,
        [](SceneView& view) {
            view.occlusionEnabled = false;
            view.occlusionCheck = false;
        },
        &depth, &baselineTargets);
    REQUIRE(baseline.sceneCounters.occluded == 0);
    REQUIRE(baseline.occlusionInvalidReason == OcclusionInvalidReason::Disabled);
    auto first = checkedFrame(*gpu, scene, **renderer, camera);
    REQUIRE(first.sceneCounters.occluded == 0);
    auto second =
        checkedFrame(*gpu, scene, **renderer, camera, {}, &occludedDepth, &occludedTargets);
    REQUIRE(second.sceneCounters.occluded == 1);
    REQUIRE(second.scene.candidates[1].reason == VisibilityReason::Occluded);
    REQUIRE(second.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE(second.occlusionSourceFrame + 1 == second.frameNumber);
    REQUIRE(second.occlusionCheck.strict);
    REQUIRE(second.occlusionCheck.falselyRejectedInstances == 0);
    REQUIRE(occludedTargets.hdr == baselineTargets.hdr);
    REQUIRE(occludedDepth == depth);
    for (uint32_t frame = 0; frame < 4; ++frame) {
        CAPTURE(frame);
        const auto status = checkedFrame(*gpu, scene, **renderer, camera);
        REQUIRE(status.occlusionSourceFrame + 1 == status.frameNumber);
        REQUIRE(status.occlusionInvalidReason == OcclusionInvalidReason::None);
        REQUIRE(status.sceneCounters.occluded == 1);
        REQUIRE(status.occlusionCheck.strict);
        REQUIRE(status.occlusionCheck.falselyRejectedInstances == 0);
    }
}

//======================================================================================================================
TEST_CASE("temporal-off occlusion recovers newly visible geometry on its second visible frame",
          "[gpu][occlusion-integration][occlusion-recovery][temporal-off]") {
    auto gpu = createTestGpu();
    auto scene = occludedScene(1.2f);
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = Renderer::create(*gpu, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    checkedFrame(*gpu, scene, **renderer, camera);
    auto hidden = checkedFrame(*gpu, scene, **renderer, camera);
    REQUIRE(hidden.sceneCounters.occluded == 1);
    camera.position.x = 0.9f;
    auto revealed = checkedFrame(*gpu, scene, **renderer, camera);
    REQUIRE(revealed.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE_FALSE(revealed.occlusionCheck.strict);
    REQUIRE(revealed.occlusionCheck.visibleInstances == 2);
    REQUIRE(revealed.occlusionCheck.falselyRejectedInstances == 1);
    REQUIRE(revealed.occlusionCheck.maximumMissingStreak == 1);
    auto recovered = checkedFrame(*gpu, scene, **renderer, camera);
    REQUIRE(recovered.occlusionCheck.visibleInstances == 2);
    REQUIRE(recovered.occlusionCheck.falselyRejectedInstances == 0);
    REQUIRE(recovered.sceneCounters.occluded == 0);
}

//======================================================================================================================
TEST_CASE("temporal-off occlusion invalidates every changed coverage source",
          "[gpu][occlusion-integration][occlusion-invalidation][temporal-off]") {
    auto gpu = createTestGpu();
    auto scene = occludedScene();
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = Renderer::create(*gpu, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    checkedFrame(*gpu, scene, **renderer, camera);
    REQUIRE(checkedFrame(*gpu, scene, **renderer, camera).sceneCounters.occluded == 1);
    OcclusionInvalidReason expected = OcclusionInvalidReason::CoverageChanged;
    std::function<void(SceneView&)> configure;
    SECTION("move occluder") {
        scene.objects[0].position.x += 0.05f;
    }
    SECTION("move hidden instance") {
        scene.objects[1].position.x += 0.05f;
    }
    SECTION("reshape") {
        scene.objects[0].scale.x *= 0.9f;
    }
    SECTION("switch occluder to masked") {
        scene.material(scene.objects[0].material).alphaMode = engine::AlphaMode::Mask;
    }
    SECTION("remove") {
        scene.removeObject(scene.objects[0].id);
    }
    SECTION("add") {
        scene.addObject({.position = {10, 0, -4},
                         .mesh = scene.objects[1].mesh,
                         .material = scene.objects[1].material});
    }
    SECTION("scene identity") {
        configure = [](SceneView& view) { view.temporal.sceneGeneration = 2; };
        expected = OcclusionInvalidReason::SceneChanged;
    }
    SECTION("explicit cut") {
        configure = [](SceneView& view) { view.temporal.cameraCut = true; };
        expected = OcclusionInvalidReason::CameraCut;
    }
    SECTION("translation") {
        camera.position.x = 1.01f;
        expected = OcclusionInvalidReason::CameraTranslation;
    }
    SECTION("rotation") {
        camera.yaw = glm::radians(10.1f);
        expected = OcclusionInvalidReason::CameraRotation;
    }
    SECTION("output resize") {
        REQUIRE((*renderer)->resize(80, 64));
        expected = OcclusionInvalidReason::OutputExtentChanged;
    }
    SECTION("toggle off then on") {
        checkedFrame(*gpu, scene, **renderer, camera, [](SceneView& view) {
            view.occlusionEnabled = false;
            view.occlusionCheck = false;
        });
        expected = OcclusionInvalidReason::PreviouslyDisabled;
    }
    SECTION("unrendered frame gap") {
        auto gpuFrame2 = gpu->beginFrame();
        gpu->submitAndEndFrame(gpuFrame2);
        expected = OcclusionInvalidReason::SourceGap;
    }
    const auto status = checkedFrame(*gpu, scene, **renderer, camera, configure);
    REQUIRE(status.occlusionInvalidReason == expected);
    REQUIRE(status.sceneCounters.occluded == 0);
    REQUIRE(status.occlusionCheck.falselyRejectedInstances == 0);
    const auto following =
        checkedFrame(*gpu, scene, **renderer, camera, [configure](SceneView& view) {
            if (configure)
                configure(view);
            view.temporal.cameraCut = false;
        });
    REQUIRE(following.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE(following.occlusionSourceFrame + 1 == following.frameNumber);
    REQUIRE(following.occlusionCheck.falselyRejectedInstances == 0);
}

//======================================================================================================================
TEST_CASE("temporal-off occlusion joins overlapping frames through paced slot reuse",
          "[gpu][occlusion-integration][occlusion-overlap][temporal-off]") {
    auto gpu = createTestGpu();
    auto scene = occludedScene();
    REQUIRE(scene.finalize(gpu->device()));
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        auto renderer = Renderer::create(*gpu, 64, 64, true);
        REQUIRE(renderer);
        std::vector<VisibilityStatus> statuses;
        const uint64_t firstFrame = gpu->frameNumber() + 1;
        for (uint32_t frame = 0; frame < 8; ++frame) {
            auto gpuFrame3 = gpu->beginFrame();
            auto& commands = gpuFrame3.commands;
            REQUIRE(scene.prepareFrame(gpu->frameNumber()));
            std::vector<engine::DrawItem> items;
            auto view = buildSceneView(scene, items, false);
            view.classifyMode = ClassifyMode::Gpu;
            view.submission = mode;
            view.classifyCheck = true;
            view.occlusionEnabled = true;
            view.occlusionCheck = true;
            view.bloomEnabled = false;
            view.temporal.enabled = false;
            view.temporal.sceneGeneration = 9;
            const auto rendered = (*renderer)->render(commands, camera, view);
            gpu->submitAndEndFrame(gpuFrame3);
            INFO(errorOf(rendered));
            REQUIRE(rendered);
            scene.commitFrame();
            auto retired = (*renderer)->takeRetiredVisibility();
            for (auto& status : retired)
                statuses.push_back(std::move(status));
        }
        gpu->waitIdle();
        REQUIRE((*renderer)->drainVisibilityAfterIdle());
        auto retired = (*renderer)->takeRetiredVisibility();
        for (auto& status : retired)
            statuses.push_back(std::move(status));
        REQUIRE(statuses.size() == 8);
        for (size_t index = 0; index < statuses.size(); ++index) {
            CAPTURE(index, static_cast<int>(mode));
            const auto& status = statuses[index];
            REQUIRE(status.frameNumber == firstFrame + index);
            REQUIRE(status.sceneGeneration == 9);
            REQUIRE(status.checkPassed());
            REQUIRE(status.occlusionCheck.enabled);
            REQUIRE(status.occlusionCheck.frameNumber == status.frameNumber);
            REQUIRE(status.occlusionCheck.passed());
            REQUIRE(status.occlusionCheck.falselyRejectedInstances == 0);
            REQUIRE(status.shadowCounters.occluded == 0);
            REQUIRE(status.shadowCounters.emittedRows == 2);
            if (index > 0) {
                REQUIRE(status.occlusionInvalidReason == OcclusionInvalidReason::None);
                REQUIRE(status.occlusionSourceFrame + 1 == status.frameNumber);
                REQUIRE(status.sceneCounters.occluded == 1);
                REQUIRE(status.sceneCounters.emittedRows == 1);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE(
    "temporal-off enabled toggles invalidate rejecting occlusion history",
    "[gpu][occlusion-integration][disabled-instance][occlusion-enabled-toggle][temporal-off]") {
    auto gpu = createTestGpu();
    auto scene = occludedScene();
    REQUIRE(scene.finalize(gpu->device()));
    const auto foregroundId = scene.objects[0].id;
    const auto targetId = scene.objects[1].id;
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    for (auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        CAPTURE(static_cast<int>(mode));
        auto renderer = Renderer::create(*gpu, 64, 64, true);
        REQUIRE(renderer);
        const auto configure = [mode](SceneView& view) { view.submission = mode; };
        checkedFrame(*gpu, scene, **renderer, camera, configure);
        const auto hidden = checkedFrame(*gpu, scene, **renderer, camera, configure);
        REQUIRE(hidden.sceneCounters.occluded == 1);
        REQUIRE(hidden.scene.candidates[1].reason == VisibilityReason::Occluded);
        REQUIRE(hidden.occlusionCheck.visibleInstances == 1);
        const auto coverageBefore = scene.coverageEpoch();
        scene.setObjectEnabled(0, false);
        REQUIRE(scene.coverageEpoch() > coverageBefore);
        const auto revealed = checkedFrame(*gpu, scene, **renderer, camera, configure);
        REQUIRE(revealed.occlusionInvalidReason == OcclusionInvalidReason::CoverageChanged);
        REQUIRE(revealed.sceneCounters.occluded == 0);
        REQUIRE(revealed.sceneCounters.candidates == 1);
        REQUIRE(revealed.sceneCounters.disabled == 1);
        REQUIRE(revealed.sceneCounters.historyInvalid == 1);
        REQUIRE(revealed.sceneCounters.rejected == 0);
        REQUIRE(revealed.shadowCounters.emittedRows == 1);
        REQUIRE(revealed.scene.candidates.size() == 2);
        REQUIRE(revealed.scene.candidates[0].reason == VisibilityReason::AuthoredOff);
        REQUIRE(revealed.scene.candidates[1].state == VisibilityState::Visible);
        REQUIRE(revealed.occlusionCheck.falselyRejectedInstances == 0);
        REQUIRE(revealed.occlusionCheck.visibleInstances == 1);
        const auto off = checkedFrame(*gpu, scene, **renderer, camera, configure);
        REQUIRE(off.occlusionInvalidReason == OcclusionInvalidReason::None);
        REQUIRE(off.sceneCounters.occluded == 0);
        REQUIRE(off.sceneCounters.disabled == 1);
        scene.setObjectEnabled(0, true);
        const auto restored = checkedFrame(*gpu, scene, **renderer, camera, configure);
        REQUIRE(restored.occlusionInvalidReason == OcclusionInvalidReason::CoverageChanged);
        REQUIRE(restored.sceneCounters.disabled == 0);
        REQUIRE(restored.sceneCounters.occluded == 0);
        REQUIRE(restored.sceneCounters.historyInvalid == 2);
        const auto hiddenAgain = checkedFrame(*gpu, scene, **renderer, camera, configure);
        REQUIRE(hiddenAgain.occlusionInvalidReason == OcclusionInvalidReason::None);
        REQUIRE(hiddenAgain.sceneCounters.occluded == 1);
        REQUIRE(hiddenAgain.scene.candidates[1].reason == VisibilityReason::Occluded);
        REQUIRE(hiddenAgain.occlusionCheck.falselyRejectedInstances == 0);
        REQUIRE(scene.objects.size() == 2);
        REQUIRE(scene.objects[0].id == foregroundId);
        REQUIRE(scene.objects[1].id == targetId);
    }
}
