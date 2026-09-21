#include "GpuTestSupport.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "SceneTableTestSupport.h"
#include <array>
#include <glm/gtc/matrix_transform.hpp>

using namespace lmx::render;
using namespace rojoRHI;
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureSceneView;

namespace {
//======================================================================================================================
lmx::engine::MeshData referenceQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}
//======================================================================================================================
VisibilityStatus referenceFrame(Device& device, TransientPool& pool, OcclusionReference& reference,
                                const FixtureSceneView& fixture, uint32_t width = 64,
                                uint32_t height = 64) {
    auto& commands = device.beginFrame();
    auto view = fixture.prepare(device);
    pool.beginFrame();
    RenderGraph graph(pool);
    lmx::engine::Camera camera;
    camera.position = {0, 0, 4};
    camera.fovY = glm::half_pi<float>();
    const auto result = reference.declare(
        graph, commands, view,
        camera.projectionMatrix(static_cast<float>(width) / height) * camera.viewMatrix(), width,
        height, true);
    INFO(errorOf(result));
    REQUIRE(result);
    graph.execute(commands, device.frameNumber());
    VisibilityStatus status;
    status.frameNumber = device.frameNumber();
    status.sceneGeneration = view.temporal.sceneGeneration;
    status.isRetired = true;
    for (const auto& item : view.items)
        status.scene.candidates.push_back({.instanceRow = item.instanceRow,
                                           .state = VisibilityState::Rejected,
                                           .reason = VisibilityReason::Occluded});
    device.endFrame(nullptr);
    return status;
}
} // namespace

//======================================================================================================================
TEST_CASE("independent occlusion reference draws rejected candidates and shares masked coverage",
          "[gpu][occlusion-reference]") {
    auto device = createDevice();
    REQUIRE(device);
    auto reference = OcclusionReference::create(**device);
    INFO(errorOf(reference));
    REQUIRE(reference);
    TransientPool pool(**device);
    auto mesh = lmx::test::fixtureMesh(**device, referenceQuad(), "lmx.test.reference.quad");
    REQUIRE(mesh);
    std::array<FixtureDrawItem, 2> items;
    for (auto& item : items)
        item.mesh = &*mesh;
    items[0].model = glm::translate(glm::mat4(1), glm::vec3(0, 0, 1));
    items[0].material.alphaMode = lmx::engine::AlphaMode::Mask;
    items[0].material.albedo.a = 0;
    FixtureSceneView fixture;
    fixture.items = items;
    auto status = referenceFrame(**device, pool, **reference, fixture);
    (*device)->waitIdle();
    (*reference)->retireThrough(status.frameNumber);
    auto check = (*reference)->check(status);
    REQUIRE(check);
    REQUIRE(check->visibleInstances == 1);
    REQUIRE(check->falselyRejectedInstances == 1);
    REQUIRE(check->missing[0].instanceRow == status.scene.candidates[1].instanceRow);
    REQUIRE(check->falselyRejectedPixels == 256);
    REQUIRE(check->invalidReferencePixels == 0);
    REQUIRE_FALSE(check->passed());
    REQUIRE_FALSE((*reference)->check(status));
    items[0].material.albedo.a = 1;
    status = referenceFrame(**device, pool, **reference, fixture);
    (*device)->waitIdle();
    (*reference)->retireThrough(status.frameNumber);
    check = (*reference)->check(status);
    REQUIRE(check);
    REQUIRE(check->visibleInstances == 1);
    REQUIRE(check->missing[0].instanceRow == status.scene.candidates[0].instanceRow);
    REQUIRE(check->falselyRejectedPixels > 256);
}

//======================================================================================================================
TEST_CASE("reference byte IDs and transient readback survive three paced slot recycles",
          "[gpu][occlusion-reference]") {
    auto device = createDevice();
    REQUIRE(device);
    auto reference = OcclusionReference::create(**device);
    REQUIRE(reference);
    TransientPool pool(**device);
    auto mesh = lmx::test::fixtureMesh(**device, referenceQuad(), "lmx.test.reference.ids");
    REQUIRE(mesh);
    std::vector<FixtureDrawItem> items(258);
    for (auto& item : items) {
        item.mesh = &*mesh;
        item.model = glm::translate(glm::mat4(1), glm::vec3(100, 0, 0));
    }
    items.back().model = glm::mat4(1);
    FixtureSceneView fixture;
    fixture.items = items;
    std::vector<VisibilityStatus> statuses;
    for (uint32_t frame = 0; frame < 8; ++frame)
        statuses.push_back(
            referenceFrame(**device, pool, **reference, fixture, frame % 2 == 0 ? 64 : 80, 64));
    (*device)->waitIdle();
    (*reference)->retireThrough(statuses.back().frameNumber);
    for (const auto& status : statuses) {
        const auto check = (*reference)->check(status);
        REQUIRE(check);
        REQUIRE(check->visibleInstances == 1);
        REQUIRE(check->falselyRejectedPixels == 256);
        REQUIRE(check->invalidReferencePixels == 0);
        REQUIRE(check->unmatchedCandidates == 0);
        REQUIRE(check->missing[0].instanceRow == 257);
        REQUIRE(check->maximumMissingStreak == status.frameNumber);
    }
}

//======================================================================================================================
TEST_CASE("independent ID reference preserves wireframe coverage", "[gpu][occlusion-reference]") {
    auto device = createDevice();
    REQUIRE(device);
    auto reference = OcclusionReference::create(**device);
    REQUIRE(reference);
    TransientPool pool(**device);
    auto mesh = lmx::test::fixtureMesh(**device, referenceQuad(), "lmx.test.reference.wire");
    REQUIRE(mesh);
    FixtureDrawItem item;
    item.mesh = &*mesh;
    FixtureSceneView fixture;
    fixture.items = std::span{&item, 1};
    fixture.wireframe = true;
    const auto status = referenceFrame(**device, pool, **reference, fixture);
    (*device)->waitIdle();
    (*reference)->retireThrough(status.frameNumber);
    const auto check = (*reference)->check(status);
    REQUIRE(check);
    REQUIRE(check->visibleInstances == 1);
    REQUIRE(check->falselyRejectedPixels > 0);
    REQUIRE(check->falselyRejectedPixels < 256);
    REQUIRE(check->invalidReferencePixels == 0);
}
