#include "GpuRendererTestSupport.h"

#include "Core/Util/Sha256.h"
#include "Render/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

namespace {

using namespace lmx::render;
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kFrames = 32;
constexpr size_t kHistories = 5;
constexpr std::array<const char*, 10> kAttachments{"hdr",       "depth",   "motion",   "reactive",
                                                   "history",   "display", "exposure", "instances",
                                                   "materials", "meshes"};

struct Cell {
    const char* name;
    bool temporal;
    ReconstructionMode reconstruction;
    float scale;
};

constexpr std::array kCells{Cell{"off", false, ReconstructionMode::Raw, 1.0f},
                            Cell{"raw-1", true, ReconstructionMode::Raw, 1.0f},
                            Cell{"taa-1", true, ReconstructionMode::NativeTaa, 1.0f},
                            Cell{"taa-0.5", true, ReconstructionMode::NativeTaa, 0.5f},
                            Cell{"metalfx-1", true, ReconstructionMode::VendorTemporal, 1.0f},
                            Cell{"metalfx-0.5", true, ReconstructionMode::VendorTemporal, 0.5f}};

struct ZeroLightDepthProbe {
    std::unique_ptr<rojoRHI::ShaderLibrary> library;
    std::unique_ptr<rojoRHI::ComputePipeline> pipeline;
    std::unique_ptr<rojoRHI::Buffer> bytes;
};

//======================================================================================================================
ZeroLightDepthProbe makeZeroLightDepthProbe(rojoRHI::Device& device, uint32_t width,
                                            uint32_t height) {
    auto library = device.loadShaderLibrary("Shaders/VisibilityDepthReadback");
    REQUIRE(library);
    auto pipeline = device.createComputePipeline({.library = library->get(),
                                                  .computeEntry = "computeMain",
                                                  .threadsPerThreadgroup = {8, 8, 1},
                                                  .label = "lmx.test.zeroLight.depthReader"});
    REQUIRE(pipeline);
    auto bytes = device.createBuffer({.size = uint64_t{width} * height * 4,
                                      .storageWrite = true,
                                      .cpuReadback = true,
                                      .label = "lmx.test.zeroLight.depthBytes"},
                                     nullptr);
    REQUIRE(bytes);
    return {std::move(*library), std::move(*pipeline), std::move(*bytes)};
}

//======================================================================================================================
Bytes crop(const Bytes& source, uint32_t stride, uint32_t width, uint32_t height, uint32_t bpp) {
    Bytes result(size_t{width} * height * bpp);
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(result.data() + size_t{y} * width * bpp,
                    source.data() + size_t{y} * stride * bpp, size_t{width} * bpp);
    return result;
}

//======================================================================================================================
Bytes readTexture(rojoRHI::Texture& texture, uint32_t bpp, uint32_t width, uint32_t height) {
    Bytes bytes(size_t{texture.width()} * texture.height() * bpp);
    texture.readback(bytes.data(), bytes.size());
    return crop(bytes, texture.width(), width, height, bpp);
}

//======================================================================================================================
bool finiteHalfComponents(const Bytes& bytes) {
    if (bytes.size() % sizeof(uint16_t) != 0)
        return false;
    for (size_t offset = 0; offset < bytes.size(); offset += sizeof(uint16_t)) {
        uint16_t bits;
        std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
        if ((bits & 0x7c00u) == 0x7c00u)
            return false;
    }
    return true;
}

//======================================================================================================================
bool hasHdrColorDetail(const Bytes& bytes) {
    for (size_t offset = 8; offset + 8 <= bytes.size(); offset += 8)
        if (std::memcmp(bytes.data(), bytes.data() + offset, 6) != 0)
            return true;
    return false;
}

//======================================================================================================================
std::string digest(const Bytes& bytes) {
    return lmx::sha256Hex(std::as_bytes(std::span(bytes)));
}

//======================================================================================================================
lmx::engine::LocalLightMode modeFor(size_t history, uint32_t frame) {
    if (history < 2)
        return lmx::engine::LocalLightMode::Off;
    if (history == 2)
        return lmx::engine::LocalLightMode::Direct;
    if (history == 3)
        return lmx::engine::LocalLightMode::Clustered;
    constexpr std::array modes{lmx::engine::LocalLightMode::Off,
                               lmx::engine::LocalLightMode::Direct,
                               lmx::engine::LocalLightMode::Clustered};
    return modes[(frame / 3) % modes.size()];
}

//======================================================================================================================
void compareHistories(rojoRHI::Device& device, const char* sceneName,
                      const lmx::engine::Camera& camera, uint32_t width, uint32_t height,
                      const std::function<SceneView(uint64_t)>& prepare) {
    std::ofstream report;
    std::filesystem::path reportPath;
    if (const char* path = std::getenv("LMX_ZERO_LIGHT_REPORT")) {
        REQUIRE_FALSE(std::filesystem::exists(path));
        reportPath = path;
        report.open(path);
        REQUIRE(report.good());
        report << "scene\tcell\tframe\thistory\tmode\tattachment\tbytes\tdifferingBytes"
                  "\tfirstDifference\tbaselineSha256\tactualSha256\n";
    }
    const char* selectedCell = std::getenv("LMX_ZERO_LIGHT_CELL");
    if (selectedCell)
        REQUIRE(std::ranges::any_of(
            kCells, [&](const Cell& cell) { return std::string_view(cell.name) == selectedCell; }));
    const char* selectedSubmission = std::getenv("LMX_ZERO_LIGHT_SUBMISSION");
    if (selectedSubmission)
        REQUIRE(std::string_view(selectedSubmission) == "direct");
    auto probe = makeZeroLightDepthProbe(device, width, height);
    for (const auto& cell : kCells) {
        if (selectedCell && std::string_view(cell.name) != selectedCell)
            continue;
        {
            INFO(sceneName << " " << cell.name);
            if (cell.reconstruction == ReconstructionMode::VendorTemporal &&
                !device.capabilities().temporalScaler.available) {
                SKIP("MetalFX unavailable; the six-cell zero-light matrix is incomplete");
            }
            std::array<std::unique_ptr<Renderer>, kHistories> renderers;
            for (auto& renderer : renderers) {
                auto created = Renderer::create(device, width, height, true);
                REQUIRE(created);
                renderer = std::move(*created);
            }
            bool savedDifference = false;
            for (uint32_t frame = 0; frame < kFrames; ++frame) {
                std::array<Bytes, kAttachments.size()> baseline;
                TemporalStatus baselineStatus;
                for (size_t history = 0; history < renderers.size(); ++history) {
                    auto& renderer = *renderers[history];
                    auto& commands = device.beginFrame();
                    auto view = prepare(device.frameNumber());
                    REQUIRE(view.tables.liveLightCount == 0);
                    view.localLightMode = modeFor(history, frame);
                    if (selectedSubmission)
                        view.submission = SubmissionMode::Direct;
                    view.temporal.enabled = cell.temporal;
                    view.temporal.jitterEnabled = cell.temporal;
                    view.temporal.reconstruction = cell.reconstruction;
                    view.temporal.renderScale = cell.scale;
                    view.temporal.cameraCut = false;
                    view.autoExposureEnabled = false;
                    view.exposureEv = 0;
                    renderer.render(commands, camera, view, false);
                    commands.textureBarrier(renderer.depthTarget(),
                                            rojoRHI::TextureUse::RenderTarget,
                                            rojoRHI::TextureUse::ShaderRead);
                    commands.beginComputePass("lmx.test.zeroLight.readDepth");
                    commands.bindComputePipeline(*probe.pipeline);
                    commands.bindStorageBuffer(0, *probe.bytes, rojoRHI::StorageAccess::Write);
                    commands.bindTexture(1, renderer.depthTarget());
                    commands.dispatch((width + 7) / 8, (height + 7) / 8, 1);
                    commands.endComputePass();
                    device.endFrame(nullptr);
                    device.waitIdle();
                    renderer.drainLightingAfterIdle();
                    REQUIRE(renderer.lightingStatus().effective ==
                            lmx::engine::LocalLightMode::Off);
                    REQUIRE(renderer.lightingStatus().liveLightCount == 0);
                    const auto status = renderer.temporalStatus();
                    REQUIRE(status.vendorFallback == VendorFallback::None);
                    if (cell.temporal) {
                        REQUIRE(status.reconstruction == cell.reconstruction);
                        REQUIRE(status.lastReset == (frame == 0 ? HistoryResetReason::FirstFrame
                                                                : HistoryResetReason::None));
                        REQUIRE(status.historyValid == (frame > 0));
                    }
                    const uint32_t activeWidth = status.extents.renderWidth;
                    const uint32_t activeHeight = status.extents.renderHeight;
                    std::array<Bytes, kAttachments.size()> image;
                    image[0] = readTexture(renderer.hdrColorTarget(), 8, activeWidth, activeHeight);
                    Bytes depth(size_t{width} * height * 4);
                    probe.bytes->readback(depth.data(), depth.size());
                    image[1] = crop(depth, width, activeWidth, activeHeight, 4);
                    if (cell.temporal) {
                        image[2] =
                            readTexture(*renderer.motionTarget(), 4, activeWidth, activeHeight);
                        image[3] =
                            readTexture(*renderer.reactiveTarget(), 1, activeWidth, activeHeight);
                        if (cell.reconstruction != ReconstructionMode::VendorTemporal)
                            image[4] = readTexture(*renderer.historyTarget(), 8, width, height);
                    }
                    image[5] = readTexture(renderer.colorTarget(), 4, width, height);
                    image[6].resize(renderer.exposureBuffer().size());
                    renderer.exposureBuffer().readback(image[6].data(), image[6].size());
                    const std::array buffers{view.tables.instances, view.tables.materials,
                                             view.tables.meshes};
                    const std::array sizes{
                        size_t{view.tables.instanceCount} * sizeof(lmx::engine::InstanceRow),
                        size_t{view.tables.materialCount} * sizeof(lmx::engine::MaterialRow),
                        size_t{view.tables.meshCount} * sizeof(lmx::engine::MeshRow)};
                    for (size_t i = 0; i < buffers.size(); ++i) {
                        REQUIRE(buffers[i] != nullptr);
                        image[7 + i].resize(sizes[i]);
                        buffers[i]->readback(image[7 + i].data(), sizes[i]);
                    }
                    REQUIRE(hasHdrColorDetail(image[0]));
                    REQUIRE(finiteHalfComponents(image[0]));
                    REQUIRE(finiteHalfComponents(image[4]));
                    if (history == 0) {
                        baseline = std::move(image);
                        baselineStatus = status;
                        continue;
                    }
                    CHECK(status.jitterIndex == baselineStatus.jitterIndex);
                    CHECK(status.historyAge == baselineStatus.historyAge);
                    for (size_t attachment = 0; attachment < image.size(); ++attachment) {
                        if (image[attachment].empty())
                            continue;
                        REQUIRE(image[attachment].size() == baseline[attachment].size());
                        size_t differing = 0;
                        size_t first = image[attachment].size();
                        for (size_t i = 0; i < image[attachment].size(); ++i) {
                            if (image[attachment][i] != baseline[attachment][i]) {
                                if (differing == 0)
                                    first = i;
                                ++differing;
                            }
                        }
                        INFO("frame=" << frame + 1 << " history=" << history << " attachment="
                                      << kAttachments[attachment] << " firstByte=" << first);
                        CHECK(differing == 0);
                        if (differing > 0 && !savedDifference && report.is_open()) {
                            const auto directory = reportPath.string() + "." + cell.name;
                            REQUIRE(std::filesystem::create_directory(directory));
                            std::ofstream metadata(directory + "/context.tsv");
                            metadata << sceneName << '\t' << frame + 1 << '\t' << history << '\t'
                                     << activeWidth << '\t' << activeHeight << '\t' << width << '\t'
                                     << height << '\n';
                            for (size_t a = 0; a < image.size(); ++a) {
                                if (image[a].empty())
                                    continue;
                                for (const bool reference : {true, false}) {
                                    const auto& bytes = reference ? baseline[a] : image[a];
                                    const auto path = directory + "/" + kAttachments[a] +
                                                      (reference ? ".baseline.raw" : ".actual.raw");
                                    std::ofstream raw(path, std::ios::binary);
                                    raw.write(reinterpret_cast<const char*>(bytes.data()),
                                              bytes.size());
                                    REQUIRE(raw.good());
                                }
                            }
                            savedDifference = true;
                        }
                        if (report.is_open()) {
                            report << sceneName << '\t' << cell.name << '\t' << frame + 1 << '\t'
                                   << history << '\t' << static_cast<int>(view.localLightMode)
                                   << '\t' << kAttachments[attachment] << '\t'
                                   << image[attachment].size() << '\t' << differing << '\t' << first
                                   << '\t' << digest(baseline[attachment]) << '\t'
                                   << digest(image[attachment]) << '\n';
                        }
                    }
                }
            }
        }
    }
}

} // namespace

