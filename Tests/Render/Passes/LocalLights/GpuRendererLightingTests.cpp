#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"
#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include "Engine/Lights/LocalLight.h"
#include "Engine/Lights/LocalLightMath.h"
#include "Render/Graph/GraphDump.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <ranges>
#include <sstream>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Raw HDR equality also checks the always-bound Off path under Metal validation.
TEST_CASE("a zero-light frame is byte-identical across the three local-light modes",
          "[gpu][light]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.modeCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSceneProbeSize, kSceneProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const auto items = twoCubeScene(*cube);
    FixtureSceneView view = litSceneView(items);

    const auto renderWith = [&](lmx::engine::LocalLightMode mode, bool removeLast = false,
                                uint32_t expectedLights = 0) {
        view.localLightMode = mode;
        if (removeLast) {
            auto& scene = view.state->scene;
            const auto light = scene.addLight({.range = 2.0f});
            REQUIRE(light.has_value());
            REQUIRE(scene.removeLight(*light));
        }
        CommandList& commands = (*device)->beginFrame();
        const lmx::render::SceneView prepared = lmx::test::prepareSceneView(view, device);
        if (removeLast)
            REQUIRE(prepared.tables.lightRowCount > 0);
        REQUIRE(prepared.tables.liveLightCount == expectedLights);
        lmx::render::TransientPool transients(**device);
        transients.beginFrame();
        lmx::render::RenderGraph graph(transients);
        const auto output = (*renderer)->declarePasses(graph, commands, sceneCamera(), prepared);
        graph.exportTexture(output);
        const auto record = graph.compileFrame((*device)->frameNumber());
        REQUIRE(record.has_value());
        const auto dump = lmx::render::dumpCompiledFrame(*record);
        REQUIRE((dump.find("lmx.scene.lights") != std::string::npos) == (expectedLights > 0));
        if (expectedLights == 0)
            REQUIRE(dump.find("lmx.pass.light.") == std::string::npos);
        graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        std::vector<uint16_t> texels(size_t{kSceneProbeSize} * kSceneProbeSize * 4);
        (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));
        return texels;
    };

    const std::vector<uint16_t> off = renderWith(lmx::engine::LocalLightMode::Off);
    const std::vector<uint16_t> direct = renderWith(lmx::engine::LocalLightMode::Direct);
    const std::vector<uint16_t> clustered = renderWith(lmx::engine::LocalLightMode::Clustered);

    // A frame that drew nothing would pass trivially, so pin that the image has content first.
    REQUIRE(std::ranges::any_of(off, [](uint16_t bits) { return bits != 0; }));
    REQUIRE(direct == off);
    REQUIRE(clustered == off);
    REQUIRE(renderWith(lmx::engine::LocalLightMode::Direct, true) == off);

    // Populate every paced slot with real cluster work, then disable the final identity. Stale
    // list resources must not become visible when any zero-enabled mode reuses those slots.
    auto& scene = view.state->scene;
    lmx::engine::LocalLight local{
        .position = {0.0f, 0.0f, 3.0f}, .intensity = 20.0f, .range = 8.0f};
    const auto id = scene.addLight(local);
    REQUIRE(id);
    for (uint32_t slot = 0; slot < 3; ++slot)
        REQUIRE(renderWith(lmx::engine::LocalLightMode::Clustered, false, 1) != off);
    local.enabled = false;
    REQUIRE(scene.updateLight(*id, local));
    REQUIRE(scene.localLights().size() == 1);
    REQUIRE(scene.enabledLightCount() == 0);
    constexpr std::array modes{lmx::engine::LocalLightMode::Off,
                               lmx::engine::LocalLightMode::Direct,
                               lmx::engine::LocalLightMode::Clustered};
    for (uint32_t round = 0; round < 3; ++round)
        for (uint32_t slot = 0; slot < 3; ++slot)
            REQUIRE(renderWith(modes[(round + slot) % modes.size()]) == off);
}

