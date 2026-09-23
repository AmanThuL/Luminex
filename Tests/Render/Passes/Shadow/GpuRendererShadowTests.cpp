#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Asymmetric receiver probes catch Y mirroring; broad image disagreement proves both PCF and PCSS
// paths run.
TEST_CASE("shadow filters resolve a quad's shadow on a plane", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto shadowMap = (*device)->createTexture({.width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .format = Format::D32Float,
                                               .renderTarget = true,
                                               .sampled = true,
                                               .label = "lmx.test.shadowMap"});
    INFO(errorOf(shadowMap));
    REQUIRE(shadowMap.has_value());

    const auto makeShadowDestination = [&](const char* label) {
        return (*device)->createTexture({.width = kShadowMapSize,
                                         .height = kShadowMapSize,
                                         .format = Format::BGRA8Unorm,
                                         .renderTarget = true,
                                         .cpuReadback = true,
                                         .label = label});
    };
    auto pcfDestination = makeShadowDestination("lmx.test.shadowPcfDestination");
    INFO(errorOf(pcfDestination));
    REQUIRE(pcfDestination.has_value());
    auto pcssDestination = makeShadowDestination("lmx.test.shadowPcssDestination");
    INFO(errorOf(pcssDestination));
    REQUIRE(pcssDestination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ShadowSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto depthPipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentDepthOnly",
                                           .colorFormat = Format::Unknown,
                                           .depthFormat = Format::D32Float,
                                           .depthTestEnable = true,
                                           .depthWriteEnable = true,
                                           // Reversed-Z: the map clears to 0 and the nearest
                                           // surface to the light is the largest depth.
                                           .depthCompare = DepthCompare::Greater,
                                           .label = "lmx.test.shadowDepthPipeline"});
    INFO(errorOf(depthPipeline));
    REQUIRE(depthPipeline.has_value());

    auto shadowPipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.shadowReadPipeline"});
    INFO(errorOf(shadowPipeline));
    REQUIRE(shadowPipeline.has_value());

    auto shadowSampler = (*device)->createSampler({.filter = FilterMode::Linear,
                                                   .addressMode = AddressMode::Clamp,
                                                   .maxAnisotropy = 16,
                                                   .compare = CompareFunc::GreaterEqual,
                                                   .label = "lmx.test.shadowCompareSampler"});
    INFO(errorOf(shadowSampler));
    REQUIRE(shadowSampler.has_value());

    const glm::mat4 lightView =
        glm::lookAtRH(glm::vec3{0.0f, 0.0f, 2.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    // Reversed, the same way fitShadowOrtho reverses: near and far handed to orthoRH_ZO the other
    // way round, so the light's near plane (distance 1) is depth 1 and its far plane (3) is 0.
    // The occluder quad sits at world z = 0.2, i.e. 1.8 from the light, and so writes
    // (3 - 1.8) / 2 = 0.6; the receiver plane at z = 0 is 2 away and reads 0.5. Nearer is larger.
    const glm::mat4 lightProj = glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 3.0f, 1.0f);
    const glm::mat4 lightViewProj = lightProj * lightView;

    glm::mat4 ndcToTexcoord{1.0f};
    ndcToTexcoord[0][0] = 0.5f;
    ndcToTexcoord[1][1] = -0.5f;
    ndcToTexcoord[3][0] = 0.5f;
    ndcToTexcoord[3][1] = 0.5f;

    ShadowPassUniforms uniforms{.lightViewProj = lightViewProj,
                                .shadowTransform = ndcToTexcoord * lightViewProj};

    const std::array<ShadowVertex, 6> receiver = shadowQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f);
    const std::array<ShadowVertex, 6> occluder = shadowQuad(-0.5f, 0.5f, -0.25f, 0.75f, 0.2f);

    CommandList& commands = (*device)->beginFrame();

    commands.beginRenderPass({.depthTarget = shadowMap->get(),
                              .clearDepth = 0.0f,
                              .storeDepth = true,
                              .label = "lmx.test.shadowSmoke.map"});
    commands.bindPipeline(**depthPipeline);
    commands.bindFrameData(kShadowPassSlot, uniforms);
    commands.bindFrameData(kShadowVertexSlot, occluder.data(), sizeof(occluder));
    commands.draw(static_cast<uint32_t>(occluder.size()));
    commands.endRenderPass();

    commands.textureBarrier(**shadowMap, TextureUse::RenderTarget, TextureUse::ShaderRead);

    const auto receiverPass = [&](Texture& destination, int32_t filter) {
        uniforms.filter = filter;
        commands.beginRenderPass({.colorTarget = &destination,
                                  .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                                  .clear = true,
                                  .label = "lmx.test.shadowSmoke.receiver"});
        commands.bindPipeline(**shadowPipeline);
        commands.bindTexture(kShadowMapSlot, **shadowMap);
        commands.bindSampler(kShadowSamplerSlot, **shadowSampler);
        commands.bindFrameData(kShadowPassSlot, uniforms);
        commands.bindFrameData(kShadowVertexSlot, receiver.data(), sizeof(receiver));
        commands.draw(static_cast<uint32_t>(receiver.size()));
        commands.endRenderPass();
    };
    receiverPass(**pcfDestination, kFilterPcf);
    receiverPass(**pcssDestination, kFilterPcss);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pcf(size_t{kShadowMapSize} * kShadowMapSize * 4);
    (*pcfDestination)->readback(pcf.data(), pcf.size());
    std::vector<uint8_t> pcss(size_t{kShadowMapSize} * kShadowMapSize * 4);
    (*pcssDestination)->readback(pcss.data(), pcss.size());

    const auto requireProbes = [](const std::vector<uint8_t>& image, const char* filter) {
        INFO(std::string("filter: ") + filter);

        const Pixel shadowed = shadowPixelAt(image, 128, 96);
        INFO(describe("under the occluder", 128, 96, shadowed));
        REQUIRE(shadowed.r < 128);

        const Pixel litCorner = shadowPixelAt(image, 16, 240);
        INFO(describe("clear of the occluder", 16, 240, litCorner));
        REQUIRE(litCorner.r == 255);
        REQUIRE(litCorner.g == 255);

        const Pixel litBelow = shadowPixelAt(image, 128, 216);
        INFO(describe("below the occluder", 128, 216, litBelow));
        REQUIRE(litBelow.r == 255);
        REQUIRE(litBelow.g == 255);
    };
    requireProbes(pcf, "PCF");
    requireProbes(pcss, "PCSS");

    const size_t differing = differingTexels(pcf, pcss);
    INFO("texels where PCF and PCSS disagree: " + std::to_string(differing));
    REQUIRE(differing > 500);
}

