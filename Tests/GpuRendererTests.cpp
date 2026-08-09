#include "GpuTestSupport.h"

namespace {

using lmx::render::Camera;
using lmx::render::DirectionalLight;
using lmx::render::DrawItem;
using lmx::render::Material;
using lmx::render::Mesh;
using lmx::render::Renderer;
using lmx::render::SceneView;

constexpr std::array<float, 4> kSceneClear = {0.05f, 0.07f, 0.10f, 1.0f};

constexpr float kCubeOffsetX = 1.2f;

//======================================================================================================================
Camera sceneCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    return camera;
}

//======================================================================================================================
SceneView litSceneView(std::span<const DrawItem> items) {
    SceneView view;
    view.items = items;
    view.lights[0] = {.strength = {0.5f, 0.5f, 0.5f}, .direction = {0.0f, 0.0f, -1.0f}};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    return view;
}

//======================================================================================================================
bool isClearChannel(uint8_t actual, float expected) {
    const int want = static_cast<int>(expected * 255.0f + 0.5f);
    return std::abs(int{actual} - want) <= 2;
}

//======================================================================================================================
std::array<DrawItem, 2> twoCubeScene(const Mesh& cube) {
    return {{
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{-kCubeOffsetX, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}},
        {.mesh = &cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{kCubeOffsetX, 0.0f, 0.0f}),
         .material = {.albedo = {0.0f, 0.0f, 1.0f, 1.0f}}},
    }};
}

//======================================================================================================================
void requireTwoCubeImage(const std::vector<uint8_t>& pixels, const char* label) {
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe(label, 2, 2, corner));
    REQUIRE(isClearChannel(corner.b, kSceneClear[2]));
    REQUIRE(isClearChannel(corner.g, kSceneClear[1]));
    REQUIRE(isClearChannel(corner.r, kSceneClear[0]));
    REQUIRE(corner.a == 255);

    const Pixel left = pixelAt(pixels, 16, 32);
    INFO(describe("left cube", 16, 32, left));
    REQUIRE(left.r > 64);
    REQUIRE(left.r > left.g + 32);
    REQUIRE(left.r > left.b + 32);

    const Pixel right = pixelAt(pixels, 48, 32);
    INFO(describe("right cube", 48, 32, right));
    REQUIRE(right.b > 64);
    REQUIRE(right.b > right.r + 32);
    REQUIRE(right.b > right.g + 32);
}

} // namespace

//======================================================================================================================
// Separated red and blue cubes catch argument-table last-write reuse across per-draw uniforms.
TEST_CASE("renderer draws per-object uniforms in one pass", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.cube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 2> items = twoCubeScene(*cube);

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), litSceneView(items), /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    requireTwoCubeImage(pixels, "clear");
}

//======================================================================================================================
// Overlapping geometry must resolve by depth rather than submission order.
TEST_CASE("renderer depth test beats draw order", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane = lmx::render::createMesh(**device, lmx::render::makePlane(1.0f), "lmx.test.plane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const auto upright = [](float z) {
        return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
               glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    };
    const DrawItem nearGreen{
        .mesh = &*plane, .model = upright(1.0f), .material = {.albedo = {0.0f, 1.0f, 0.0f, 1.0f}}};
    const DrawItem farRed{
        .mesh = &*plane, .model = upright(-1.0f), .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}};

    const std::array<std::array<DrawItem, 2>, 2> orderings = {{
        {{farRed, nearGreen}}, // back-to-front: passes even without a depth buffer
        {{nearGreen, farRed}}, // front-to-back: only depth can keep green on top
    }};
    const std::array<const char*, 2> names = {"far-then-near", "near-then-far"};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (size_t i = 0; i < orderings.size(); ++i) {
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), litSceneView(orderings[i]),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

        INFO(std::string("ordering: ") + names[i]);

        const Pixel center = pixelAt(pixels, 32, 32);
        INFO(describe("center", 32, 32, center));
        REQUIRE(center.g > 64);
        REQUIRE(center.g > center.r + 32);
        REQUIRE(center.g > center.b + 32);

        const Pixel corner = pixelAt(pixels, 2, 2);
        INFO(describe("corner", 2, 2, corner));
        REQUIRE(isClearChannel(corner.b, kSceneClear[2]));
        REQUIRE(isClearChannel(corner.g, kSceneClear[1]));
        REQUIRE(isClearChannel(corner.r, kSceneClear[0]));
    }
}