namespace {

const glm::vec4 kLitProbeAlbedo{0.5f, 0.5f, 0.5f, 1.0f};
// Above Lighting.slang's kMinRoughness, so the shader's clamp leaves it alone.
constexpr float kLitProbeRoughness = 0.5f;
// A pure dielectric, so F0FromBaseColor is the 4% constant and needs no mirror here.
constexpr float kLitProbeMetallic = 0.0f;
// preExposure 2, exactly representable in binary16 and in the auto path's seeded buffer alike.
constexpr float kLitProbeExposureEv = 1.0f;

//======================================================================================================================
// Intersect the probe pixel's centre ray with the plane; the origin is not a pixel centre.
inline glm::vec3 probeSurfacePoint(const Camera& camera, uint32_t size, uint32_t px, uint32_t py,
                                   glm::vec3 planeNormal) {
    const glm::mat4 inverseViewProj =
        glm::inverse(camera.projectionMatrix(1.0f) * camera.viewMatrix());
    const float ndcX = (static_cast<float>(px) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(size) * 2.0f;
    const auto unproject = [&](float depth) {
        const glm::vec4 world = inverseViewProj * glm::vec4(ndcX, ndcY, depth, 1.0f);
        return glm::vec3(world) / world.w;
    };
    // Reversed depth: 1 is the near plane and smaller values are farther, so these two points are
    // distinct and both lie in front of the camera.
    const glm::vec3 near = unproject(1.0f);
    const glm::vec3 direction = unproject(0.5f) - near;
    const float denominator = glm::dot(planeNormal, direction);
    REQUIRE(std::abs(denominator) > 1e-6f);
    return near + direction * (-glm::dot(planeNormal, near) / denominator);
}

//======================================================================================================================
// The target stores binary16, so compare with the CPU result rounded to that storage format.
inline float storedAsHalf(float value) {
    return static_cast<float>(static_cast<_Float16>(value));
}

} // namespace

//======================================================================================================================
// Zero directional strength, black fallback IBL and no emissive isolate the local contribution.
TEST_CASE("a point light shades the scene pass to its CPU mirror times pre-exposure",
          "[gpu][light]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto quad =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(8.0f), "lmx.test.litProbeQuad");
    INFO(errorOf(quad));
    REQUIRE(quad.has_value());

    auto renderer = Renderer::create(**device, kBrdfProbeSize, kBrdfProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    // Rotating the +Y plane a quarter turn about X turns its normal towards the camera on +Z.
    const glm::mat4 model =
        glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*quad,
         .model = model,
         .material = {.albedo = kLitProbeAlbedo,
                      .roughness = kLitProbeRoughness,
                      .metallic = kLitProbeMetallic}},
    }};

    // Off the view axis on both axes, so neither N.L nor the distance falls out of the geometry by
    // symmetry, and well inside its range so the window term is neither 0 nor 1.
    const std::array<lmx::engine::LocalLight, 1> lights = {{
        {.type = lmx::engine::LocalLightType::Point,
         .position = {1.0f, 1.5f, 3.0f},
         .colour = {1.0f, 1.0f, 1.0f},
         .intensity = 8.0f,
         .range = 20.0f},
    }};

    FixtureSceneView view;
    view.items = items;
    view.localLights = lights;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 12.0f};
    view.exposureEv = kLitProbeExposureEv;
    view.localLightMode = lmx::engine::LocalLightMode::Direct;

    const Camera camera = pinnedAngleCamera();

    CommandList& commands = (*device)->beginFrame();
    const lmx::render::SceneView prepared = lmx::test::prepareSceneView(view, device);
    REQUIRE(prepared.tables.liveLightCount == 1);
    REQUIRE(prepared.tables.lightRowCount == 1);
    REQUIRE(prepared.tables.lights != nullptr);
    REQUIRE(prepared.tables.instanceRows.size() == 1);

    // The matrices the vertex shader will use, so the reference below shades the surface the
    // fragment shades rather than the one this test authored.
    const glm::mat4 worldTransform = prepared.tables.instanceRows[0].model;
    const glm::mat4 normalTransform = prepared.tables.instanceRows[0].normalMatrix;

    (*renderer)->render(commands, camera, prepared, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> texels(size_t{kBrdfProbeSize} * kBrdfProbeSize * 4);
    (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));

    const glm::vec3 planeNormal =
        glm::normalize(glm::vec3(worldTransform * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}));
    const glm::vec3 shadingNormal =
        glm::normalize(glm::vec3(normalTransform * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}));
    constexpr uint32_t kProbeX = kBrdfProbeSize / 2;
    constexpr uint32_t kProbeY = kBrdfProbeSize / 2;
    const glm::vec3 surface =
        probeSurfacePoint(camera, kBrdfProbeSize, kProbeX, kProbeY, planeNormal);
    const glm::vec3 toEye = glm::normalize(camera.position - surface);

    auto row = lmx::engine::makeLightRow(lights[0]);
    INFO(errorOf(row));
    REQUIRE(row.has_value());

    // The two BRDF inputs the shader derives: a dielectric's 4% normal-incidence reflectance, and
    // the GGX alpha, which is the perceptual roughness squared.
    const glm::vec3 baseColour = glm::vec3(kLitProbeAlbedo);
    const glm::vec3 f0{0.04f};
    constexpr float kAlpha = kLitProbeRoughness * kLitProbeRoughness;
    const float preExposure = std::exp2(kLitProbeExposureEv);
    const glm::vec3 expected =
        lmx::engine::computePunctualLight(*row, surface, shadingNormal, toEye, baseColour, f0,
                                          kLitProbeMetallic, kAlpha) *
        preExposure;

    const glm::vec3 radiance = hdrTexelAt(texels, kBrdfProbeSize, kProbeX, kProbeY);
    INFO("surface (" << surface.x << ", " << surface.y << ", " << surface.z << "), expected ("
                     << expected.r << ", " << expected.g << ", " << expected.b << "), read ("
                     << radiance.r << ", " << radiance.g << ", " << radiance.b << ")");
    // A zero reference would make the comparison vacuous: the light has to actually reach here.
    REQUIRE(expected.r > 0.05f);
    REQUIRE(radiance.r == Catch::Approx(storedAsHalf(expected.r)).epsilon(1e-4));
    REQUIRE(radiance.g == Catch::Approx(storedAsHalf(expected.g)).epsilon(1e-4));
    REQUIRE(radiance.b == Catch::Approx(storedAsHalf(expected.b)).epsilon(1e-4));
}

