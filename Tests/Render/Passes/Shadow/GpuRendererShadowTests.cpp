#include "Render/Common/FrameData.h"
#include "Render/Passes/Shadow/ShadowRoots.h"
#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include <cstring>
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Projected world-space probes follow the expected shadow and a distant lit patch through the real
// renderer.
TEST_CASE("renderer shadows a floating cube onto the ground", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto ground = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(8.0f),
                                         "lmx.test.shadowGround");
    INFO(errorOf(ground));
    REQUIRE(ground.has_value());
    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.shadowCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSceneProbeSize, kSceneProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = {{
        {.mesh = &*ground,
         .model = glm::mat4{1.0f},
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
        {.mesh = &*cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f}) *
                  glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    const glm::vec3 lightDir = glm::normalize(glm::vec3{1.0f, -1.0f, 1.0f});
    FixtureSceneView view;
    view.items = items;
    view.lights[0] = {.strength = {0.8f, 0.8f, 0.8f}, .direction = lightDir};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 12.0f};

    Camera camera;
    camera.position = {0.0f, 12.0f, 16.0f};
    camera.pitch = -0.62f;

    auto gpuFrame2 = gpu->beginFrame();
    auto& commands = gpuFrame2.commands;
    (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, *gpu));
    gpu->submitAndEndFrame(gpuFrame2);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSceneProbeSize} * kSceneProbeSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, (*renderer)->colorTarget(),
                                                            (*renderer)->textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }

    const PixelCoord shadowAt = projectToPixel(camera, kSceneProbeSize, {3.0f, 0.0f, 3.0f});
    const Pixel shadowed = pixelAtWidth(pixels, kSceneProbeSize, shadowAt.x, shadowAt.y);
    INFO(describe("under the cube's shadow", shadowAt.x, shadowAt.y, shadowed));

    const PixelCoord litAt = projectToPixel(camera, kSceneProbeSize, {-5.0f, 0.0f, -5.0f});
    const Pixel lit = pixelAtWidth(pixels, kSceneProbeSize, litAt.x, litAt.y);
    INFO(describe("clear of the shadow", litAt.x, litAt.y, lit));

    // Re-derived for the GGX model rather than carried over: under Blinn-Phong this probe read
    // byte 189, which was albedo * lightStrength with no 1/pi and an ambient floor underneath it.
    // The lit ground is now a white dielectric at the renderer's default roughness, lit by one
    // directional light, with no environment bound -- so its radiance is exactly what the CPU
    // mirror of the same BRDF returns for this geometry, pushed through the display transform.
    const glm::vec3 groundPoint{-5.0f, 0.0f, -5.0f};
    const glm::vec3 litRadiance = lmx::test::brdf::directionalLight(
        view.lights[0].strength, view.lights[0].direction, glm::vec3{0.0f, 1.0f, 0.0f},
        glm::normalize(camera.position - groundPoint), {.baseColor = glm::vec3(1.0f)});
    const std::array<int, 3> litBytes = lmx::test::displayBytes(litRadiance);
    INFO("expected lit bytes " << litBytes[0] << ", " << litBytes[1] << ", " << litBytes[2]);
    REQUIRE(channelNear(lit.r, litBytes[0], 4));
    REQUIRE(channelNear(lit.g, litBytes[1], 4));
    REQUIRE(channelNear(lit.b, litBytes[2], 4));

    REQUIRE(shadowed.r * 4 < lit.r * 3);
    REQUIRE(shadowed.g * 4 < lit.g * 3);
    REQUIRE(shadowed.b * 4 < lit.b * 3);
    // No lower bound on the shadowed probe any more: the ambient floor it used to guard is gone,
    // and this view binds no IBL, so a fully occluded fragment's only radiance is whatever partial
    // PCF coverage lets through. What the environment now contributes in shadow is measured by the
    // furnace case below, where it is the entire signal.

    const PixelCoord edgeAt = projectToPixel(camera, kSceneProbeSize, {4.0f, 0.0f, 4.0f});
    const Pixel edge = pixelAtWidth(pixels, kSceneProbeSize, edgeAt.x, edgeAt.y);
    INFO(describe("near the shadow's far corner", edgeAt.x, edgeAt.y, edge));
    REQUIRE(edge.r * 4 < lit.r * 3);
}