//======================================================================================================================
TEST_CASE("zero-light requested modes preserve controlled GPU histories",
          "[gpu][zero-light-history]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto cube = fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.zeroLight.cube");
    REQUIRE(cube);
    auto sky = fixtureMesh(**device, lmx::engine::fromGeo(lmx::asset::makeSphere(0.5f, 20, 20)),
                           "lmx.test.zeroLight.sky");
    REQUIRE(sky);
    const uint32_t skyTexel = 0xffff8000u;
    const rojoRHI::TextureMip skyMip{.data = &skyTexel, .bytesPerRow = 4};
    const std::array skyFaces{skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};
    auto cubemap = (*device)->createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA8Unorm,
                                             .kind = rojoRHI::TextureKind::Cube,
                                             .sampled = true,
                                             .label = "lmx.test.zeroLight.skyTexture"},
                                            skyFaces);
    REQUIRE(cubemap);
    std::array<uint32_t, 64> maskTexels{};
    for (size_t i = 0; i < maskTexels.size(); ++i)
        maskTexels[i] = ((i / 8 + i % 8) % 2) ? 0xffffffffu : 0x00ffffffu;
    const rojoRHI::TextureMip maskMip{.data = maskTexels.data(), .bytesPerRow = 8 * 4};
    auto mask = (*device)->createTexture({.width = 8,
                                          .height = 8,
                                          .format = rojoRHI::Format::RGBA8Unorm,
                                          .sampled = true,
                                          .label = "lmx.test.zeroLight.mask"},
                                         std::span{&maskMip, 1});
    REQUIRE(mask);
    auto items = twoCubeScene(*cube);
    items[1].material.alphaMode = lmx::engine::AlphaMode::Mask;
    items[1].material.diffuse = mask->get();
    const std::array disabled{lmx::engine::LocalLight{.range = 2.0f, .enabled = false}};
    FixtureSceneView fixture = litSceneView(items);
    fixture.skySphere = &*sky;
    fixture.skyCubemap = cubemap->get();
    fixture.localLights = disabled;
    compareHistories(**device, "opaque-mask-sky-disabled", sceneCamera(), 160, 90, [&](uint64_t) {
        auto view = lmx::test::prepareSceneView(fixture, device);
        REQUIRE(view.tables.lightRowCount == 1);
        REQUIRE(fixture.state->scene.localLights().size() == 1);
        return view;
    });
}