//======================================================================================================================
// Exposure reset seeds exactly exp2(exposureEv), matching manual exposure in all four variants.
TEST_CASE("the masked and auto-exposure scene variants light a local light identically",
          "[gpu][light]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto quad =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(8.0f), "lmx.test.litVariantQuad");
    INFO(errorOf(quad));
    REQUIRE(quad.has_value());

    auto renderer = Renderer::create(**device, kBrdfProbeSize, kBrdfProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model =
        glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    const std::array<lmx::engine::LocalLight, 1> lights = {{
        {.type = lmx::engine::LocalLightType::Point,
         .position = {1.0f, 1.5f, 3.0f},
         .colour = {1.0f, 1.0f, 1.0f},
         .intensity = 8.0f,
         .range = 20.0f},
    }};

    const Camera camera = pinnedAngleCamera();

    // Alpha 1 against a cutoff of 0.5 keeps every probe pixel covered, so the masked variant
    // shades the same surface rather than a partly discarded one.
    const auto probeVariant = [&](bool masked, bool automatic) {
        const std::array<FixtureDrawItem, 1> items = {{
            {.mesh = &*quad,
             .model = model,
             .material = {.albedo = kLitProbeAlbedo,
                          .roughness = kLitProbeRoughness,
                          .metallic = kLitProbeMetallic,
                          .alphaMode = masked ? lmx::engine::AlphaMode::Mask
                                              : lmx::engine::AlphaMode::Opaque,
                          .alphaCutoff = 0.5f}},
        }};

        FixtureSceneView view;
        view.items = items;
        view.localLights = lights;
        for (DirectionalLight& light : view.lights) {
            light.strength = {0.0f, 0.0f, 0.0f};
        }
        view.boundingSphere = {0.0f, 0.0f, 0.0f, 12.0f};
        view.exposureEv = kLitProbeExposureEv;
        view.autoExposureEnabled = automatic;
        view.exposureReset = automatic;
        view.localLightMode = lmx::engine::LocalLightMode::Direct;

        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::vector<uint16_t> texels(size_t{kBrdfProbeSize} * kBrdfProbeSize * 4);
        (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));
        constexpr size_t kOffset =
            (size_t{kBrdfProbeSize / 2} * kBrdfProbeSize + kBrdfProbeSize / 2) * 4;
        return std::array<uint16_t, 4>{texels[kOffset], texels[kOffset + 1], texels[kOffset + 2],
                                       texels[kOffset + 3]};
    };

    const std::array<uint16_t, 4> opaqueManual =
        probeVariant(/*masked=*/false, /*automatic=*/false);
    // A dark probe would hide any difference the variants could have.
    REQUIRE(floatOfHalfBits(opaqueManual[0]) > 0.05f);
    REQUIRE(probeVariant(/*masked=*/true, /*automatic=*/false) == opaqueManual);
    REQUIRE(probeVariant(/*masked=*/false, /*automatic=*/true) == opaqueManual);
    REQUIRE(probeVariant(/*masked=*/true, /*automatic=*/true) == opaqueManual);
}

