#include "GraphTestSupport.h"

#include "Render/Graph/GraphDump.h"
#include "Render/Renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

namespace {
using namespace lmx;

struct InputSnapshot {
    std::vector<std::string> operations;
    std::vector<std::vector<std::byte>> payloads;
    std::vector<std::byte> localParams;

    //==================================================================================================================
    bool operator==(const InputSnapshot&) const = default;
};

struct LightingCommands final : FakeCommandList {
    InputSnapshot snapshot;

    //==================================================================================================================
    rojoRHI::GpuAddress bindFrameData(uint32_t slot, const void* data, uint64_t size,
                                      uint64_t alignment) override {
        snapshot.operations.push_back("uniform:" + std::to_string(slot) + ":" +
                                      std::to_string(alignment));
        const auto* bytes = static_cast<const std::byte*>(data);
        snapshot.payloads.emplace_back(bytes, bytes + size);
        if (slot == 11)
            snapshot.localParams = snapshot.payloads.back();
        return FakeCommandList::bindFrameData(slot, data, size, alignment);
    }

    //==================================================================================================================
    void bindBuffer(uint32_t slot, rojoRHI::Buffer& buffer) override {
        snapshot.operations.push_back("buffer:" + std::to_string(slot) + ":" +
                                      std::to_string(buffer.size()));
        auto& bytes = snapshot.payloads.emplace_back(buffer.size());
        buffer.readback(bytes.data(), bytes.size());
    }

    //==================================================================================================================
    void bindTexture(uint32_t slot, rojoRHI::Texture& texture,
                     const rojoRHI::TextureViewDesc& view) override {
        const auto& value = static_cast<FakeDevice::TextureObject&>(texture);
        snapshot.operations.push_back("texture:" + std::to_string(slot) + ":" + value.label + ":" +
                                      std::to_string(texture.width()) + ":" +
                                      std::to_string(texture.height()) + ":" +
                                      std::to_string(static_cast<int>(texture.format())) + ":" +
                                      std::to_string(static_cast<int>(view.format)) + ":" +
                                      std::to_string(view.range.baseMipLevel) + ":" +
                                      std::to_string(view.range.mipLevelCount) + ":" +
                                      std::to_string(view.range.baseArrayLayer) + ":" +
                                      std::to_string(view.range.arrayLayerCount));
    }

    //==================================================================================================================
    void bindStorageTexture(uint32_t slot, rojoRHI::Texture& texture,
                            const rojoRHI::TextureViewDesc& view,
                            rojoRHI::StorageAccess access) override {
        snapshot.operations.push_back("storageTexture:" + std::to_string(static_cast<int>(access)));
        bindTexture(slot, texture, view);
    }

    //==================================================================================================================
    void bindStorageBuffer(uint32_t slot, rojoRHI::Buffer& buffer,
                           rojoRHI::StorageAccess access) override {
        snapshot.operations.push_back("storageBuffer:" + std::to_string(static_cast<int>(access)));
        bindBuffer(slot, buffer);
    }

    //==================================================================================================================
    void temporalScale(rojoRHI::TemporalScaler& scaler,
                       const rojoRHI::TemporalScaleParams& params) override {
        snapshot.operations.push_back("vendor:" + std::to_string(params.inputContentWidth) + ":" +
                                      std::to_string(params.inputContentHeight) + ":" +
                                      std::to_string(params.reset) + ":" +
                                      std::to_string(params.reversedDepth));
        const std::array floats{params.jitterOffsetX, params.jitterOffsetY, params.motionScaleX,
                                params.motionScaleY, params.preExposure};
        const auto* bytes = reinterpret_cast<const std::byte*>(floats.data());
        snapshot.payloads.emplace_back(bytes, bytes + sizeof(floats));
        uint32_t slot = 0;
        for (auto* texture : {params.color, params.depth, params.motion, params.reactive,
                              params.exposure, params.output})
            bindTexture(slot++, *texture, {});
        FakeCommandList::temporalScale(scaler, params);
    }

    //==================================================================================================================
    void drawIndexed(rojoRHI::Buffer&, uint32_t count, uint32_t first) override {
        snapshot.operations.push_back("draw:" + std::to_string(first) + ":" +
                                      std::to_string(count));
    }

    //==================================================================================================================
    void drawIndexedIndirect(rojoRHI::Buffer&, rojoRHI::Buffer& arguments,
                             uint64_t offset) override {
        snapshot.operations.push_back("indirect:" + std::to_string(offset));
        auto& bytes = snapshot.payloads.emplace_back(arguments.size());
        arguments.readback(bytes.data(), bytes.size());
    }

    //==================================================================================================================
    void bindPipeline(rojoRHI::GraphicsPipeline& pipeline) override {
        snapshot.operations.push_back("graphics:" +
                                      static_cast<FakeGraphicsPipeline&>(pipeline).label);
    }

