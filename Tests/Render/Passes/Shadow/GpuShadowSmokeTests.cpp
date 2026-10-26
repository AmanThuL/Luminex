#include "Support/GpuTestSupport.h"

#include "Render/Common/FrameData.h"

#include <fstream>

namespace {

constexpr uint32_t kShadowMapSize = 256;
constexpr int32_t kFilterPcf = 0;
constexpr int32_t kFilterPcss = 1;

struct ShadowVertex {
    float x = 0.f, y = 0.f, z = 0.f;
};
static_assert(sizeof(ShadowVertex) == 12);
static_assert(offsetof(ShadowVertex, x) == 0);
static_assert(offsetof(ShadowVertex, y) == 4);
static_assert(offsetof(ShadowVertex, z) == 8);

//======================================================================================================================
std::array<ShadowVertex, 6> shadowQuad(float x0, float x1, float y0, float y1, float z) {
    return {{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y0, z}, {x1, y1, z}, {x0, y1, z}}};
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
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    REQUIRE(gpu != nullptr);

    auto shadowMap = lmx::engine::GpuTexture::create(
        gpu->device(), {.format = Format::D32Float,
                        .extent = {kShadowMapSize, kShadowMapSize, 1},
                        .usage = TextureUsage::DepthStencil | TextureUsage::Sampled,
                        .label = "lmx.test.shadowMap"});
    INFO(errorOf(shadowMap));
    REQUIRE(shadowMap.has_value());

    const auto makeShadowDestination = [&](const char* label) {
        return lmx::engine::GpuTexture::create(gpu->device(),
                                               {.format = Format::BGRA8Unorm,
                                                .extent = {kShadowMapSize, kShadowMapSize, 1},
                                                .usage = TextureUsage::RenderTarget,
                                                .label = label});
    };
    auto pcfDestination = makeShadowDestination("lmx.test.shadowPcfDestination");
    INFO(errorOf(pcfDestination));
    REQUIRE(pcfDestination.has_value());
    auto pcssDestination = makeShadowDestination("lmx.test.shadowPcssDestination");
    INFO(errorOf(pcssDestination));
    REQUIRE(pcssDestination.has_value());