//======================================================================================================================
// Fetched Sponza is an explicit diagnostic, separate from the small always-available fixture.
TEST_CASE("Sponza zero-light histories repeat at the frozen baseline camera",
          "[.][gpu][zero-light-history-sponza]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = lmx::scenes::loadSponzaScene(**device);
    REQUIRE(scene);
    const std::vector ids((*scene)->localLights().begin(), (*scene)->localLights().end());
    for (const auto id : ids) {
        auto light = *(*scene)->light(id);
        light.enabled = false;
        REQUIRE((*scene)->updateLight(id, light));
    }
    lmx::engine::Camera camera;
    const glm::vec3 center{(*scene)->boundingSphere};
    const float radius = (*scene)->boundingSphere.w;
    camera.position = center + glm::vec3(radius * 0.6f, 0, 0);
    camera.yaw = -glm::half_pi<float>();
    camera.pitch = 0;
    camera.fovY = glm::radians(45.0f);
    camera.nearZ = 0.05f;
    camera.farZ = radius * 20.0f;
    std::vector<lmx::engine::DrawItem> items;
    compareHistories(**device, "sponza-frozen-camera", camera, 1280, 720, [&](uint64_t frame) {
        REQUIRE((*scene)->prepareFrame(frame));
        return buildSceneView(**scene, items, ShadowFilter::PCF, false);
    });
}