//======================================================================================================================
// A magenta destination makes a missing draw visible while the copied image pins the pass barrier.
TEST_CASE("renderer scene survives a barrier into a sampling pass", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.barrierCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = (*device)->createTexture({.width = kSize,
                                                 .height = kSize,
                                                 .format = Format::BGRA8Unorm,
                                                 .renderTarget = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.barrierDestination"});
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto copyPipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.fullscreenSamplePipeline"});
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    constexpr uint32_t kSourceTextureSlot = 0;

    CommandList& commands = (*device)->beginFrame();
    const std::array<DrawItem, 2> items = twoCubeScene(*cube);
    (*renderer)->render(commands, sceneCamera(), litSceneView(items), /*barrierForSampling=*/true);

    commands.beginRenderPass({.colorTarget = destination->get(),
                              .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                              .clear = true,
                              .label = "lmx.test.rendererCopy"});
    commands.bindPipeline(**copyPipeline);
    commands.bindTexture(kSourceTextureSlot, (*renderer)->colorTarget());
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());

    requireTwoCubeImage(pixels, "copied clear");
}

namespace {

constexpr uint32_t kShadowMapSize = 256;

constexpr uint32_t kShadowVertexSlot = 0;
constexpr uint32_t kShadowPassSlot = 2;
constexpr uint32_t kShadowMapSlot = 3;
constexpr uint32_t kShadowSamplerSlot = 1;

constexpr int32_t kFilterPcf = 0;
constexpr int32_t kFilterPcss = 1;

struct ShadowPassUniforms {
    glm::mat4 lightViewProj;
    glm::mat4 shadowTransform;
    int32_t filter = 0;
    int32_t pad[3] = {0, 0, 0};
};
static_assert(sizeof(ShadowPassUniforms) == 144, "must match ShadowSmoke.slang's PassUniforms");

struct ShadowVertex {
    float x = 0.f, y = 0.f, z = 0.f;
};
static_assert(sizeof(ShadowVertex) == 12, "must match Slang's packed_float3 Vertex layout");

//======================================================================================================================
std::array<ShadowVertex, 6> shadowQuad(float x0, float x1, float y0, float y1, float z) {
    return {{
        {x0, y0, z},
        {x1, y0, z},
        {x1, y1, z},
        {x0, y0, z},
        {x1, y1, z},
        {x0, y1, z},
    }};
}

//======================================================================================================================
Pixel shadowPixelAt(const std::vector<uint8_t>& bgra, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * kShadowMapSize + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

//======================================================================================================================
size_t differingTexels(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t differing = 0;
    for (uint32_t y = 0; y < kShadowMapSize; ++y) {
        for (uint32_t x = 0; x < kShadowMapSize; ++x) {
            const int lhs = shadowPixelAt(a, x, y).r;
            const int rhs = shadowPixelAt(b, x, y).r;
            if (std::abs(lhs - rhs) > 8) {
                ++differing;
            }
        }
    }
    return differing;
}

} // namespace

//======================================================================================================================
// Asymmetric receiver probes catch Y mirroring; broad image disagreement proves both PCF and PCSS
// paths run.
TEST_CASE("shadow filters resolve a quad's shadow on a plane", "[gpu]") {
    using namespace lmx::rhi;

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
                                                   .compare = CompareFunc::LessEqual,
                                                   .label = "lmx.test.shadowCompareSampler"});
    INFO(errorOf(shadowSampler));
    REQUIRE(shadowSampler.has_value());

    const glm::mat4 lightView =
        glm::lookAtRH(glm::vec3{0.0f, 0.0f, 2.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 lightProj = glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 3.0f);
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
                              .clearDepth = 1.0f,
                              .storeDepth = true,
                              .label = "lmx.test.shadowSmoke.map"});
    commands.bindPipeline(**depthPipeline);
    commands.setUniforms(kShadowPassSlot, &uniforms, sizeof(uniforms));
    commands.setUniforms(kShadowVertexSlot, occluder.data(), sizeof(occluder));
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
        commands.setUniforms(kShadowPassSlot, &uniforms, sizeof(uniforms));
        commands.setUniforms(kShadowVertexSlot, receiver.data(), sizeof(receiver));
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