//======================================================================================================================
// A sloped ramp exposes depth bias; a flat control keeps unrelated depth behavior pinned.
//
// The bias is the renderer's own sign, negative, because depth is reversed: kShadowDepthBias in
// Renderer.cpp pushes a shadow caster's stored depth *away* from the light so the surface stops
// shadowing itself, and away from the light is now the smaller number. This case is the
// instrument that says so -- a bias that kept the conventional sign would move the ramp the other
// way and read here as a positive difference.
TEST_CASE("depth bias offsets a sloped polygon and leaves a flat one alone", "[gpu]") {
    using namespace rojoRHI;

    const std::array<lmx::engine::Vertex, 12> quads = {{
        clipVertex(-1.0f, -1.0f, 0.15f),
        clipVertex(1.0f, -1.0f, 0.15f),
        clipVertex(1.0f, -0.75f, 0.85f),
        clipVertex(-1.0f, -1.0f, 0.15f),
        clipVertex(1.0f, -0.75f, 0.85f),
        clipVertex(-1.0f, -0.75f, 0.85f),
        clipVertex(-1.0f, 0.75f, 0.5f),
        clipVertex(1.0f, 0.75f, 0.5f),
        clipVertex(1.0f, 1.0f, 0.5f),
        clipVertex(-1.0f, 0.75f, 0.5f),
        clipVertex(1.0f, 1.0f, 0.5f),
        clipVertex(-1.0f, 1.0f, 0.5f),
    }};
    const glm::mat4 identity{1.0f};

    auto gpu = createTestGpu();

    REQUIRE(gpu != nullptr);

    auto shadowLibrary = gpu->loadShaderLibrary("ShadowPass");
    INFO(errorOf(shadowLibrary));
    REQUIRE(shadowLibrary.has_value());

    const auto makeDepthPipeline = [&](bool bias, const char* label) {
        return lmx::engine::adoptGpu(
            gpu->device(),
            gpu->device().createPipeline({.vertex = {shadowLibrary->get(), "vertexMain"},
                                          .fragment = {shadowLibrary->get(), "fragmentMain"},
                                          .raster = {.cull = CullMode::None, .depthBias = bias},
                                          .depthStencil = {.depthTest = true,
                                                           .depthWrite = true,
                                                           .depthCompare = CompareOp::Greater},
                                          .depthFormat = Format::D32Float,
                                          .label = label}));
    };
    auto unbiasedPipeline = makeDepthPipeline(false, "lmx.test.unbiasedDepthPipeline");
    INFO(errorOf(unbiasedPipeline));
    REQUIRE(unbiasedPipeline.has_value());
    auto biasedPipeline = makeDepthPipeline(true, "lmx.test.biasedDepthPipeline");
    INFO(errorOf(biasedPipeline));
    REQUIRE(biasedPipeline.has_value());

    const auto makeDepthTarget = [&](const char* label) {
        return lmx::engine::GpuTexture::create(
            gpu->device(),
            {.format = Format::D32Float,
             .extent = {kSize, kSize, 1},
             .usage = rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled,
             .label = label});
    };
    auto unbiasedDepth = makeDepthTarget("lmx.test.unbiasedDepth");
    INFO(errorOf(unbiasedDepth));
    REQUIRE(unbiasedDepth.has_value());
    auto biasedDepth = makeDepthTarget("lmx.test.biasedDepth");
    INFO(errorOf(biasedDepth));
    REQUIRE(biasedDepth.has_value());

    auto unbiasedImage = makeProbeTarget(*gpu, "lmx.test.unbiasedDepthImage");
    INFO(errorOf(unbiasedImage));
    REQUIRE(unbiasedImage.has_value());
    auto biasedImage = makeProbeTarget(*gpu, "lmx.test.biasedDepthImage");
    INFO(errorOf(biasedImage));
    REQUIRE(biasedImage.has_value());

    auto sampleLibrary = gpu->loadShaderLibrary("SamplerSmoke");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());
    auto samplePipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline(
            {.vertex = {sampleLibrary->get(), "vertexMain"},
             .fragment = {sampleLibrary->get(), "fragmentIdentityUv"},
             .raster = {.cull = rojoRHI::CullMode::Back},
             .depthStencil = {.depthTest = false,
                              .depthWrite = false,
                              .depthCompare = rojoRHI::CompareOp::Less},
             .colors = std::array{rojoRHI::ColorTarget{.format = Format::BGRA8Unorm}},
             .label = "lmx.test.depthBiasSamplePipeline"}));
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto sampler = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createSampler({.filter = SamplerFilter::Nearest,
                                                    .address = SamplerAddress::ClampToEdge,
                                                    .label = "lmx.test.depthBiasSampler"}));
    INFO(errorOf(sampler));
    REQUIRE(sampler.has_value());

    lmx::engine::Scene scene;
    lmx::engine::MeshData geometry;
    geometry.vertices.assign(quads.begin(), quads.end());
    for (uint32_t i = 0; i < quads.size(); ++i) {
        geometry.indices.push_back(i);
    }
    const auto mesh = scene.addMesh(std::move(geometry), "lmx.test.depthBias.geometry");
    const auto material = scene.addMaterial({});
    const auto instance = scene.addObject({.mesh = mesh, .material = material});
    REQUIRE(scene.finalize(gpu->device()));

    auto gpuFrame3 = gpu->beginFrame();
    auto& commands = gpuFrame3.commands;
    REQUIRE(scene.prepareFrame(gpu->frameNumber()));
    const auto tables = scene.tables();
    const auto meshRow = *scene.tryMesh(mesh);
    auto visibleRows = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = sizeof(uint32_t), .label = "lmx.test.depthBias.visibleRows"},
        std::as_bytes(std::span(&instance.slot, 1)));
    REQUIRE(visibleRows.has_value());
    const lmx::render::ShadowPassRoot passRoot{
        .vertices = tables.vertices->address(),
        .visibleRows = visibleRows->address(),
        .instances = tables.instances->address(),
        .materials = tables.materials->address(),
        .uniforms = lmx::render::frameData(commands, lmx::render::ShadowPassUniforms{identity})};
    const lmx::render::ShadowDrawRoot drawRoot{.pass = lmx::render::frameData(commands, passRoot)};
    const auto depthPass = [&](const lmx::engine::GpuTexture& target, Pipeline pipeline,
                               bool bias) {
        const TextureBarrier begin{.texture = target.rhi(),
                                   .range = {0, 1, 0, 1},
                                   .dstStage = Stage::DepthStencil,
                                   .dstAccess =
                                       Access::DepthStencilRead | Access::DepthStencilWrite,
                                   .newLayout = Layout::DepthStencilWrite};
        commands.barrier({}, {}, std::span(&begin, 1));
        commands.beginRenderPass({.depth = {.texture = target.rhi(),
                                            .load = LoadOp::Clear,
                                            .store = StoreOp::Store,
                                            .clearDepth = 0.0f},
                                  .area = {0, 0, kSize, kSize},
                                  .label = "lmx.test.depthBias.write"});
        commands.setViewport({.width = float(kSize), .height = float(kSize)});
        commands.setScissor({0, 0, kSize, kSize});
        commands.setPipeline(pipeline);
        if (bias)
            commands.setDepthBias(-4.0f, -1.0f, 0.0f);
        commands.pushRoot(drawRoot);
#ifndef NDEBUG
        const std::array<ResourceAccess, 3> accesses{
            {{.buffer = tables.vertices->rhi(),
              .size = tables.vertices->desc().size,
              .stage = Stage::VertexShader,
              .access = Access::ShaderRead},
             {.buffer = visibleRows->rhi(),
              .size = sizeof(uint32_t),
              .stage = Stage::VertexShader,
              .access = Access::ShaderRead},
             {.buffer = tables.instances->rhi(),
              .size = uint64_t{tables.instanceCount} * sizeof(lmx::engine::InstanceRow),
              .stage = Stage::VertexShader,
              .access = Access::ShaderRead}}};
        commands.declareAccess(accesses);
#endif
        commands.drawIndexed(tables.indices->rhi(), IndexType::UInt32, meshRow.indexCount, 1,
                             meshRow.firstIndex, 0, 0);
        commands.endRenderPass();
    };
    depthPass(*unbiasedDepth, unbiasedPipeline->get(), false);
    depthPass(*biasedDepth, biasedPipeline->get(), true);
    const auto samplePass = [&](const lmx::engine::GpuTexture& source,
                                const lmx::engine::GpuTexture& destination) {
        const std::array<TextureBarrier, 2> barriers{
            {{.texture = source.rhi(),
              .range = {0, 1, 0, 1},
              .srcStage = Stage::DepthStencil,
              .srcAccess = Access::DepthStencilRead | Access::DepthStencilWrite,
              .dstStage = Stage::FragmentShader,
              .dstAccess = Access::ShaderRead,
              .oldLayout = Layout::DepthStencilWrite,
              .newLayout = Layout::DepthStencilRead},
             {.texture = destination.rhi(),
              .range = {0, 1, 0, 1},
              .dstStage = Stage::ColorAttachment,
              .dstAccess = Access::ColorAttachmentWrite,
              .newLayout = Layout::ColorAttachment}}};
        commands.barrier({}, {}, barriers);
        const ColorAttachment color{.texture = destination.rhi(),
                                    .load = LoadOp::Clear,
                                    .store = StoreOp::Store,
                                    .clear = {1.0f, 0.0f, 1.0f, 1.0f}};
        commands.beginRenderPass({.colors = std::span(&color, 1),
                                  .area = {0, 0, kSize, kSize},
                                  .label = "lmx.test.depthBias.sample"});
        commands.setViewport({.width = float(kSize), .height = float(kSize)});
        commands.setScissor({0, 0, kSize, kSize});
        commands.setPipeline(samplePipeline->get());
        commands.pushRoot(lmx::test::SamplerRoot{.source = static_cast<uint32_t>(source.view()),
                                                 .sampler = static_cast<uint32_t>(sampler->get())});
#ifndef NDEBUG
        const ResourceAccess access{.texture = source.rhi(),
                                    .range = {0, 1, 0, 1},
                                    .stage = Stage::FragmentShader,
                                    .access = Access::ShaderRead};
        commands.declareAccess(std::span(&access, 1));
#endif
        commands.draw(3);
        commands.endRenderPass();
    };
    samplePass(*unbiasedDepth, *unbiasedImage);
    samplePass(*biasedDepth, *biasedImage);

    gpu->submitAndEndFrame(gpuFrame3);
    gpu->waitIdle();

    std::vector<uint8_t> unbiased(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *unbiasedImage,
            std::array{lmx::render::TextureRangeState{.range = {0, 1, 0, 1},
                                                      .state = {Stage::ColorAttachment,
                                                                Access::ColorAttachmentWrite,
                                                                Layout::ColorAttachment}}});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(unbiased.data(), readbackBytes->data(), unbiased.size());
    }
    std::vector<uint8_t> biased(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *biasedImage,
            std::array{lmx::render::TextureRangeState{.range = {0, 1, 0, 1},
                                                      .state = {Stage::ColorAttachment,
                                                                Access::ColorAttachmentWrite,
                                                                Layout::ColorAttachment}}});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(biased.data(), readbackBytes->data(), biased.size());
    }

    const Pixel rampUnbiased = pixelAt(unbiased, 32, 3);
    const Pixel rampBiased = pixelAt(biased, 32, 3);
    INFO(describe("ramp, unbiased", 32, 3, rampUnbiased));
    INFO(describe("ramp, biased", 32, 3, rampBiased));
    // The unbiased reading is the vertex depth this probe interpolates and is unchanged by the
    // convention flip: the quads are given in clip space, so reversing what the *projection*
    // emits does not move them. Only the bias direction changed, and the assertion below is the
    // previous one with its operands swapped.
    REQUIRE(channelNear(rampUnbiased.r, 116, 6));
    REQUIRE(int{rampUnbiased.r} - int{rampBiased.r} > 12);

    const Pixel flatUnbiased = pixelAt(unbiased, 32, 60);
    const Pixel flatBiased = pixelAt(biased, 32, 60);
    INFO(describe("flat, unbiased", 32, 60, flatUnbiased));
    INFO(describe("flat, biased", 32, 60, flatBiased));
    REQUIRE(channelNear(flatUnbiased.r, 128, 3));
    REQUIRE(std::abs(int{flatBiased.r} - int{flatUnbiased.r}) <= 1);
}

