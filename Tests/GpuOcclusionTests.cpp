#include "Engine/Scene/Scene.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include <algorithm>
#include <functional>

using namespace lmx::render;
using namespace rojoRHI;
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
//======================================================================================================================
VisibilityStatus checkedFrame(Device& device, engine::Scene& scene, Renderer& renderer,
                              const engine::Camera& camera, float scale = 1, bool jitter = false,
                              std::function<void(SceneView&)> configure = {},
                              std::vector<uint32_t>* depthReadback = nullptr) {
    auto& commands = device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()));
    std::vector<engine::DrawItem> items;
    auto view = buildSceneView(scene, items, ShadowFilter::PCF, false);
    view.classifyMode = ClassifyMode::Gpu;
    view.classifyCheck = true;
    view.occlusionEnabled = true;
    view.occlusionCheck = true;
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = jitter;
    view.temporal.reconstruction = ReconstructionMode::Raw;
    view.temporal.renderScale = scale;
    view.temporal.sceneGeneration = 1;
    view.bloomEnabled = false;
    if (configure)
        configure(view);
    renderer.render(commands, camera, view, false);
    std::unique_ptr<ShaderLibrary> readLibrary;
    std::unique_ptr<ComputePipeline> readPipeline;
    std::unique_ptr<Buffer> depthBuffer;
    if (depthReadback) {
        auto library = device.loadShaderLibrary("Shaders/HzbReadbackFixture");
        REQUIRE(library);
        readLibrary = std::move(*library);
        auto pipeline = device.createComputePipeline({.library = readLibrary.get(),
                                                      .computeEntry = "computeMain",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.occlusion.depthReader"});
        REQUIRE(pipeline);
        readPipeline = std::move(*pipeline);
        auto& depth = renderer.depthTarget();
        depthReadback->resize(uint64_t{depth.width()} * depth.height());
        auto buffer = device.createBuffer({.size = depthReadback->size() * sizeof(float),
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.occlusion.depthReadback"},
                                          nullptr);
        REQUIRE(buffer);
        depthBuffer = std::move(*buffer);
        commands.beginComputePass("lmx.test.occlusion.depthReadback");
        commands.bindComputePipeline(*readPipeline);
        commands.bindTexture(0, depth);
        commands.bindStorageBuffer(1, *depthBuffer, StorageAccess::Write);
        commands.bindFrameData(0, std::array<uint32_t, 4>{depth.width(), depth.height(), 0, 0});
        commands.dispatch((depth.width() + 7) / 8, (depth.height() + 7) / 8, 1);
        commands.endComputePass();
    }
    device.endFrame(nullptr);
    scene.commitFrame();
    device.waitIdle();
    if (depthReadback)
        depthBuffer->readback(depthReadback->data(), depthReadback->size() * sizeof(float));
    renderer.drainVisibilityAfterIdle();
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
//======================================================================================================================
std::vector<uint8_t> targetBytes(Texture& texture, uint32_t bytesPerPixel) {
    std::vector<uint8_t> result(uint64_t{texture.width()} * texture.height() * bytesPerPixel);
    texture.readback(result.data(), result.size());
    return result;
}
} // namespace

//======================================================================================================================
TEST_CASE("production occlusion preserves exact static geometry attachments and independent IDs",
          "[gpu][occlusion-integration]") {
    auto device = createDevice();
    REQUIRE(device);
    auto scene = occludedScene();
    REQUIRE(scene.finalize(**device));
    auto renderer = Renderer::create(**device, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    std::vector<uint32_t> depth, occludedDepth;
    auto baseline = checkedFrame(
        **device, scene, **renderer, camera, 1, false,
        [](SceneView& view) {
            view.occlusionEnabled = false;
            view.occlusionCheck = false;
        },
        &depth);
    const auto hdr = targetBytes((*renderer)->hdrColorTarget(), 8);
    const auto motion = targetBytes(*(*renderer)->motionTarget(), 4);
    const auto reactive = targetBytes(*(*renderer)->reactiveTarget(), 1);
    auto first = checkedFrame(**device, scene, **renderer, camera);
    REQUIRE(first.sceneCounters.occluded == 0);
    auto second = checkedFrame(**device, scene, **renderer, camera, 1, false, {}, &occludedDepth);
    REQUIRE(second.sceneCounters.occluded == 1);
    REQUIRE(second.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE(second.occlusionSourceFrame + 1 == second.frameNumber);
    REQUIRE(second.occlusionCheck.strict);
    REQUIRE(second.occlusionCheck.falselyRejectedInstances == 0);
    REQUIRE(targetBytes((*renderer)->hdrColorTarget(), 8) == hdr);
    REQUIRE(occludedDepth == depth);
    REQUIRE(targetBytes(*(*renderer)->motionTarget(), 4) == motion);
    REQUIRE(targetBytes(*(*renderer)->reactiveTarget(), 1) == reactive);
    for (const float scale : {1.0f, 0.5f, 1.0f}) {
        for (uint32_t phase = 0; phase < 16; ++phase) {
            CAPTURE(scale, phase);
            const auto status = checkedFrame(**device, scene, **renderer, camera, scale, true);
            REQUIRE(status.occlusionSourceFrame + 1 == status.frameNumber);
            REQUIRE(status.occlusionInvalidReason == OcclusionInvalidReason::None);
            REQUIRE(status.sceneCounters.occluded == 1);
            REQUIRE(status.occlusionCheck.falselyRejectedInstances == 0);
        }
    }
}

//======================================================================================================================
TEST_CASE("newly visible geometry is recovered on its second visible frame",
          "[gpu][occlusion-integration][occlusion-recovery]") {
    auto device = createDevice();
    REQUIRE(device);
    auto scene = occludedScene(1.2f);
    REQUIRE(scene.finalize(**device));
    auto renderer = Renderer::create(**device, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    checkedFrame(**device, scene, **renderer, camera);
    auto hidden = checkedFrame(**device, scene, **renderer, camera);
    REQUIRE(hidden.sceneCounters.occluded == 1);
    camera.position.x = 0.9f;
    auto revealed = checkedFrame(**device, scene, **renderer, camera);
    REQUIRE(revealed.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE_FALSE(revealed.occlusionCheck.strict);
    REQUIRE(revealed.occlusionCheck.visibleInstances == 2);
    REQUIRE(revealed.occlusionCheck.falselyRejectedInstances == 1);
    REQUIRE(revealed.occlusionCheck.maximumMissingStreak == 1);
    auto recovered = checkedFrame(**device, scene, **renderer, camera);
    REQUIRE(recovered.occlusionCheck.visibleInstances == 2);
    REQUIRE(recovered.occlusionCheck.falselyRejectedInstances == 0);
    REQUIRE(recovered.sceneCounters.occluded == 0);
}

//======================================================================================================================
TEST_CASE("global occlusion invalidation retains all candidates before reusing depth evidence",
          "[gpu][occlusion-integration][occlusion-invalidation]") {
    auto device = createDevice();
    REQUIRE(device);
    auto scene = occludedScene();
    REQUIRE(scene.finalize(**device));
    auto renderer = Renderer::create(**device, 64, 64, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    checkedFrame(**device, scene, **renderer, camera);
    REQUIRE(checkedFrame(**device, scene, **renderer, camera).sceneCounters.occluded == 1);
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
        checkedFrame(**device, scene, **renderer, camera, 1, false, [](SceneView& view) {
            view.occlusionEnabled = false;
            view.occlusionCheck = false;
        });
        expected = OcclusionInvalidReason::PreviouslyDisabled;
    }
    SECTION("unrendered frame gap") {
        (*device)->beginFrame();
        (*device)->endFrame(nullptr);
        expected = OcclusionInvalidReason::SourceGap;
    }
    const auto status = checkedFrame(**device, scene, **renderer, camera, 1, false, configure);
    REQUIRE(status.occlusionInvalidReason == expected);
    REQUIRE(status.sceneCounters.occluded == 0);
    REQUIRE(status.occlusionCheck.falselyRejectedInstances == 0);
    const auto following =
        checkedFrame(**device, scene, **renderer, camera, 1, false, [configure](SceneView& view) {
            if (configure)
                configure(view);
            view.temporal.cameraCut = false;
        });
    REQUIRE(following.occlusionInvalidReason == OcclusionInvalidReason::None);
    REQUIRE(following.occlusionSourceFrame + 1 == following.frameNumber);
    REQUIRE(following.occlusionCheck.falselyRejectedInstances == 0);
}

//======================================================================================================================
TEST_CASE("occlusion joins overlapping retired frames across slot reuse in both submission modes",
          "[gpu][occlusion-integration][occlusion-overlap]") {
    auto device = createDevice();
    REQUIRE(device);
    auto scene = occludedScene();
    REQUIRE(scene.finalize(**device));
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    for (const auto mode : {SubmissionMode::Indirect, SubmissionMode::Batched}) {
        auto renderer = Renderer::create(**device, 64, 64, true);
        REQUIRE(renderer);
        std::vector<VisibilityStatus> statuses;
        const uint64_t firstFrame = (*device)->frameNumber() + 1;
        for (uint32_t frame = 0; frame < 8; ++frame) {
            auto& commands = (*device)->beginFrame();
            REQUIRE(scene.prepareFrame((*device)->frameNumber()));
            std::vector<engine::DrawItem> items;
            auto view = buildSceneView(scene, items, ShadowFilter::PCF, false);
            view.classifyMode = ClassifyMode::Gpu;
            view.submission = mode;
            view.classifyCheck = true;
            view.occlusionEnabled = true;
            view.occlusionCheck = true;
            view.bloomEnabled = false;
            view.temporal.enabled = true;
            view.temporal.jitterEnabled = true;
            view.temporal.reconstruction = ReconstructionMode::Raw;
            view.temporal.sceneGeneration = 9;
            view.temporal.renderScale = frame % 2 == 0 ? 1.0f : 0.5f;
            (*renderer)->render(commands, camera, view, false);
            (*device)->endFrame(nullptr);
            scene.commitFrame();
            auto retired = (*renderer)->takeRetiredVisibility();
            for (auto& status : retired)
                statuses.push_back(std::move(status));
        }
        (*device)->waitIdle();
        (*renderer)->drainVisibilityAfterIdle();
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