namespace {

constexpr uint32_t kSceneProbeSize = 128;

//======================================================================================================================
Pixel pixelAtWidth(const std::vector<uint8_t>& bgra, uint32_t width, uint32_t x, uint32_t y) {
    const size_t offset = (size_t{y} * width + x) * 4;
    return {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
}

struct PixelCoord {
    uint32_t x = 0, y = 0;
};

//======================================================================================================================
PixelCoord projectToPixel(const Camera& camera, uint32_t size, const glm::vec3& world) {
    const glm::vec4 clip =
        camera.projectionMatrix(1.0f) * camera.viewMatrix() * glm::vec4(world, 1.0f);
    REQUIRE(clip.w > 0.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    INFO("ndc (" + std::to_string(ndc.x) + ", " + std::to_string(ndc.y) + ")");
    REQUIRE(std::abs(ndc.x) < 1.0f);
    REQUIRE(std::abs(ndc.y) < 1.0f);
    return {static_cast<uint32_t>((ndc.x * 0.5f + 0.5f) * static_cast<float>(size)),
            static_cast<uint32_t>((0.5f - ndc.y * 0.5f) * static_cast<float>(size))};
}

//======================================================================================================================
lmx::render::Vertex clipVertex(float x, float y, float z) {
    return {x, y, z, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
}

} // namespace

//======================================================================================================================
// Projected world-space probes follow the expected shadow and a distant lit patch through the real
// renderer.
TEST_CASE("renderer shadows a floating cube onto the ground", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto ground =
        lmx::render::createMesh(**device, lmx::render::makePlane(8.0f), "lmx.test.shadowGround");
    INFO(errorOf(ground));
    REQUIRE(ground.has_value());
    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.shadowCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSceneProbeSize, kSceneProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 2> items = {{
        {.mesh = &*ground,
         .model = glm::mat4{1.0f},
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
        {.mesh = &*cube,
         .model = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f}) *
                  glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    const glm::vec3 lightDir = glm::normalize(glm::vec3{1.0f, -1.0f, 1.0f});
    SceneView view;
    view.items = items;
    view.lights[0] = {.strength = {0.8f, 0.8f, 0.8f}, .direction = lightDir};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.ambient = {0.05f, 0.05f, 0.05f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 12.0f};

    Camera camera;
    camera.position = {0.0f, 12.0f, 16.0f};
    camera.pitch = -0.62f;

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
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

    REQUIRE(channelNear(lit.r, 189, 6));
    REQUIRE(channelNear(lit.g, 189, 6));
    REQUIRE(channelNear(lit.b, 189, 6));

    REQUIRE(shadowed.r * 4 < lit.r * 3);
    REQUIRE(shadowed.g * 4 < lit.g * 3);
    REQUIRE(shadowed.b * 4 < lit.b * 3);
    REQUIRE(shadowed.r > 30);

    const PixelCoord edgeAt = projectToPixel(camera, kSceneProbeSize, {4.0f, 0.0f, 4.0f});
    const Pixel edge = pixelAtWidth(pixels, kSceneProbeSize, edgeAt.x, edgeAt.y);
    INFO(describe("near the shadow's far corner", edgeAt.x, edgeAt.y, edge));
    REQUIRE(edge.r * 4 < lit.r * 3);
}

//======================================================================================================================
// A sloped ramp exposes depth bias; a flat control keeps unrelated depth behavior pinned.
TEST_CASE("depth bias offsets a sloped polygon and leaves a flat one alone", "[gpu]") {
    using namespace lmx::rhi;

    constexpr uint32_t kObjectSlot = 1;
    constexpr uint32_t kDepthTextureSlot = 0;
    constexpr uint32_t kSamplerSlot = 0;

    const std::array<lmx::render::Vertex, 12> quads = {{
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
                                                  .depthBias = bias,
                                                  .label = label});
    };
    auto unbiasedPipeline = makeDepthPipeline({}, "lmx.test.unbiasedDepthPipeline");
    INFO(errorOf(unbiasedPipeline));
    REQUIRE(unbiasedPipeline.has_value());
    auto biasedPipeline =
        makeDepthPipeline({.constant = 4.0f, .slopeScale = 1.0f}, "lmx.test.biasedDepthPipeline");
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

    CommandList& commands = (*device)->beginFrame();
    const auto depthPass = [&](Texture& target, GraphicsPipeline& pipeline) {
        commands.beginRenderPass({.depthTarget = &target,
                                  .clearDepth = 1.0f,
                                  .storeDepth = true,
                                  .label = "lmx.test.depthBias.write"});
        commands.bindPipeline(pipeline);
        commands.setUniforms(kObjectSlot, &identity, sizeof(identity));
        commands.setUniforms(kVertexBufferSlot, quads.data(), sizeof(quads));
        commands.draw(static_cast<uint32_t>(quads.size()));
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
    REQUIRE(channelNear(rampUnbiased.r, 116, 6));
    REQUIRE(int{rampBiased.r} - int{rampUnbiased.r} > 12);

    const Pixel flatUnbiased = pixelAt(unbiased, 32, 60);
    const Pixel flatBiased = pixelAt(biased, 32, 60);
    INFO(describe("flat, unbiased", 32, 60, flatUnbiased));
    INFO(describe("flat, biased", 32, 60, flatBiased));
    REQUIRE(channelNear(flatUnbiased.r, 128, 3));
    REQUIRE(std::abs(int{flatBiased.r} - int{flatUnbiased.r}) <= 1);
}

//======================================================================================================================
// Linear 0.5 should read back near sRGB byte 188, separating shader encode from raw storage.
TEST_CASE("the scene pass encodes its linear output to sRGB", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(2.0f), "lmx.test.encodePlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {{
        {.mesh = &*plane,
         .model = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    SceneView view;
    view.items = items;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.ambient = {0.5f, 0.5f, 0.5f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const Pixel probe = pixelAt(pixels, 32, 32);
    INFO(describe("ambient-only white", 32, 32, probe));
    REQUIRE(channelNear(probe.r, 188, 6));
    REQUIRE(channelNear(probe.g, 188, 6));
    REQUIRE(channelNear(probe.b, 188, 6));
    REQUIRE(probe.a == 255);
}

//======================================================================================================================
// An interior probe and total coverage bound distinguish wireframe from empty and solid output.
TEST_CASE("a wireframe SceneView leaves the interior of a face unfilled", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.wireCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {{
        {.mesh = &*cube,
         .model = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto renderWith = [&](bool wireframe) {
        SceneView view = litSceneView(items);
        view.wireframe = wireframe;
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, sceneCamera(), view, /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    };

    renderWith(false);
    const Pixel solid = pixelAt(pixels, 26, 26);
    INFO(describe("solid interior", 26, 26, solid));
    REQUIRE(solid.r > 128);
    REQUIRE(solid.g > 128);
    REQUIRE(solid.b > 128);

    renderWith(true);
    const Pixel wire = pixelAt(pixels, 26, 26);
    INFO(describe("wireframe interior", 26, 26, wire));
    REQUIRE(isClearChannel(wire.b, kSceneClear[2]));
    REQUIRE(isClearChannel(wire.g, kSceneClear[1]));
    REQUIRE(isClearChannel(wire.r, kSceneClear[0]));
    REQUIRE(wire.a == 255);
}

//======================================================================================================================
// The corner pins one sRGB encode of the sky; a cube probe proves depth keeps geometry in front.
TEST_CASE("the sky pass fills the background behind the scene", "[gpu]") {
    using namespace lmx::rhi;

    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const TextureMip skyMip{.data = kSkyTexel.data(), .bytesPerRow = 4};
    const std::array<TextureMip, 6> skyFaces = {skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.skySceneCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto skySphere = lmx::render::createMesh(
        **device, lmx::render::fromGeo(lmx::engine::makeSphere(0.5f, 20, 20)),
        "lmx.test.skySphere");
    INFO(errorOf(skySphere));
    REQUIRE(skySphere.has_value());

    auto skyCubemap = (*device)->createTexture({.width = 1,
                                                .height = 1,
                                                .format = Format::RGBA8Unorm,
                                                .kind = TextureKind::Cube,
                                                .sampled = true,
                                                .label = "lmx.test.skyCubemap"},
                                               skyFaces);
    INFO(errorOf(skyCubemap));
    REQUIRE(skyCubemap.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 2> items = twoCubeScene(*cube);
    SceneView view = litSceneView(items);
    view.skySphere = &*skySphere;
    view.skyCubemap = skyCubemap->get();

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("sky corner", 2, 2, corner));
    REQUIRE(channelNear(corner.r, 0, 4));
    REQUIRE(channelNear(corner.g, 188, 6));
    REQUIRE(channelNear(corner.b, 255, 4));
    REQUIRE(corner.a == 255);

    const Pixel right = pixelAt(pixels, 48, 32);
    INFO(describe("blue cube under the sky", 48, 32, right));
    REQUIRE(right.b > 64);
    REQUIRE(right.g < 188 - 6);
}

//======================================================================================================================
// The editor reads these labels straight out of the device, so the frame's passes have to arrive
// named and in the order the graph ran them -- an unnamed or missing pass is an invisible pass.
TEST_CASE("pass timings name every pass the graph ran", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.timedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 2> items = twoCubeScene(*cube);

    CommandList& commands = (*device)->beginFrame();
    (*renderer)->render(commands, sceneCamera(), litSceneView(items), /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    // waitIdle retires the measured frame; the next beginFrame is what publishes its counters.
    (*device)->beginFrame();
    (*device)->endFrame(nullptr);

    const std::span<const PassTiming> timings = (*device)->passTimings();
    REQUIRE(timings.size() == 2);
    REQUIRE(timings[0].label == "lmx.pass.shadow");
    REQUIRE(timings[1].label == "lmx.pass.scene");
    for (const PassTiming& timing : timings) {
        INFO(timing.label + ": " + std::to_string(timing.gpuMilliseconds) + " ms");
        REQUIRE(timing.gpuMilliseconds > 0.0);
    }
}

//======================================================================================================================
// The editor's frame shape: the scene passes plus one joined pass that reads what they rendered.
// Nothing here places a barrier -- the read declaration is the only thing standing between the
// scene pass's writes and this pass's sample, so a correct image is what proves the graph derived
// the transition.
TEST_CASE("a joined pass samples the scene colour the graph rendered", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.joinedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = makeProbeTarget(**device, "lmx.test.joinedDestination");
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto copyPipeline = (*device)->createGraphicsPipeline({.library = library->get(),
                                                           .vertexEntry = "vertexMain",
                                                           .fragmentEntry = "fragmentMain",
                                                           .colorFormat = Format::BGRA8Unorm,
                                                           .label = "lmx.test.joinedCopyPipeline"});
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    constexpr uint32_t kSourceTextureSlot = 0;

    const std::array<DrawItem, 2> items = twoCubeScene(*cube);
    const SceneView view = litSceneView(items);

    CommandList& commands = (*device)->beginFrame();
    lmx::render::RenderGraph graph;
    const lmx::render::GraphTexture sceneColor =
        (*renderer)->declarePasses(graph, commands, sceneCamera(), view);
    const lmx::render::GraphTexture copyTarget =
        graph.importTexture(**destination, Format::BGRA8Unorm, "destination");

    lmx::render::PassDesc copy;
    copy.textureReads.push_back(sceneColor);
    // Magenta makes a pass that drew nothing at all obvious rather than merely wrong.
    copy.color =
        lmx::render::ColorAttachment{.handle = copyTarget, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
    graph.addPass("lmx.pass.copy", std::move(copy),
                  [&](const lmx::render::PassResources& resources) {
                      const auto source = resources.texture(sceneColor);
                      REQUIRE(source.has_value());
                      commands.bindPipeline(**copyPipeline);
                      commands.bindTexture(kSourceTextureSlot, **source);
                      commands.draw(3);
                  });
    graph.exportTexture(lmx::render::nextVersion(copyTarget));
    graph.execute(commands);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());
    requireTwoCubeImage(pixels, "clear sampled through the graph");
}

//======================================================================================================================
// Validation has to be live in the path the frame actually takes, not only in a unit test of the
// declaration layer: a pass body reaching for a resource it never declared is refused mid-frame,
// with the pass and the resource named.
TEST_CASE("a pass resolving an undeclared texture is refused while the frame runs", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto declaredTarget = makeProbeTarget(**device, "lmx.test.declaredTarget");
    INFO(errorOf(declaredTarget));
    REQUIRE(declaredTarget.has_value());
    auto undeclaredTarget = makeProbeTarget(**device, "lmx.test.undeclaredTarget");
    INFO(errorOf(undeclaredTarget));
    REQUIRE(undeclaredTarget.has_value());

    lmx::render::RenderGraph graph;
    const lmx::render::GraphTexture declared =
        graph.importTexture(**declaredTarget, Format::BGRA8Unorm, "declaredTarget");
    const lmx::render::GraphTexture undeclared =
        graph.importTexture(**undeclaredTarget, Format::BGRA8Unorm, "undeclaredTarget");

    std::optional<lmx::render::GraphError> refusal;
    lmx::render::PassDesc probe;
    probe.color = lmx::render::ColorAttachment{.handle = declared};
    graph.addPass("lmx.pass.probe", std::move(probe),
                  [&](const lmx::render::PassResources& resources) {
                      const auto texture = resources.texture(undeclared);
                      if (!texture) {
                          refusal = texture.error();
                      }
                  });

    CommandList& commands = (*device)->beginFrame();
    graph.execute(commands);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    REQUIRE(refusal.has_value());
    INFO(refusal->message);
    REQUIRE(refusal->message.contains("lmx.pass.probe"));
    REQUIRE(refusal->message.contains("undeclaredTarget"));
    REQUIRE(refusal->message.contains("did not declare"));
}
