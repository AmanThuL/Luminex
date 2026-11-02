//----------------------------------------------------------------------------------------------------------------------
/// @file RenderZeroLightInvariantTests.cpp
/// @brief Replays zero-live requested light modes against the Off frame on a configured device.
//----------------------------------------------------------------------------------------------------------------------
#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Render/Graph/GraphDump.h"
#include "Render/Passes/Scene/SceneFrameData.h"
#include "Render/Renderer/Renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace lmx;

// The bytes and descriptor of one scene-owned buffer the frame's tables named. Addresses differ
// between the two replays' scenes, so the comparison is of contents and shape, never identity.
struct OwnedBytes {
    uint64_t size = 0;
    rojoRHI::Memory memory = rojoRHI::Memory::Gpu;
    std::vector<std::byte> bytes; ///< Defined prefix; empty when the owner is not CPU-visible.

    //==================================================================================================================
    bool operator==(const OwnedBytes&) const = default;
};

struct DeclaredSnapshot {
    std::string graph;
    render::TemporalStatus temporal;
    std::vector<std::byte> localParams; ///< The 136 bytes the scene pass uploads for this frame.
    std::array<OwnedBytes, 5> tables;   ///< Vertices, indices, meshes, instances, materials.
};

//======================================================================================================================
OwnedBytes ownedBytes(const engine::GpuBuffer* buffer, uint64_t definedBytes) {
    REQUIRE(buffer != nullptr);
    OwnedBytes result{.size = buffer->desc().size, .memory = buffer->desc().memory};
    if (buffer->desc().memory == rojoRHI::Memory::Upload) {
        REQUIRE(definedBytes <= buffer->desc().size);
        const auto* data = reinterpret_cast<const std::byte*>(buffer->cpuPointer());
        result.bytes.assign(data, data + definedBytes);
    }
    return result;
}

//======================================================================================================================
// One replay on its own device, so frame numbers and graph identities restart identically. The
// opaque cube, the masked double-sided cube and the sky cover every scene pipeline family; the
// high-water replay authors four disabled lights, so rows exist while none is live.
std::vector<DeclaredSnapshot> zeroLightReplay(bool switches, bool highWater,
                                              render::ReconstructionMode reconstruction,
                                              bool temporalEnabled, float scale) {
    auto gpu = createTestGpu();
    auto renderer = render::Renderer::create(*gpu, 80, 48, false);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto cube = test::fixtureMesh(gpu->device(), engine::makeCube(), "lmx.test.zeroLight.cube");
    REQUIRE(cube);
    auto sphere = test::fixtureMesh(gpu->device(), engine::fromGeo(asset::makeSphere(0.5f, 8, 8)),
                                    "lmx.test.zeroLight.sky");
    REQUIRE(sphere);
    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const rojoRHI::TextureData skyMip{.bytes = std::as_bytes(std::span(kSkyTexel)),
                                      .bytesPerRow = 4};
    const std::array<rojoRHI::TextureData, 6> skyFaces{skyMip, skyMip, skyMip,
                                                       skyMip, skyMip, skyMip};
    auto sky = engine::GpuTexture::create(gpu->device(),
                                          {.type = rojoRHI::TextureType::Cube,
                                           .format = rojoRHI::Format::RGBA8Unorm,
                                           .extent = {1, 1, 1},
                                           .layers = 6,
                                           .usage = rojoRHI::TextureUsage::Sampled,
                                           .label = "lmx.test.zeroLight.skyCubemap"},
                                          skyFaces);
    REQUIRE(sky);
    const std::array<test::FixtureDrawItem, 2> items{{
        {.mesh = &*cube, .model = glm::translate(glm::mat4{1.0f}, glm::vec3{-1.2f, 0.0f, 0.0f})},
        {.mesh = &*cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{1.2f, 0.0f, 0.0f}),
         .material = {.alphaMode = engine::AlphaMode::Mask, .doubleSided = true}},
    }};
    std::array<engine::LocalLight, 4> disabled{};
    for (auto& light : disabled) {
        light.range = 2.0f;
        light.enabled = false;
    }
    test::FixtureSceneView view = litSceneView(items);
    view.visibilityEnabled = false;
    view.skySphere = &*sphere;
    view.skyCubemap = &*sky;
    if (highWater)
        view.localLights = disabled;
    view.temporal.enabled = temporalEnabled;
    view.temporal.jitterEnabled = temporalEnabled;
    view.temporal.reconstruction = reconstruction;
    view.temporal.renderScale = scale;
    constexpr std::array modes{engine::LocalLightMode::Off,       engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Off,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Off};
    const engine::Camera camera = sceneCamera();
    std::vector<DeclaredSnapshot> result;
    for (const auto requested : modes) {
        view.localLightMode = switches ? requested : engine::LocalLightMode::Off;
        auto frame = gpu->beginFrame();
        const render::SceneView prepared = test::prepareSceneView(view, *gpu);
        REQUIRE(prepared.tables.liveLightCount == 0);
        REQUIRE(prepared.tables.lightRowCount == (highWater ? disabled.size() : 0));
        render::TransientPool pool(*gpu);
        pool.beginFrame();
        render::RenderGraph graph(pool);
        const auto display = (*renderer)->declarePasses(graph, frame.commands, camera, prepared);
        if (!display)
            gpu->submitAndEndFrame(frame);
        INFO(errorOf(display));
        REQUIRE(display);
        graph.exportTexture(*display);
        const auto record = graph.compileFrame(frame.number);
        if (!record)
            gpu->submitAndEndFrame(frame);
        REQUIRE(record);
        graph.execute(frame.commands, frame.number);
        gpu->submitAndEndFrame(frame);
        // Contents are read only after the frame that consumed them completed.
        gpu->waitIdle();
        const auto dump = render::dumpCompiledFrame(*record);
        CHECK(dump.find("lmx.scene.lights") == std::string::npos);
        CHECK(dump.find("lmx.pass.light.") == std::string::npos);
        CHECK(dump.find("lmx.light.") == std::string::npos);
        const auto& lighting = (*renderer)->lightingStatus();
        CHECK(lighting.frameNumber == frame.number);
        CHECK(lighting.requested == view.localLightMode);
        CHECK(lighting.effective == engine::LocalLightMode::Off);
        CHECK(lighting.listBytes == 0);
        CHECK(lighting.allocatedListBytes == 0);
        // The parameters the scene pass builds from this frame's inputs: Off, zero rows, and the
        // fixed grid over the frame's own render extent.
        const auto& temporal = (*renderer)->temporalStatus();
        const auto params =
            render::buildLocalLightParams({.requestedMode = view.localLightMode,
                                           .liveLightCount = prepared.tables.liveLightCount,
                                           .lightRowCount = prepared.tables.lightRowCount,
                                           .hasClusters = false,
                                           .extents = temporal.extents,
                                           .nearZ = camera.nearZ});
        CHECK(params.mode == 0);
        CHECK(params.rowCount == 0);
        const auto* paramBytes = reinterpret_cast<const std::byte*>(&params);
        const auto& tables = prepared.tables;
        result.push_back(
            {.graph = dump,
             .temporal = temporal,
             .localParams = {paramBytes, paramBytes + sizeof(params)},
             .tables = {
                 ownedBytes(tables.vertices, tables.vertices->desc().size),
                 ownedBytes(tables.indices, tables.indices->desc().size),
                 ownedBytes(tables.meshes, uint64_t{tables.meshCount} * sizeof(engine::MeshRow)),
                 ownedBytes(tables.instances,
                            uint64_t{tables.instanceCount} * sizeof(engine::InstanceRow)),
                 ownedBytes(tables.materials,
                            uint64_t{tables.materialCount} * sizeof(engine::MaterialRow))}});
    }
    // Every declared frame retires as Off with no list work, in frame order.
    const auto drained = (*renderer)->drainLightingAfterIdle();
    INFO(errorOf(drained));
    REQUIRE(drained);
    const auto retired = (*renderer)->takeRetiredLighting();
    REQUIRE(retired.size() == modes.size());
    for (size_t index = 0; index < retired.size(); ++index) {
        CAPTURE(index);
        CHECK(retired[index].frameNumber == uint64_t{index} + 1);
        CHECK(retired[index].isRetired);
        CHECK(retired[index].effective == engine::LocalLightMode::Off);
        CHECK(retired[index].counters.assigned == 0);
        CHECK(retired[index].listBytes == 0);
    }
    const auto visibility = (*renderer)->drainVisibilityAfterIdle();
    INFO(errorOf(visibility));
    REQUIRE(visibility);
    return result;
}
} // namespace