//======================================================================================================================
// Projected world-space probes follow the expected shadow and a distant lit patch through the real
// renderer.
TEST_CASE("renderer shadows a floating cube onto the ground", "[gpu]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto ground =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(8.0f), "lmx.test.shadowGround");
    INFO(errorOf(ground));
    REQUIRE(ground.has_value());
    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.shadowCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSceneProbeSize, kSceneProbeSize,
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

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSceneProbeSize} * kSceneProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

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

    constexpr uint32_t kObjectSlot = 1;
    constexpr uint32_t kDepthTextureSlot = 0;
    constexpr uint32_t kSamplerSlot = 0;

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

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto shadowLibrary = (*device)->loadShaderLibrary("Shaders/ShadowPass");
    INFO(errorOf(shadowLibrary));
    REQUIRE(shadowLibrary.has_value());

    const auto makeDepthPipeline = [&](DepthBias bias, const char* label) {
        return (*device)->createGraphicsPipeline({.library = shadowLibrary->get(),
                                                  .vertexEntry = "vertexMain",
                                                  .fragmentEntry = "fragmentMain",
                                                  .colorFormat = Format::Unknown,
                                                  .depthFormat = Format::D32Float,
                                                  .depthTestEnable = true,
                                                  .depthWriteEnable = true,
                                                  .cullMode = CullMode::None,
                                                  .depthCompare = DepthCompare::Greater,
                                                  .depthBias = bias,
                                                  .label = label});
    };
    auto unbiasedPipeline = makeDepthPipeline({}, "lmx.test.unbiasedDepthPipeline");
    INFO(errorOf(unbiasedPipeline));
    REQUIRE(unbiasedPipeline.has_value());
    auto biasedPipeline =
        makeDepthPipeline({.constant = -4.0f, .slopeScale = -1.0f}, "lmx.test.biasedDepthPipeline");
    INFO(errorOf(biasedPipeline));
    REQUIRE(biasedPipeline.has_value());

    const auto makeDepthTarget = [&](const char* label) {
        return (*device)->createTexture({.width = kSize,
                                         .height = kSize,
                                         .format = Format::D32Float,
                                         .renderTarget = true,
                                         .sampled = true,
                                         .label = label});
    };
    auto unbiasedDepth = makeDepthTarget("lmx.test.unbiasedDepth");
    INFO(errorOf(unbiasedDepth));
    REQUIRE(unbiasedDepth.has_value());
    auto biasedDepth = makeDepthTarget("lmx.test.biasedDepth");
    INFO(errorOf(biasedDepth));
    REQUIRE(biasedDepth.has_value());

    auto unbiasedImage = makeProbeTarget(**device, "lmx.test.unbiasedDepthImage");
    INFO(errorOf(unbiasedImage));
    REQUIRE(unbiasedImage.has_value());
    auto biasedImage = makeProbeTarget(**device, "lmx.test.biasedDepthImage");
    INFO(errorOf(biasedImage));
    REQUIRE(biasedImage.has_value());

    auto sampleLibrary = (*device)->loadShaderLibrary("Shaders/SamplerSmoke");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());
    auto samplePipeline =
        (*device)->createGraphicsPipeline({.library = sampleLibrary->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentIdentityUv",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.depthBiasSamplePipeline"});
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto sampler = (*device)->createSampler({.filter = FilterMode::Nearest,
                                             .addressMode = AddressMode::Clamp,
                                             .label = "lmx.test.depthBiasSampler"});
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
    REQUIRE(scene.finalize(**device));

    CommandList& commands = (*device)->beginFrame();
    REQUIRE(scene.prepareFrame((*device)->frameNumber()));
    const auto tables = scene.tables();
    const auto meshRow = *scene.tryMesh(mesh);
    auto visibleRows = (*device)->createBuffer(
        {.size = sizeof(uint32_t), .storageRead = true, .label = "lmx.test.depthBias.visibleRows"},
        &instance.slot);
    REQUIRE(visibleRows.has_value());
    const auto depthPass = [&](Texture& target, GraphicsPipeline& pipeline) {
        commands.beginRenderPass({.depthTarget = &target,
                                  .clearDepth = 0.0f,
                                  .storeDepth = true,
                                  .label = "lmx.test.depthBias.write"});
        commands.bindPipeline(pipeline);
        commands.bindFrameData(kObjectSlot, lmx::engine::DrawUniforms{0});
        commands.bindBuffer(lmx::engine::kVisibleRowsSlot, **visibleRows);
        commands.bindFrameData(2, identity);
        commands.bindBuffer(kVertexBufferSlot, *tables.vertices);
        commands.bindBuffer(lmx::engine::kSceneInstancesSlot, *tables.instances);
        commands.bindBuffer(lmx::engine::kSceneMaterialsSlot, *tables.materials);
        commands.drawIndexed(*tables.indices, meshRow.indexCount, meshRow.firstIndex);
        commands.endRenderPass();
    };
    depthPass(**unbiasedDepth, **unbiasedPipeline);
    depthPass(**biasedDepth, **biasedPipeline);

    commands.textureBarrier(**unbiasedDepth, TextureUse::RenderTarget, TextureUse::ShaderRead);
    commands.textureBarrier(**biasedDepth, TextureUse::RenderTarget, TextureUse::ShaderRead);

    const auto samplePass = [&](Texture& source, Texture& destination) {
        commands.beginRenderPass({.colorTarget = &destination,
                                  .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                                  .clear = true,
                                  .label = "lmx.test.depthBias.sample"});
        commands.bindPipeline(**samplePipeline);
        commands.bindTexture(kDepthTextureSlot, source);
        commands.bindSampler(kSamplerSlot, **sampler);
        commands.draw(3);
        commands.endRenderPass();
    };
    samplePass(**unbiasedDepth, **unbiasedImage);
    samplePass(**biasedDepth, **biasedImage);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> unbiased(size_t{kSize} * kSize * 4);
    (*unbiasedImage)->readback(unbiased.data(), unbiased.size());
    std::vector<uint8_t> biased(size_t{kSize} * kSize * 4);
    (*biasedImage)->readback(biased.data(), biased.size());

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