//======================================================================================================================
TEST_CASE("LightLab graph declares the selected light consumers",
          "[gpu][light][clustered-consumer]") {
    using namespace lmx::render;
    for (const auto mode :
         {lmx::engine::LocalLightMode::Clustered, lmx::engine::LocalLightMode::Direct}) {
        auto device = rojoRHI::createDevice();
        REQUIRE(device);
        auto scene = lmx::scenes::loadLightLabScene(**device, 256, 0);
        REQUIRE(scene);
        auto renderer = Renderer::create(**device, 64, 64, true);
        REQUIRE(renderer);
        TransientPool pool(**device);
        auto& commands = (*device)->beginFrame();
        REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
        std::vector<lmx::engine::DrawItem> items;
        auto view = buildSceneView(**scene, items, ShadowFilter::PCF, false);
        view.localLightMode = mode;
        view.temporal.enabled = false;
        view.bloomEnabled = false;
        const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
        pool.beginFrame();
        RenderGraph graph(pool);
        graph.exportTexture((*renderer)->declarePasses(graph, commands, camera, view));
        const auto record = graph.compileFrame((*device)->frameNumber());
        REQUIRE(record);
        const auto& debug = record->debug;
        const bool clustered = mode == lmx::engine::LocalLightMode::Clustered;
        for (const auto label : {"lmx.pass.light.reset", "lmx.pass.light.count",
                                 "lmx.pass.light.scan", "lmx.pass.light.fill"}) {
            const auto pass = std::ranges::find(debug.passes, label, &DebugPass::label);
            REQUIRE((pass != debug.passes.end()) == clustered);
            if (clustered)
                REQUIRE_FALSE(pass->cullReason);
        }
        for (const auto name : {"lmx.light.grid", "lmx.light.indices"}) {
            const bool barrier = std::ranges::any_of(debug.transitions, [&](const auto& item) {
                return debug.resources[item.resource].name == name &&
                       item.kind == GraphResourceKind::Buffer &&
                       item.bufferTo == rojoRHI::BufferUse::ShaderRead;
            });
            REQUIRE(barrier == clustered);
        }
        graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        const std::string path =
            std::string(LMX_REPO_ROOT) + "/Tests/Golden/" +
            (clustered ? "frame-light-clustered.txt" : "frame-light-direct.txt");
        const auto dump = dumpCompiledFrame(*record);
        std::ifstream expected(path);
        std::ostringstream contents;
        contents << expected.rdbuf();
        if (contents.str() != dump)
            std::ofstream(path + ".actual") << dump;
        REQUIRE(expected.good());
        REQUIRE(contents.str() == dump);
    }
}