    auto library = gpu->loadShaderLibrary("ShadowSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto depthPipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline({.vertex = {library->get(), "vertexMain"},
                                      .fragment = {library->get(), "fragmentDepthOnly"},
                                      .raster = {.cull = CullMode::Back},
                                      .depthStencil = {.depthTest = true,
                                                       .depthWrite = true,
                                                       .depthCompare = CompareOp::Greater},
                                      .depthFormat = Format::D32Float,
                                      .label = "lmx.test.shadowDepthPipeline"}));
    INFO(errorOf(depthPipeline));
    REQUIRE(depthPipeline.has_value());

    const std::array<ColorTarget, 1> colors{{{.format = Format::BGRA8Unorm}}};
    auto shadowPipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline({.vertex = {library->get(), "vertexMain"},
                                      .fragment = {library->get(), "fragmentMain"},
                                      .raster = {.cull = CullMode::Back},
                                      .depthStencil = {.depthTest = false,
                                                       .depthWrite = false,
                                                       .depthCompare = CompareOp::Less},
                                      .colors = colors,
                                      .label = "lmx.test.shadowReadPipeline"}));
    INFO(errorOf(shadowPipeline));
    REQUIRE(shadowPipeline.has_value());

    auto shadowSampler = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createSampler({.filter = SamplerFilter::Linear,
                                                    .address = SamplerAddress::ClampToEdge,
                                                    .maxAnisotropy = 16,
                                                    .compareEnable = true,
                                                    .compare = CompareOp::GreaterOrEqual,
                                                    .label = "lmx.test.shadowCompareSampler"}));
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

    lmx::test::ShadowSmokeUniforms uniforms{.lightViewProj = lightViewProj,
                                            .shadowTransform = ndcToTexcoord * lightViewProj};

    const std::array<ShadowVertex, 6> receiver = shadowQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f);
    const std::array<ShadowVertex, 6> occluder = shadowQuad(-0.5f, 0.5f, -0.25f, 0.75f, 0.2f);

    auto gpuFrame1 = gpu->beginFrame();
    auto& commands = gpuFrame1.commands;

    const SubresourceRange range{0, 1, 0, 1};
    const TextureBarrier beginDepth{.texture = shadowMap->rhi(),
                                    .range = range,
                                    .dstStage = Stage::DepthStencil,
                                    .dstAccess =
                                        Access::DepthStencilRead | Access::DepthStencilWrite,
                                    .newLayout = Layout::DepthStencilWrite};
    commands.barrier({}, {}, std::span{&beginDepth, 1});
    const auto occluderAddress =
        commands.uploadFrameData(std::as_bytes(std::span{occluder}), alignof(ShadowVertex));
    const auto receiverAddress =
        commands.uploadFrameData(std::as_bytes(std::span{receiver}), alignof(ShadowVertex));
    const lmx::test::ShadowSmokeRoot depthRoot{
        .vertices = occluderAddress, .uniforms = lmx::render::frameData(commands, uniforms)};
    commands.beginRenderPass({.depth = {.texture = shadowMap->rhi(),
                                        .load = LoadOp::Clear,
                                        .store = StoreOp::Store,
                                        .clearDepth = 0.0f},
                              .area = {0, 0, kShadowMapSize, kShadowMapSize},
                              .label = "lmx.test.shadowSmoke.map"});
    commands.setPipeline(depthPipeline->get());
    commands.setViewport({.width = float(kShadowMapSize), .height = float(kShadowMapSize)});
    commands.setScissor({0, 0, kShadowMapSize, kShadowMapSize});
    commands.pushRoot(depthRoot);
    commands.draw(static_cast<uint32_t>(occluder.size()));
    commands.endRenderPass();

    const TextureBarrier sampleDepth{.texture = shadowMap->rhi(),
                                     .range = range,
                                     .srcStage = Stage::DepthStencil,
                                     .srcAccess =
                                         Access::DepthStencilRead | Access::DepthStencilWrite,
                                     .dstStage = Stage::FragmentShader,
                                     .dstAccess = Access::ShaderRead,
                                     .oldLayout = Layout::DepthStencilWrite,
                                     .newLayout = Layout::DepthStencilRead};
    commands.barrier({}, {}, std::span{&sampleDepth, 1});

    const auto receiverPass = [&](Texture destination, int32_t filter) {
        uniforms.filter = filter;
        const TextureBarrier beginColor{.texture = destination,
                                        .range = range,
                                        .dstStage = Stage::ColorAttachment,
                                        .dstAccess = Access::ColorAttachmentWrite,
                                        .newLayout = Layout::ColorAttachment};
        commands.barrier({}, {}, std::span{&beginColor, 1});
        const ColorAttachment color{.texture = destination,
                                    .load = LoadOp::Clear,
                                    .store = StoreOp::Store,
                                    .clear = {1.0f, 0.0f, 1.0f, 1.0f}};
        const lmx::test::ShadowSmokeRoot root{
            .vertices = receiverAddress,
            .uniforms = lmx::render::frameData(commands, uniforms),
            .shadow = static_cast<uint32_t>(shadowMap->view()),
            .sampler = static_cast<uint32_t>(shadowSampler->get())};
        commands.beginRenderPass({.colors = std::span{&color, 1},
                                  .area = {0, 0, kShadowMapSize, kShadowMapSize},
                                  .label = "lmx.test.shadowSmoke.receiver"});
        commands.setPipeline(shadowPipeline->get());
        commands.setViewport({.width = float(kShadowMapSize), .height = float(kShadowMapSize)});
        commands.setScissor({0, 0, kShadowMapSize, kShadowMapSize});
        commands.pushRoot(root);
#ifndef NDEBUG
        const ResourceAccess access{.texture = shadowMap->rhi(),
                                    .range = range,
                                    .stage = Stage::FragmentShader,
                                    .access = Access::ShaderRead};
        commands.declareAccess(std::span{&access, 1});
#endif
        commands.draw(static_cast<uint32_t>(receiver.size()));
        commands.endRenderPass();
    };
    receiverPass(pcfDestination->rhi(), kFilterPcf);
    receiverPass(pcssDestination->rhi(), kFilterPcss);

    gpu->submitAndEndFrame(gpuFrame1);
    gpu->waitIdle();

    const lmx::render::TextureRangeState written{
        .range = range,
        .state = {Stage::ColorAttachment, Access::ColorAttachmentWrite, Layout::ColorAttachment}};
    std::vector<uint8_t> pcf(size_t{kShadowMapSize} * kShadowMapSize * 4);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(*gpu, *pcfDestination, std::span{&written, 1});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pcf.data(), readbackBytes->data(), pcf.size());
    }
    std::vector<uint8_t> pcss(size_t{kShadowMapSize} * kShadowMapSize * 4);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(*gpu, *pcssDestination, std::span{&written, 1});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pcss.data(), readbackBytes->data(), pcss.size());
    }

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

    if (const char* output = std::getenv("LMX_TEST_SHADOW_OUTPUT")) {
        const auto writeBytes = [&](const char* name, const std::vector<uint8_t>& bytes) {
            std::ofstream file(std::filesystem::path(output) / name, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            REQUIRE(file.good());
        };
        writeBytes("shadow-pcf.bgra", pcf);
        writeBytes("shadow-pcss.bgra", pcss);
    }
}
