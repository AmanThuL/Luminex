#include "Engine/Scene/Scene.h"

#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include <cstring>

#include <algorithm>
#include <cmath>
#include <glm/gtc/packing.hpp>

namespace {
namespace render = lmx::render;
namespace engine = lmx::engine;

constexpr uint32_t kRejectedCount = 15;

struct ContributionTargets {
    std::array<const engine::GpuTexture*, 4> textures{};
    render::TextureUse hdrUse = render::TextureUse::CopySource;
    engine::GpuBuffer depth;
};

//======================================================================================================================
engine::MeshData contributionQuad() {
    return {.vertices = {{-0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
engine::Scene contributionScene(rojoRHI::Device& device) {
    engine::Scene result;
    result.name = "lmx.test.visibility.contribution";
    result.boundingSphere = {0, 0, -2, 4};
    for (auto& light : result.lights)
        light.strength = {};
    const std::array<uint8_t, 16> texels{255, 255, 255, 0, 255, 255, 255, 255,
                                         255, 255, 255, 0, 255, 255, 255, 255};
    const rojoRHI::TextureMip mip{.data = texels.data(), .bytesPerRow = 16};
    auto texture = engine::GpuTexture::create(device,
                                              {.width = 4,
                                               .height = 1,
                                               .format = rojoRHI::Format::RGBA8Unorm_sRGB,
                                               .sampled = true,
                                               .label = "lmx.test.visibility.cutout"},
                                              std::span{&mip, 1});
    INFO(errorOf(texture));
    REQUIRE(texture);
    const auto cutout = result.addTexture(std::move(*texture));
    const auto mesh = result.addMesh(contributionQuad(), "lmx.test.visibility.quad");
    std::array<engine::MaterialId, 3> materials;
    for (uint32_t variant = 0; variant < materials.size(); ++variant) {
        glm::vec3 emissive{0};
        emissive[variant] = 2;
        materials[variant] = result.addMaterial(
            {.diffuse = variant == 0 ? std::nullopt : std::optional{cutout},
             .albedo = {0, 0, 0, 1},
             .emissive = emissive,
             .alphaMode = variant == 0 ? engine::AlphaMode::Opaque : engine::AlphaMode::Mask,
             .alphaCutoff = 0.5f,
             .doubleSided = variant == 2});
    }
    const std::array<glm::vec3, 5> outside{
        {{-2.5f, 0, -2}, {2.5f, 0, -2}, {0, -2.5f, -2}, {0, 2.5f, -2}, {0, 0, -0.04f}}};
    for (uint32_t variant = 0; variant < materials.size(); ++variant) {
        for (uint32_t plane = 0; plane < outside.size(); ++plane) {
            result.addObject({.position = outside[plane],
                              .eulerDegrees = {0, variant == 2 ? 180.0f : 0, 0},
                              .scale = glm::vec3{plane == 4 ? 0.1f : 1.0f},
                              .mesh = mesh,
                              .material = materials[variant]});
        }
    }
    result.addObject({.position = {2, 0, -2}, .mesh = mesh, .material = materials[0]});
    result.addObject({.position = {-0.7f, -0.7f, -2},
                      .scale = glm::vec3{2},
                      .mesh = mesh,
                      .material = materials[1],
                      .motionClass = engine::MotionClass::Invalid});
    result.addObject({.position = {0.7f, 0.7f, -2},
                      .eulerDegrees = {0, 180, 0},
                      .scale = glm::vec3{2},
                      .mesh = mesh,
                      .material = materials[2]});
    REQUIRE(result.finalize(device));
    return result;
}

//======================================================================================================================
std::unique_ptr<render::Renderer> contributionRenderer(engine::GpuContext& gpu) {
    auto result = render::Renderer::create(gpu, kSize, kSize, true);
    INFO(errorOf(result));
    REQUIRE(result);
    std::fill_n((*result)->clearColor, 3, 0.0f);
    return std::move(*result);
}

//======================================================================================================================
ContributionTargets contributionTargets(engine::GpuContext& gpu, render::Renderer& renderer,
                                        render::TransientPool& pool, rojoRHI::CommandList& commands,
                                        const engine::Camera& camera, const render::SceneView& view,
                                        uint64_t frame, rojoRHI::ComputePipeline& depthPipeline) {
    pool.beginFrame();
    render::RenderGraph graph(pool);
    graph.exportTexture(renderer.declarePasses(graph, commands, camera, view));
    const auto compiled = graph.compileFrame(frame);
    REQUIRE(compiled);
    const auto& record = *compiled;
    ContributionTargets targets;
    targets.textures = {&renderer.hdrColorTexture(), &renderer.depthTexture(),
                        renderer.motionTexture(), nullptr};
    render::GraphTexture depthHandle;
    for (uint32_t pass = 0; pass < record.debug.passes.size(); ++pass) {
        if (record.debug.passes[pass].label != "lmx.pass.scene")
            continue;
        REQUIRE_FALSE(record.debug.passes[pass].cullReason);
        for (const auto& use : record.debug.passes[pass].uses) {
            if (use.role == render::UseRole::DepthAttachment)
                depthHandle = render::nextVersion(render::GraphTexture{use.resource, use.version});
            if (record.debug.resources[use.resource].name != "lmx.render.reactive")
                continue;
            REQUIRE(use.role == render::UseRole::ColorAttachment);
            const auto texture = graph.passResources(pass).texture({use.resource, use.version});
            REQUIRE(texture);
            targets.textures[3] = renderer.reactiveTexture();
            REQUIRE(targets.textures[3] != nullptr);
            REQUIRE(&targets.textures[3]->rhi() == *texture);
        }
    }
    REQUIRE(std::ranges::all_of(targets.textures,
                                [](const auto* texture) { return texture != nullptr; }));
    auto depth = engine::GpuBuffer::create(gpu.device(),
                                           {.size = uint64_t{kSize} * kSize * 4,
                                            .storageWrite = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.visibility.depth"},
                                           nullptr);
    REQUIRE(depth);
    targets.depth = std::move(*depth);
    targets.hdrUse = renderer.temporalStatus().upscaled ? render::TextureUse::ShaderRead
                                                        : render::TextureUse::CopySource;
    const auto depthBuffer = graph.importBuffer(targets.depth.rhi(), "lmx.test.visibility.depth");
    graph.addComputePass("lmx.test.visibility.readDepth",
                         {.shaderTextureReads = {depthHandle}, .bufferWrites = {depthBuffer}},
                         [&](const render::PassResources& resources) {
                             const auto source = resources.texture(depthHandle);
                             const auto destination = resources.buffer(depthBuffer);
                             REQUIRE(source);
                             REQUIRE(destination);
                             commands.bindComputePipeline(depthPipeline);
                             commands.bindTexture(1, **source);
                             commands.bindStorageBuffer(0, **destination,
                                                        rojoRHI::StorageAccess::Write);
                             commands.dispatch(kSize / 8, kSize / 8, 1);
                         });
    graph.readbackBuffer(render::nextVersion(depthBuffer));
    graph.execute(commands, frame);
    return targets;
}

//======================================================================================================================
std::array<std::vector<uint8_t>, 4> contributionBytes(engine::GpuContext& gpu,
                                                      const ContributionTargets& targets) {
    constexpr std::array<uint32_t, 4> bytesPerPixel{8, 4, 4, 1};
    std::array<std::vector<uint8_t>, 4> result;
    for (uint32_t attachment = 0; attachment < result.size(); ++attachment) {
        result[attachment].resize(size_t{kSize} * kSize * bytesPerPixel[attachment]);
        if (attachment == 1) {
            const auto readbackBytes =
                lmx::render::readbackNow(gpu, targets.depth, lmx::render::BufferUse::StorageWrite,
                                         {0, result[attachment].size()});
            std::memcpy(result[attachment].data(), readbackBytes.data(), result[attachment].size());
        } else {
            const auto readbackBytes = lmx::render::readbackNow(
                gpu, *targets.textures[attachment],
                attachment == 0 ? targets.hdrUse : render::TextureUse::RenderTarget);
            std::memcpy(result[attachment].data(), readbackBytes.data(), result[attachment].size());
        }
    }
    return result;
}

//======================================================================================================================
void requireOutsideClip(const engine::Scene& scene, const glm::mat4& viewProjection) {
    // Check vertex clip inequalities directly, independently of extracted planes and AABBs.
    for (uint32_t row = 0; row < kRejectedCount; ++row) {
        for (const glm::vec2 corner :
             {glm::vec2{-0.2f, -0.2f}, {0.2f, -0.2f}, {0.2f, 0.2f}, {-0.2f, 0.2f}}) {
            const auto clip =
                viewProjection * scene.tables().instanceRows[row].model * glm::vec4(corner, 0, 1);
            const std::array<float, 5> distances{clip.w + clip.x, clip.w - clip.x, clip.w + clip.y,
                                                 clip.w - clip.y, clip.w - clip.z};
            REQUIRE(distances[row % 5] < 0);
        }
    }
}

//======================================================================================================================
void requireSentinelCoverage(engine::GpuContext& gpu, const ContributionTargets& targets,
                             uint32_t width, const engine::Scene& scene,
                             const glm::mat4& viewProjection, bool expectRigidMotion) {
    std::vector<uint16_t> hdr(size_t{kSize} * kSize * 4);
    std::vector<float> depth(size_t{kSize} * kSize);
    std::vector<uint16_t> motion(size_t{kSize} * kSize * 2);
    std::vector<uint8_t> reactive(size_t{kSize} * kSize);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, *targets.textures[0], targets.hdrUse);
        std::memcpy(hdr.data(), readbackBytes.data(), hdr.size() * sizeof(uint16_t));
    }
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, targets.depth, lmx::render::BufferUse::StorageWrite,
                                     {0, depth.size() * sizeof(float)});
        std::memcpy(depth.data(), readbackBytes.data(), depth.size() * sizeof(float));
    }
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, *targets.textures[2], render::TextureUse::RenderTarget);
        std::memcpy(motion.data(), readbackBytes.data(), motion.size() * sizeof(uint16_t));
    }
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, *targets.textures[3], render::TextureUse::RenderTarget);
        std::memcpy(reactive.data(), readbackBytes.data(), reactive.size());
    }
    std::array<uint32_t, 3> covered{};
    uint32_t rigidMotion = 0;
    for (uint32_t y = 0; y < width; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t pixel = size_t{y} * kSize + x;
            bool colored = false;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                if (glm::unpackHalf1x16(hdr[pixel * 4 + channel]) > 0.1f) {
                    ++covered[channel];
                    colored = true;
                }
            }
            REQUIRE((depth[pixel] > 0) == colored);
            REQUIRE((reactive[pixel] > 0) == colored);
            const auto mx = glm::unpackHalf1x16(motion[pixel * 2]);
            if (glm::unpackHalf1x16(hdr[pixel * 4 + 1]) > 0.1f)
                REQUIRE(std::isinf(mx));
            else
                REQUIRE(std::isfinite(mx));
            if (glm::unpackHalf1x16(hdr[pixel * 4 + 2]) > 0.1f && std::abs(mx) > 0.001f)
                ++rigidMotion;
        }
    }
    for (const auto count : covered)
        REQUIRE(count > 0);
    if (expectRigidMotion)
        REQUIRE(rigidMotion > 0);
    // Probe the centres of one transparent and one opaque texel inside each mask quad.
    // Their margins exceed half a render pixel at either tested extent, including jitter.
    for (uint32_t variant = 1; variant < 3; ++variant) {
        for (const bool solid : {false, true}) {
            const auto clip = viewProjection *
                              scene.objects[kRejectedCount + variant].modelMatrix() *
                              glm::vec4(solid ? -0.05f : -0.15f, 0, 0, 1);
            const auto uv = render::clipToMotionUv(clip);
            const auto x = static_cast<uint32_t>(uv.x * width);
            const auto y = static_cast<uint32_t>(uv.y * width);
            REQUIRE(x < width);
            REQUIRE(y < width);
            const size_t pixel = size_t{y} * kSize + x;
            CAPTURE(variant, solid, x, y);
            REQUIRE((depth[pixel] > 0) == solid);
            REQUIRE((reactive[pixel] > 0) == solid);
            REQUIRE((glm::unpackHalf1x16(hdr[pixel * 4 + variant]) > 0.1f) == solid);
        }
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("rejected geometry contributes no camera attachments through the jitter cycle",
          "[gpu][visibility][visibility-contribution]") {
    auto gpu = createTestGpu();
    auto library = gpu->loadShaderLibrary("VisibilityDepthReadback");
    INFO(errorOf(library));
    REQUIRE(library);
    auto depthPipeline =
        gpu->device().createComputePipeline({.library = library->get(),
                                             .computeEntry = "computeMain",
                                             .threadsPerThreadgroup = {8, 8, 1},
                                             .label = "lmx.test.visibility.depthReadback"});
    INFO(errorOf(depthPipeline));
    REQUIRE(depthPipeline);
    auto scene = contributionScene(gpu->device());
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    for (const float scale : {1.0f, 0.5f}) {
        auto culled = contributionRenderer(*gpu);
        auto unculled = contributionRenderer(*gpu);
        auto empty = contributionRenderer(*gpu);
        render::TransientPool culledPool(gpu->device()), unculledPool(gpu->device()),
            emptyPool(gpu->device());
        for (const bool sentinels : {false, true}) {
            for (uint32_t phase = 0; phase < render::kJitterSequenceLength; ++phase) {
                CAPTURE(scale, sentinels, phase);
                scene.objects.back().position.x = phase % 2 == 0 ? 0.7f : 0.72f;
                auto commandsFrame = gpu->beginFrame();
                auto& commands = commandsFrame.commands;
                REQUIRE(scene.prepareFrame(gpu->frameNumber()));
                std::vector<engine::DrawItem> items;
                auto view = render::buildSceneView(scene, items, false);
                if (!sentinels)
                    view.items = view.items.first(kRejectedCount);
                view.bloomEnabled = false;
                view.submission = render::SubmissionMode::Indirect;
                view.visibilityEnabled = true;
                view.temporal.enabled = true;
                view.temporal.jitterEnabled = true;
                view.temporal.reconstruction = render::ReconstructionMode::Raw;
                view.temporal.renderScale = scale;
                view.temporal.sceneGeneration = scene.objects.front().id.store;
                const auto extents = render::renderExtentsForScale(kSize, kSize, scale);
                const auto viewProjection = render::buildCameraFrameState(
                                                camera, extents, render::haltonJitterPixels(phase))
                                                .viewProjectionJittered;
                requireOutsideClip(scene, viewProjection);
                const auto frame = gpu->frameNumber();
                const auto culledTargets = contributionTargets(
                    *gpu, *culled, culledPool, commands, camera, view, frame, **depthPipeline);
                view.visibilityEnabled = false;
                const auto unculledTargets = contributionTargets(
                    *gpu, *unculled, unculledPool, commands, camera, view, frame, **depthPipeline);
                view.items = {};
                const auto emptyTargets = contributionTargets(*gpu, *empty, emptyPool, commands,
                                                              camera, view, frame, **depthPipeline);
                gpu->submitAndEndFrame(commandsFrame);
                scene.commitFrame();
                gpu->waitIdle();
                REQUIRE(culled->temporalStatus().jitterIndex == phase);
                REQUIRE(unculled->temporalStatus().jitterIndex == phase);
                REQUIRE(empty->temporalStatus().jitterIndex == phase);
                REQUIRE(culled->visibilityStatus().scene.rejected == kRejectedCount);
                REQUIRE(culled->visibilityStatus().scene.visible == (sentinels ? 3 : 0));
                REQUIRE(culled->visibilityStatus().shadow.rejected == 0);
                REQUIRE(culled->visibilityStatus().submission.sceneCommands == (sentinels ? 3 : 0));
                REQUIRE(unculled->visibilityStatus().submission.sceneCommands ==
                        kRejectedCount + (sentinels ? 3 : 0));
                const auto actual = contributionBytes(*gpu, culledTargets);
                const auto submitted = contributionBytes(*gpu, unculledTargets);
                const auto clear = contributionBytes(*gpu, emptyTargets);
                for (uint32_t attachment = 1; attachment < clear.size(); ++attachment)
                    REQUIRE(
                        std::ranges::all_of(clear[attachment], [](uint8_t b) { return b == 0; }));
                for (uint32_t attachment = 0; attachment < actual.size(); ++attachment) {
                    CAPTURE(attachment);
                    REQUIRE(actual[attachment] == submitted[attachment]);
                    if (!sentinels)
                        REQUIRE(submitted[attachment] == clear[attachment]);
                    else
                        REQUIRE(actual[attachment] != clear[attachment]);
                }
                if (sentinels)
                    requireSentinelCoverage(*gpu, culledTargets, extents.renderWidth, scene,
                                            viewProjection, phase > 0);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("GPU classification preserves every camera attachment through the jitter cycle",
          "[gpu][visibility][visibility-contribution][gpu-classify]") {
    auto gpuContext = createTestGpu();
    auto library = gpuContext->loadShaderLibrary("VisibilityDepthReadback");
    INFO(errorOf(library));
    REQUIRE(library);
    auto depthPipeline =
        gpuContext->device().createComputePipeline({.library = library->get(),
                                                    .computeEntry = "computeMain",
                                                    .threadsPerThreadgroup = {8, 8, 1},
                                                    .label = "lmx.test.visibility.depthReadback"});
    REQUIRE(depthPipeline);
    auto scene = contributionScene(gpuContext->device());
    engine::Camera camera;
    camera.fovY = glm::half_pi<float>();
    for (const auto submission :
         {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        for (const float scale : {1.0f, 0.5f}) {
            auto cpu = contributionRenderer(*gpuContext);
            auto gpu = contributionRenderer(*gpuContext);
            render::TransientPool cpuPool(gpuContext->device()), gpuPool(gpuContext->device());
            for (uint32_t phase = 0; phase < render::kJitterSequenceLength; ++phase) {
                CAPTURE(static_cast<int>(submission), scale, phase);
                scene.objects.back().position.x = phase % 2 == 0 ? 0.7f : 0.72f;
                auto commandsFrame = gpuContext->beginFrame();
                auto& commands = commandsFrame.commands;
                REQUIRE(scene.prepareFrame(gpuContext->frameNumber()));
                std::vector<engine::DrawItem> items;
                auto view = render::buildSceneView(scene, items, false);
                view.bloomEnabled = false;
                view.submission = submission;
                view.visibilityEnabled = true;
                view.temporal.enabled = true;
                view.temporal.jitterEnabled = true;
                view.temporal.reconstruction = render::ReconstructionMode::Raw;
                view.temporal.renderScale = scale;
                view.temporal.sceneGeneration = scene.objects.front().id.store;
                const auto frame = gpuContext->frameNumber();
                const auto expected = contributionTargets(*gpuContext, *cpu, cpuPool, commands,
                                                          camera, view, frame, **depthPipeline);
                view.classifyMode = render::ClassifyMode::Gpu;
                const auto actual = contributionTargets(*gpuContext, *gpu, gpuPool, commands,
                                                        camera, view, frame, **depthPipeline);
                gpuContext->submitAndEndFrame(commandsFrame);
                scene.commitFrame();
                gpuContext->waitIdle();
                REQUIRE(cpu->temporalStatus().jitterIndex == phase);
                REQUIRE(gpu->temporalStatus().jitterIndex == phase);
                const auto expectedBytes = contributionBytes(*gpuContext, expected);
                const auto actualBytes = contributionBytes(*gpuContext, actual);
                for (uint32_t attachment = 0; attachment < expectedBytes.size(); ++attachment) {
                    CAPTURE(attachment);
                    REQUIRE(actualBytes[attachment] == expectedBytes[attachment]);
                }
            }
        }
    }
}