    //==================================================================================================================
    void bindComputePipeline(rojoRHI::ComputePipeline& pipeline) override {
        snapshot.operations.push_back("compute:" +
                                      static_cast<FakeComputePipeline&>(pipeline).label);
    }
};

struct DeclaredSnapshot {
    std::string graph;
    InputSnapshot inputs;
    render::TemporalStatus temporal;
};

//======================================================================================================================
std::vector<DeclaredSnapshot> zeroLightReplay(bool switches, bool highWater,
                                              render::ReconstructionMode reconstruction,
                                              bool temporalEnabled, float scale) {
    FakeDevice device;
    device.frame = 1;
    device.deviceCaps.temporalScaler = {
        .available = true, .minInputScale = 0.5f, .maxInputScale = 1.0f, .name = "Fake Temporal"};
    auto renderer = render::Renderer::create(device, 80, 48, false);
    REQUIRE(renderer);
    std::array<engine::InstanceRow, 2> instances{};
    instances[1].materialRow = 1;
    const engine::MeshRow mesh{.indexCount = 3, .vertexCount = 3};
    std::array<engine::DrawItem, 2> items{{{.instanceRow = 0, .mesh = mesh},
                                           {.instanceRow = 1,
                                            .mesh = mesh,
                                            .alphaMode = engine::AlphaMode::Mask,
                                            .doubleSided = true}}};
    std::array<engine::MaterialRow, 2> materials{};
    materials[1].flags = engine::kMaterialMasked | engine::kMaterialDoubleSided;
    FakeDevice::BufferObject vertices(3 * 64);
    FakeDevice::BufferObject indices(3 * sizeof(uint32_t));
    FakeDevice::BufferObject meshBuffer(sizeof(mesh), &mesh);
    FakeDevice::BufferObject instanceBuffer(sizeof(instances), instances.data());
    FakeDevice::BufferObject materialBuffer(sizeof(materials), materials.data());
    FakeDevice::TextureObject sky({.width = 1,
                                   .height = 1,
                                   .format = rojoRHI::Format::RGBA8Unorm,
                                   .kind = rojoRHI::TextureKind::Cube,
                                   .sampled = true,
                                   .label = "test.zeroLightSky"});
    render::SceneView view;
    view.items = items;
    view.visibilityEnabled = false;
    view.skySphere = mesh;
    view.skyCubemap = &sky;
    view.tables = {.vertices = &vertices,
                   .indices = &indices,
                   .meshes = &meshBuffer,
                   .instances = &instanceBuffer,
                   .materials = &materialBuffer,
                   .meshCount = 1,
                   .instanceCount = 2,
                   .materialCount = 2,
                   .instanceRows = instances,
                   .instanceCapacity = 2};
    view.temporal.enabled = temporalEnabled;
    view.temporal.jitterEnabled = temporalEnabled;
    view.temporal.reconstruction = reconstruction;
    view.temporal.renderScale = scale;
    std::array<engine::LightRow, 4> emptyRows{};
    FakeDevice::BufferObject oldLightTable(sizeof(emptyRows), emptyRows.data());
    if (highWater) {
        view.tables.lightRowCount = emptyRows.size();
        view.tables.lightRows = emptyRows;
        view.tables.lights = &oldLightTable;
    }
    constexpr std::array modes{engine::LocalLightMode::Off,       engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Off,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Clustered, engine::LocalLightMode::Direct,
                               engine::LocalLightMode::Off};
    std::vector<DeclaredSnapshot> result;
    for (const auto requested : modes) {
        view.localLightMode = switches ? requested : engine::LocalLightMode::Off;
        LightingCommands commands;
        render::TransientPool pool(device);
        pool.beginFrame();
        render::RenderGraph graph(pool);
        engine::Camera camera;
        camera.position = {0.0f, 0.0f, 5.0f};
        graph.presentTexture((*renderer)->declarePasses(graph, commands, camera, view));
        const auto record = graph.compileFrame(device.frameNumber());
        REQUIRE(record);
        graph.execute(commands, device.frameNumber());
        const auto dump = render::dumpCompiledFrame(*record);
        CHECK(dump.find("lmx.scene.lights") == std::string::npos);
        CHECK(dump.find("lmx.pass.light.") == std::string::npos);
        CHECK(dump.find("lmx.light.") == std::string::npos);
        REQUIRE(commands.snapshot.localParams.size() == 136);
        std::array<uint32_t, 2> modeAndRows{};
        std::memcpy(modeAndRows.data(), commands.snapshot.localParams.data(), sizeof(modeAndRows));
        CHECK(modeAndRows[0] == 0);
        CHECK(modeAndRows[1] == 0);
        CHECK((*renderer)->lightingStatus().requested == view.localLightMode);
        CHECK((*renderer)->lightingStatus().effective == engine::LocalLightMode::Off);
        CHECK((*renderer)->lightingStatus().listBytes == 0);
        CHECK((*renderer)->lightingStatus().allocatedListBytes == 0);
        result.push_back({dump, std::move(commands.snapshot), (*renderer)->temporalStatus()});
        device.endFrame(nullptr);
    }
    return result;
}
} // namespace

//======================================================================================================================
TEST_CASE("zero-live modes preserve graph and shader inputs across temporal cells",
          "[render][zero-light-invariants]") {
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
                CHECK(switched[frame].inputs == baseline[frame].inputs);
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