//======================================================================================================================
// Replaces v1's fake-command-list comparison of every bound input. The device replay compares
// what a configured device can observe -- the compiled graph, the absence of every light import
// and pass, the scene pass's local-light parameters, the scene-owned table bytes and descriptors,
// and the temporal status -- between a replay switching requested modes and an all-Off replay,
// across the five reconstruction cells and both high-water states. Which pointed bytes, roots and
// pipelines each encoded command selected is not observable here.
TEST_CASE("zero-live requested modes preserve the Off frame on a configured device",
          "[render][device][zero-light-invariants]") {
    using namespace render;
    for (const bool highWater : {false, true}) {
        for (const auto cell : {0, 1, 2, 3, 4}) {
            CAPTURE(highWater, cell);
            const auto reconstruction =
                cell >= 3 ? ReconstructionMode::VendorTemporal : ReconstructionMode::NativeTaa;
            const float scale = cell == 2 || cell == 4 ? 0.5f : 1.0f;
            const auto baseline =
                zeroLightReplay(false, highWater, reconstruction, cell != 0, scale);
            const auto switched =
                zeroLightReplay(true, highWater, reconstruction, cell != 0, scale);
            REQUIRE(baseline.size() == switched.size());
            for (size_t frame = 0; frame < baseline.size(); ++frame) {
                CAPTURE(frame);
                CHECK(switched[frame].graph == baseline[frame].graph);
                CHECK(switched[frame].localParams == baseline[frame].localParams);
                for (size_t table = 0; table < baseline[frame].tables.size(); ++table) {
                    CAPTURE(table);
                    CHECK(switched[frame].tables[table] == baseline[frame].tables[table]);
                }
                const auto& actual = switched[frame].temporal;
                const auto& expected = baseline[frame].temporal;
                CHECK(actual.lastReset == expected.lastReset);
                CHECK(actual.historyAge == expected.historyAge);
                CHECK(actual.jitterIndex == expected.jitterIndex);
                CHECK(actual.vendorReset == expected.vendorReset);
                CHECK(actual.vendorScalerGeneration == expected.vendorScalerGeneration);
                CHECK(actual.vendorFallback == expected.vendorFallback);
                if (frame > 0 && cell != 0) {
                    CHECK(actual.lastReset == HistoryResetReason::None);
                    CHECK(actual.historyValid);
                    CHECK_FALSE(actual.vendorReset);
                }
            }
        }
    }
}