namespace {

struct LightingDepthReadback {
    std::unique_ptr<rojoRHI::ShaderLibrary> library;
    std::unique_ptr<rojoRHI::ComputePipeline> pipeline;
    std::unique_ptr<rojoRHI::Buffer> buffer;
};

//======================================================================================================================
LightingDepthReadback copyLightingDepth(rojoRHI::Device& device, rojoRHI::CommandList& commands,
                                        Renderer& renderer) {
    using namespace rojoRHI;
    auto library = device.loadShaderLibrary("Shaders/VisibilityDepthReadback");
    REQUIRE(library);
    auto pipeline = device.createComputePipeline({.library = library->get(),
                                                  .computeEntry = "computeMain",
                                                  .threadsPerThreadgroup = {8, 8, 1},
                                                  .label = "lmx.test.lightingDepthReader"});
    REQUIRE(pipeline);
    auto snapshot = device.createBuffer({.size = uint64_t{renderer.width()} * renderer.height() * 4,
                                         .storageWrite = true,
                                         .cpuReadback = true,
                                         .label = "lmx.test.lightingDepthReadback"},
                                        nullptr);
    REQUIRE(snapshot);
    commands.textureBarrier(renderer.depthTarget(), TextureUse::RenderTarget,
                            TextureUse::ShaderRead);
    commands.beginComputePass("lmx.test.readLightingDepth");
    commands.bindComputePipeline(**pipeline);
    commands.bindStorageBuffer(0, **snapshot, StorageAccess::Write);
    commands.bindTexture(1, renderer.depthTarget());
    commands.dispatch((renderer.width() + 7) / 8, (renderer.height() + 7) / 8, 1);
    commands.endComputePass();
    return {std::move(*library), std::move(*pipeline), std::move(*snapshot)};
}

//======================================================================================================================
std::vector<uint8_t> lightAttachmentBytes(rojoRHI::Texture& texture, uint32_t bytesPerPixel) {
    std::vector<uint8_t> result(size_t{texture.width()} * texture.height() * bytesPerPixel);
    texture.readback(result.data(), result.size());
    return result;
}

} // namespace