//======================================================================================================================
TEST_CASE("renderer applies the chosen directional caster and leaves none or disabled unshadowed",
          "[gpu][scene-doc][composition][caster]") {
    using namespace lmx;
    auto gpu = createTestGpu();
    engine::Scene scene;
    const auto ground = scene.addMesh(engine::makePlane(8.0f), "caster.ground");
    const auto cube = scene.addMesh(engine::makeCube(), "caster.cube");
    const auto material =
        scene.addMaterial({.albedo = glm::vec4(1), .roughness = 0.5f, .metallic = 0.0f});
    scene.addObject({.mesh = ground, .material = material});
    scene.addObject(
        {.position = {0, 3, 0}, .scale = glm::vec3(2), .mesh = cube, .material = material});
    scene.boundingSphere = {0, 0, 0, 12};
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = render::Renderer::create(*gpu, kSceneProbeSize, kSceneProbeSize, true);
    REQUIRE(renderer);
    engine::Camera camera;
    camera.position = {0, 12, 16};
    camera.pitch = -0.62f;
    const auto probe = projectToPixel(camera, kSceneProbeSize, {3, 0, 3});
    const glm::vec3 direction = glm::normalize(glm::vec3{1, -1, 1});
    for (bool masked : {false, true}) {
        scene.material(material).alphaMode =
            masked ? engine::AlphaMode::Mask : engine::AlphaMode::Opaque;
        for (bool automatic : {false, true}) {
            CAPTURE(masked, automatic);
            const auto renderCase = [&](int caster, bool disabled) {
                for (auto& light : scene.lights)
                    light = {.strength = glm::vec3(0), .direction = {0, -1, 0}};
                const int contributor = caster < 0 || disabled ? 1 : caster;
                scene.lights[contributor] = {.strength = glm::vec3(0.8f), .direction = direction};
                scene.shadowCaster = caster < 0 ? std::optional<uint32_t>{} : caster;
                if (disabled)
                    scene.lights[caster] = {
                        .strength = glm::vec3(0.8f), .direction = direction, .enabled = false};
                auto gpuFrame4 = gpu->beginFrame();
                auto& commands = gpuFrame4.commands;
                REQUIRE(scene.prepareFrame(gpu->frameNumber()));
                std::vector<engine::DrawItem> items;
                auto view = render::buildSceneView(scene, items, false);
                view.bloomEnabled = false;
                view.autoExposureEnabled = automatic;
                view.exposureReset = true;
                view.exposureAdaptUpStopsPerSecond = 0;
                view.exposureAdaptDownStopsPerSecond = 0;
                (*renderer)->render(commands, camera, view);
                gpu->submitAndEndFrame(gpuFrame4);
                gpu->waitIdle();
                std::vector<uint8_t> pixels(size_t{kSceneProbeSize} * kSceneProbeSize * 4);
                {
                    const auto readbackBytes = lmx::render::readbackNow(
                        *gpu, (*renderer)->colorTarget(), (*renderer)->textureStates());
                    INFO(errorOf(readbackBytes));
                    REQUIRE(readbackBytes.has_value());
                    std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
                }
                return pixels;
            };
            const auto key = renderCase(0, false);
            REQUIRE(renderCase(1, false) == key);
            REQUIRE(renderCase(2, false) == key);
            const auto none = renderCase(-1, false);
            REQUIRE(renderCase(0, true) == none);
            const auto shadowed = pixelAtWidth(key, kSceneProbeSize, probe.x, probe.y);
            const auto unshadowed = pixelAtWidth(none, kSceneProbeSize, probe.x, probe.y);
            REQUIRE(shadowed.r * 4 < unshadowed.r * 3);
            REQUIRE(shadowed.g * 4 < unshadowed.g * 3);
            REQUIRE(shadowed.b * 4 < unshadowed.b * 3);
        }
    }
}