//======================================================================================================================
TEST_CASE("LightLab direct and clustered paths preserve all written scene attachments",
          "[gpu][light][clustered-consumer]") {
    using namespace lmx::render;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = lmx::scenes::loadLightLabScene(**device, 256, 0);
    REQUIRE(scene);
    const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
    // Temporal off writes HDR/depth only. Raw, unjittered temporal frames also initialize the
    // motion/reactive attachments, so their equality cannot pass on uninitialized allocation bytes.
    for (const bool motionEnabled : {false, true}) {
        std::array<std::vector<uint8_t>, 4> reference;
        for (const auto mode :
             {lmx::engine::LocalLightMode::Direct, lmx::engine::LocalLightMode::Clustered}) {
            auto renderer = Renderer::create(**device, 160, 90, true);
            REQUIRE(renderer);
            LightingDepthReadback depthReadback;
            for (uint32_t step = 0; step < 2; ++step) {
                auto& commands = (*device)->beginFrame();
                REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
                std::vector<lmx::engine::DrawItem> items;
                auto view = buildSceneView(**scene, items, ShadowFilter::PCF, false);
                view.localLightMode = mode;
                view.temporal.enabled = motionEnabled;
                view.temporal.jitterEnabled = false;
                view.temporal.reconstruction = ReconstructionMode::Raw;
                view.bloomEnabled = false;
                (*renderer)->render(commands, camera, view, false);
                depthReadback = copyLightingDepth(**device, commands, **renderer);
                (*device)->endFrame(nullptr);
                (*device)->waitIdle();
            }
            (*renderer)->drainLightingAfterIdle();
            const auto status = (*renderer)->lightingStatus();
            REQUIRE(status.isRetired);
            REQUIRE(status.effective == mode);
            REQUIRE(status.liveLightCount == 256);
            if (mode == lmx::engine::LocalLightMode::Clustered) {
                REQUIRE(status.counters.assigned > 0);
                REQUIRE(status.counters.truncatedFroxels == 0);
            }
            std::array<std::vector<uint8_t>, 4> image;
            image[0] = lightAttachmentBytes((*renderer)->hdrColorTarget(), 8);
            image[1].resize(160 * 90 * 4);
            depthReadback.buffer->readback(image[1].data(), image[1].size());
            if (motionEnabled) {
                image[2] = lightAttachmentBytes(*(*renderer)->motionTarget(), 4);
                image[3] = lightAttachmentBytes(*(*renderer)->reactiveTarget(), 1);
                REQUIRE_FALSE(image[2].empty());
                REQUIRE_FALSE(image[3].empty());
            }
            if (mode == lmx::engine::LocalLightMode::Direct)
                reference = std::move(image);
            else {
                INFO("motion attachments enabled: " << motionEnabled);
                for (uint32_t attachment = 0; attachment < image.size(); ++attachment) {
                    INFO("attachment " << attachment);
                    REQUIRE(image[attachment] == reference[attachment]);
                }
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("lighting retirement keeps declaration modes through paced switches",
          "[gpu][light][clustered-consumer]") {
    using namespace lmx::render;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = lmx::scenes::loadLightLabScene(**device, 64, 0);
    REQUIRE(scene);
    auto renderer = Renderer::create(**device, 64, 64, true);
    REQUIRE(renderer);
    const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
    const std::array modes{
        lmx::engine::LocalLightMode::Clustered, lmx::engine::LocalLightMode::Direct,
        lmx::engine::LocalLightMode::Off,       lmx::engine::LocalLightMode::Clustered,
        lmx::engine::LocalLightMode::Off,       lmx::engine::LocalLightMode::Direct};
    uint32_t retiredCount = 0;
    uint64_t firstFrame = 0;
    const auto check = [&] {
        for (const auto& status : (*renderer)->takeRetiredLighting()) {
            REQUIRE(status.frameNumber == firstFrame + retiredCount);
            REQUIRE(status.sceneGeneration == 42 + firstFrame + retiredCount);
            REQUIRE(status.isRetired);
            REQUIRE(status.checkEnabled);
            REQUIRE(status.checkPassed());
            if (status.effective == lmx::engine::LocalLightMode::Clustered) {
                REQUIRE(status.checkFrame);
                REQUIRE(status.checkFrame->frameNumber == status.frameNumber);
                REQUIRE(status.checkFrame->params.rowCount == 64);
                REQUIRE(status.checkFrame->gpu.indices == status.checkFrame->cpu.indices);
            } else {
                REQUIRE_FALSE(status.checkFrame);
            }
            REQUIRE(status.requested == modes[retiredCount]);
            REQUIRE(status.effective == modes[retiredCount]);
            REQUIRE(status.liveLightCount == 64);
            REQUIRE((status.counters.assigned > 0) ==
                    (status.effective == lmx::engine::LocalLightMode::Clustered));
            REQUIRE(status.listBytes == status.counters.assigned * sizeof(uint32_t));
            ++retiredCount;
        }
    };
    uint32_t step = 0;
    for (const auto mode : modes) {
        auto& commands = (*device)->beginFrame();
        if (step == 0)
            firstFrame = (*device)->frameNumber();
        (*scene)->advanceAnimation(0.4);
        REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
        std::vector<lmx::engine::DrawItem> items;
        auto view = buildSceneView(**scene, items, ShadowFilter::PCF, false);
        view.localLightMode = mode;
        view.lightCheck = true;
        view.temporal.enabled = false;
        view.temporal.sceneGeneration = 42 + (*device)->frameNumber();
        (*renderer)->render(commands, camera, view, false);
        check();
        REQUIRE(retiredCount == (step < 3 ? 0 : step - 2));
        (*device)->endFrame(nullptr);
        ++step;
    }
    (*device)->waitIdle();
    (*renderer)->drainLightingAfterIdle();
    check();
    REQUIRE(retiredCount == modes.size());
    REQUIRE((*renderer)->takeRetiredLighting().empty());
}

//======================================================================================================================
TEST_CASE("cluster overflow darkens only pixels in truncated froxels",
          "[gpu][light][clustered-consumer]") {
    using namespace lmx::render;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(20.0f), "lmx.test.overflowPlane");
    REQUIRE(plane);
    const glm::mat4 model = glm::rotate(glm::mat4{1}, glm::radians(90.0f), glm::vec3{1, 0, 0});
    const std::array<FixtureDrawItem, 1> items{
        {{.mesh = &*plane, .model = model, .material = {.albedo = {0.5f, 0.5f, 0.5f, 1}}}}};
    std::vector<lmx::engine::LocalLight> lights(
        129, {.position = {-1.5f, 0, 1}, .intensity = 0.025f, .range = 1.5f});
    lights.push_back({.position = {1.5f, 0, 1}, .intensity = 2.0f, .range = 1.5f});
    FixtureSceneView view;
    view.items = items;
    view.localLights = lights;
    view.temporal.enabled = false;
    view.boundingSphere.w = 12;
    for (auto& light : view.lights)
        light.strength = {0, 0, 0};
    constexpr uint32_t width = 160, height = 90;
    const auto camera = sceneCamera();
    auto renderer = Renderer::create(**device, width, height, true);
    REQUIRE(renderer);
    std::array<std::vector<uint16_t>, 2> images;
    std::vector<float> depth(width * height);
    LightClusterLists mirror;
    const auto slices = clusterSliceDepths(camera.nearZ);
    uint32_t path = 0;
    for (const auto mode :
         {lmx::engine::LocalLightMode::Direct, lmx::engine::LocalLightMode::Clustered}) {
        auto& commands = (*device)->beginFrame();
        view.localLightMode = mode;
        const auto prepared = lmx::test::prepareSceneView(view, device);
        LightClusterParams params{.view = camera.viewMatrix(),
                                  .inverseJitteredProjection = glm::inverse(
                                      camera.projectionMatrix(float(width) / float(height))),
                                  .rowCount = prepared.tables.lightRowCount,
                                  .activeWidth = width,
                                  .activeHeight = height,
                                  .sliceDepth = slices};
        mirror = buildLightClusters(prepared.tables.lightRows, params);
        (*renderer)->render(commands, camera, prepared, false);
        auto depthReadback = copyLightingDepth(**device, commands, **renderer);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->drainLightingAfterIdle();
        images[path].resize(width * height * 4);
        (*renderer)->hdrColorTarget().readback(images[path].data(), images[path].size() * 2);
        depthReadback.buffer->readback(depth.data(), depth.size() * sizeof(float));
        ++path;
    }
    REQUIRE(mirror.counters.droppedPerCluster > 0);
    REQUIRE((*renderer)->lightingStatus().counters.truncatedFroxels ==
            mirror.counters.truncatedFroxels);
    uint32_t darkened = 0, untruncatedLit = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t pixel = size_t{y} * width + x;
            if (depth[pixel] == 0)
                continue;
            const auto tile = clusterTile({x, y}, {0, 0}, {width, height});
            const auto slice = clusterSlice(depth[pixel], slices);
            const auto& record =
                mirror.grid[(slice * kClusterTilesY + tile.y) * kClusterTilesX + tile.x];
            const bool truncated = (record.count & kClusterTruncatedBit) != 0;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                const size_t index = pixel * 4 + channel;
                if (!truncated) {
                    REQUIRE(images[0][index] == images[1][index]);
                    untruncatedLit += images[0][index] != 0;
                } else {
                    REQUIRE(images[1][index] <= images[0][index]);
                    darkened += images[1][index] < images[0][index];
                }
            }
        }
    }
    REQUIRE(darkened > 0);
    REQUIRE(untruncatedLit > 0);
}
