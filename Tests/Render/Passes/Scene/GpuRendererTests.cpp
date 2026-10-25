#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include <cstring>
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
constexpr const char* kRefusalCase =
    "a pass resolving an undeclared texture is refused while the frame runs";
constexpr const char* kRefusalMarker = "lmx.scene.refusal.reached-body";

struct RefusalChild {
    pid_t pid = -1;
    int input = -1;
    int output = -1;
    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    bool actionsReady = false;
    bool attributesReady = false;
    //==================================================================================================================
    ~RefusalChild() {
        if (attributesReady)
            ::posix_spawnattr_destroy(&attributes);
        if (actionsReady)
            ::posix_spawn_file_actions_destroy(&actions);
        if (input >= 0)
            ::close(input);
        if (output >= 0)
            ::close(output);
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
};

//======================================================================================================================
std::string refusalDiagnostic() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable(size, '\0');
    REQUIRE(_NSGetExecutablePath(executable.data(), &size) == 0);
    executable.resize(std::strlen(executable.c_str()));
    RefusalChild child;
    int pipeEnds[2];
    REQUIRE(::pipe(pipeEnds) == 0);
    child.input = pipeEnds[0];
    child.output = pipeEnds[1];
    int error = ::posix_spawn_file_actions_init(&child.actions);
    child.actionsReady = error == 0;
    REQUIRE(error == 0);
    error = ::posix_spawn_file_actions_adddup2(&child.actions, child.output, STDOUT_FILENO);
    if (!error)
        error = ::posix_spawn_file_actions_adddup2(&child.actions, child.output, STDERR_FILENO);
    if (!error)
        error = ::posix_spawn_file_actions_addclose(&child.actions, child.input);
    if (!error)
        error = ::posix_spawn_file_actions_addclose(&child.actions, child.output);
    if (!error)
        error = ::posix_spawn_file_actions_addopen(&child.actions, STDIN_FILENO, "/dev/null",
                                                   O_RDONLY, 0);
    std::string filter = std::string("\"") + kRefusalCase + "\"";
    std::array<char*, 3> args{executable.data(), filter.data(), nullptr};
    std::vector<std::string> variables;
    for (char** entry = environ; *entry; ++entry) {
        if (!std::string_view(*entry).starts_with("LMX_SCENE_REFUSAL_CHILD="))
            variables.emplace_back(*entry);
    }
    variables.emplace_back("LMX_SCENE_REFUSAL_CHILD=1");
    std::vector<char*> environment;
    for (auto& variable : variables)
        environment.push_back(variable.data());
    environment.push_back(nullptr);
    if (!error) {
        error = ::posix_spawnattr_init(&child.attributes);
        child.attributesReady = error == 0;
    }
    if (!error)
        error = ::posix_spawnattr_setflags(&child.attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    if (!error)
        error = ::posix_spawn(&child.pid, executable.c_str(), &child.actions, &child.attributes,
                              args.data(), environment.data());
    INFO("posix_spawn error " << error);
    REQUIRE(error == 0);
    ::close(child.output);
    child.output = -1;
    std::string diagnostic;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        pollfd ready{.fd = child.input, .events = POLLIN | POLLHUP, .revents = 0};
        const int polled = ::poll(&ready, 1, 100);
        if (polled < 0 && errno == EINTR)
            continue;
        REQUIRE(polled >= 0);
        if (!polled)
            continue;
        std::array<char, 4096> bytes;
        const auto count = ::read(child.input, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR)
            continue;
        REQUIRE(count >= 0);
        if (!count)
            break;
        diagnostic.append(bytes.data(), static_cast<size_t>(count));
    }
    ::close(child.input);
    child.input = -1;
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(child.pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE(waited == child.pid);
    child.pid = -1;
    INFO(diagnostic);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
    REQUIRE(diagnostic.contains(kRefusalMarker));
    return diagnostic;
}
} // namespace

//======================================================================================================================
// Separated red and blue cubes catch argument-table last-write reuse across per-draw uniforms.
TEST_CASE("renderer draws per-object uniforms in one pass", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto cube = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.cube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);

    auto gpuFrame1 = gpu->beginFrame();
    auto& commands = gpuFrame1.commands;
    (*renderer)->render(commands, sceneCamera(),
                        lmx::test::prepareSceneView(litSceneView(items), *gpu));
    gpu->submitAndEndFrame(gpuFrame1);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, (*renderer)->colorTarget(),
                                                            (*renderer)->textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }
    requireTwoCubeImage(pixels, "clear");
}

//======================================================================================================================
// Overlapping geometry must resolve by depth rather than submission order.
TEST_CASE("renderer depth test beats draw order", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto plane =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(1.0f), "lmx.test.plane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const auto upright = [](float z) {
        return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
               glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    };
    const FixtureDrawItem nearGreen{
        .mesh = &*plane, .model = upright(1.0f), .material = {.albedo = {0.0f, 1.0f, 0.0f, 1.0f}}};
    const FixtureDrawItem farRed{
        .mesh = &*plane, .model = upright(-1.0f), .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}};

    const std::array<std::array<FixtureDrawItem, 2>, 2> orderings = {{
        {{farRed, nearGreen}}, // back-to-front: passes even without a depth buffer
        {{nearGreen, farRed}}, // front-to-back: only depth can keep green on top
    }};
    const std::array<const char*, 2> names = {"far-then-near", "near-then-far"};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    for (size_t i = 0; i < orderings.size(); ++i) {
        auto gpuFrame2 = gpu->beginFrame();
        auto& commands = gpuFrame2.commands;
        (*renderer)->render(commands, sceneCamera(),
                            lmx::test::prepareSceneView(litSceneView(orderings[i]), *gpu));
        gpu->submitAndEndFrame(gpuFrame2);
        gpu->waitIdle();
        {
            const auto readbackBytes = lmx::render::readbackNow(*gpu, (*renderer)->colorTarget(),
                                                                (*renderer)->textureStates());
            INFO(errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
        }

        INFO(std::string("ordering: ") + names[i]);

        const Pixel center = pixelAt(pixels, 32, 32);
        INFO(describe("center", 32, 32, center));
        REQUIRE(center.g > 64);
        REQUIRE(center.g > center.r + 32);
        REQUIRE(center.g > center.b + 32);

        const Pixel corner = pixelAt(pixels, 2, 2);
        INFO(describe("corner", 2, 2, corner));
        requireClearPixel(corner);
    }
}

//======================================================================================================================
// A magenta destination makes a missing draw visible while the copied image pins the pass barrier.
TEST_CASE("renderer scene survives a barrier into a sampling pass", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.barrierCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = lmx::engine::GpuTexture::create(
        gpu->device(), {.format = Format::BGRA8Unorm,
                        .extent = {kSize, kSize, 1},
                        .usage = rojoRHI::TextureUsage::RenderTarget,
                        .label = "lmx.test.barrierDestination"});
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto copyPipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline(
            {.vertex = {library->get(), "vertexMain"},
             .fragment = {library->get(), "fragmentMain"},
             .raster = {.cull = rojoRHI::CullMode::Back},
             .depthStencil = {.depthTest = false,
                              .depthWrite = false,
                              .depthCompare = rojoRHI::CompareOp::Less},
             .colors = std::array{rojoRHI::ColorTarget{.format = Format::BGRA8Unorm}},
             .label = "lmx.test.fullscreenSamplePipeline"}));
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    auto gpuFrame3 = gpu->beginFrame();
    auto& commands = gpuFrame3.commands;
    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    (*renderer)->render(commands, sceneCamera(),
                        lmx::test::prepareSceneView(litSceneView(items), *gpu));
    // The pass below is encoded by hand, so it declared no read the graph could order.
    auto& states = (*renderer)->textureStates();
    std::vector<TextureBarrier> sourceBarriers;
    for (const auto& entry :
         states.physicalRanges((*renderer)->colorTarget().rhi(), {0, 1, 0, 1})) {
        sourceBarriers.push_back({.texture = (*renderer)->colorTarget().rhi(),
                                  .range = entry.range,
                                  .srcStage = entry.state.stage,
                                  .srcAccess = entry.state.access,
                                  .dstStage = Stage::FragmentShader,
                                  .dstAccess = Access::ShaderRead,
                                  .oldLayout = entry.state.layout,
                                  .newLayout = Layout::ShaderRead});
    }
    commands.barrier({}, {}, sourceBarriers);

    const rojoRHI::TextureBarrier beginColor{.texture = destination->rhi(),
                                             .range = {0, 1, 0, 1},
                                             .dstStage = rojoRHI::Stage::ColorAttachment,
                                             .dstAccess = rojoRHI::Access::ColorAttachmentWrite,
                                             .newLayout = rojoRHI::Layout::ColorAttachment};
    commands.barrier({}, {}, std::span(&beginColor, 1));
    const rojoRHI::ColorAttachment color{.texture = destination->rhi(),
                                         .load = rojoRHI::LoadOp::Clear,
                                         .store = rojoRHI::StoreOp::Store,
                                         .clear = {1.0f, 0.0f, 1.0f, 1.0f}};
    commands.beginRenderPass({.colors = std::span(&color, 1),
                              .area = {0, 0, kSize, kSize},
                              .label = "lmx.test.rendererCopy"});
    commands.setViewport({.width = float(kSize), .height = float(kSize)});
    commands.setScissor({0, 0, kSize, kSize});
    commands.setPipeline(copyPipeline->get());
    commands.pushRoot(lmx::test::FullscreenSampleRoot{
        .source = static_cast<uint32_t>((*renderer)->colorTarget().view())});
#ifndef NDEBUG
    const rojoRHI::ResourceAccess access{.texture = (*renderer)->colorTarget().rhi(),
                                         .range = {0, 1, 0, 1},
                                         .stage = rojoRHI::Stage::FragmentShader,
                                         .access = rojoRHI::Access::ShaderRead};
    commands.declareAccess(std::span(&access, 1));
#endif
    commands.draw(3);
    commands.endRenderPass();
    gpu->submitAndEndFrame(gpuFrame3);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *destination,
            std::array{lmx::render::TextureRangeState{
                .range = {0, 1, 0, 1},
                .state = {rojoRHI::Stage::ColorAttachment, rojoRHI::Access::ColorAttachmentWrite,
                          rojoRHI::Layout::ColorAttachment}}});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }

    requireTwoCubeImage(pixels, "copied clear");
}

//======================================================================================================================
// An interior probe and total coverage bound distinguish wireframe from empty and solid output.
TEST_CASE("a wireframe SceneView leaves the interior of a face unfilled", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto cube = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.wireCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*cube,
         .model = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}),
         .material = {.albedo = {1.0f, 1.0f, 1.0f, 1.0f}}},
    }};

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto renderWith = [&](bool wireframe) {
        FixtureSceneView view = litSceneView(items);
        view.wireframe = wireframe;
        auto gpuFrame4 = gpu->beginFrame();
        auto& commands = gpuFrame4.commands;
        (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, *gpu));
        gpu->submitAndEndFrame(gpuFrame4);
        gpu->waitIdle();
        {
            const auto readbackBytes = lmx::render::readbackNow(*gpu, (*renderer)->colorTarget(),
                                                                (*renderer)->textureStates());
            INFO(errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
        }
    };

    renderWith(false);
    const Pixel solid = pixelAt(pixels, 26, 26);
    INFO(describe("solid interior", 26, 26, solid));
    // Re-derived for the GGX model: under Blinn-Phong this probe only had to clear 128, which the
    // old diffuse term (albedo times light strength, with no 1/pi) reached easily. The cube's front
    // face is a white dielectric at the default roughness, lit head-on by litSceneView's only
    // light, so the value is what the CPU mirror of the BRDF returns for N = L = V, through the
    // display transform. The probe sits about 8 degrees off the view axis, which costs it a byte; 3
    // covers that and the target's own rounding.
    const int litByte =
        lmx::test::displayByte(lmx::test::brdf::directionalLight(
                                   {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
                                   {0.0f, 0.0f, 1.0f}, {.baseColor = glm::vec3(1.0f)})
                                   .r);
    INFO("expected lit byte " << litByte);
    REQUIRE(channelNear(solid.r, litByte, 3));
    REQUIRE(channelNear(solid.g, litByte, 3));
    REQUIRE(channelNear(solid.b, litByte, 3));

    renderWith(true);
    const Pixel wire = pixelAt(pixels, 26, 26);
    INFO(describe("wireframe interior", 26, 26, wire));
    requireClearPixel(wire);
    REQUIRE(wire.a == 255);
}

//======================================================================================================================
// The corner pins the sky through the display transform; a cube probe proves depth keeps geometry
// in front of it.
TEST_CASE("the sky pass fills the background behind the scene", "[gpu]") {
    using namespace rojoRHI;

    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const TextureData skyMip{.bytes = std::as_bytes(std::span(kSkyTexel)), .bytesPerRow = 4};
    const std::array<TextureData, 6> skyFaces = {skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.skySceneCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto skySphere = lmx::test::fixtureMesh(
        gpu->device(), lmx::engine::fromGeo(lmx::asset::makeSphere(0.5f, 20, 20)),
        "lmx.test.skySphere");
    INFO(errorOf(skySphere));
    REQUIRE(skySphere.has_value());

    auto skyCubemap = lmx::engine::GpuTexture::create(gpu->device(),
                                                      {.type = TextureType::Cube,
                                                       .format = Format::RGBA8Unorm,
                                                       .extent = {1, 1, 1},
                                                       .layers = 6,
                                                       .usage = rojoRHI::TextureUsage::Sampled,
                                                       .label = "lmx.test.skyCubemap"},
                                                      skyFaces);
    INFO(errorOf(skyCubemap));
    REQUIRE(skyCubemap.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    FixtureSceneView view = litSceneView(items);
    view.skySphere = &*skySphere;
    view.skyCubemap = &*skyCubemap;

    auto gpuFrame5 = gpu->beginFrame();
    auto& commands = gpuFrame5.commands;
    (*renderer)->render(commands, sceneCamera(), lmx::test::prepareSceneView(view, *gpu));
    gpu->submitAndEndFrame(gpuFrame5);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, (*renderer)->colorTarget(),
                                                            (*renderer)->textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }

    // kSkyTexel is uploaded to a plain RGBA8Unorm cubemap, not an sRGB view, so the sampler hands
    // the shader (0, 0.502, 1.0) as linear radiance and the sky shader passes it through untouched
    // -- the display transform is the only thing between the texel and the target. Its peak
    // channel is 1.0, above the 0.76 shoulder, so unlike the mid-grey probes this one exercises
    // the compression *and* the desaturation that comes with it, which is what lifts the black
    // channel off 0 (bytes 33, 179, 241).
    const std::array<int, 3> skyBytes = lmx::test::displayBytes(
        {kSkyTexel[0] / 255.0f, kSkyTexel[1] / 255.0f, kSkyTexel[2] / 255.0f});
    const Pixel corner = pixelAt(pixels, 2, 2);
    INFO(describe("sky corner", 2, 2, corner));
    REQUIRE(channelNear(corner.r, skyBytes[0], 4));
    REQUIRE(channelNear(corner.g, skyBytes[1], 6));
    REQUIRE(channelNear(corner.b, skyBytes[2], 4));
    REQUIRE(corner.a == 255);

    const Pixel right = pixelAt(pixels, 48, 32);
    INFO(describe("blue cube under the sky", 48, 32, right));
    REQUIRE(right.b > 64);
    // Green separates the two: the sky is half-strength green, and the blue cube only picks up
    // what its specular lobe and its environment reflection carry there.
    REQUIRE(right.g < skyBytes[1] - 32);
}

//======================================================================================================================
TEST_CASE("scene pass timings name the active Off frame after slot retirement", "[gpu]") {
    auto gpu = createTestGpu();
    auto cube = fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.timedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    const auto items = twoCubeScene(*cube);
    auto view = litSceneView(items);
    view.temporal.enabled = false;
    view.bloomEnabled = false;
    view.localLightMode = lmx::engine::LocalLightMode::Off;
    auto rendered = gpu->beginFrame();
    (*renderer)->render(rendered.commands, sceneCamera(), lmx::test::prepareSceneView(view, *gpu));
    const uint64_t measured = rendered.number;
    gpu->submitAndEndFrame(rendered);
    std::vector<lmx::engine::PassTiming> timings;
    uint64_t retired = 0;
    for (uint32_t slot = 0; slot < 3 && retired != measured; ++slot) {
        auto empty = gpu->beginFrame();
        gpu->submitAndEndFrame(empty);
        const auto publication = gpu->passTimings();
        if (publication.frame == measured) {
            retired = publication.frame;
            timings.assign(publication.passes.begin(), publication.passes.end());
        }
    }
    REQUIRE(retired == measured);
    REQUIRE(timings.size() == 3);
    REQUIRE(timings[0].label == "lmx.pass.shadow");
    REQUIRE(timings[1].label == "lmx.pass.scene");
    REQUIRE(timings[2].label == "lmx.pass.display");
    for (const auto& timing : timings) {
        INFO(timing.label);
        REQUIRE(std::isfinite(timing.gpuMilliseconds));
        REQUIRE(timing.gpuMilliseconds > 0.0);
    }
}

//======================================================================================================================
// The editor's frame shape: the scene passes plus one joined pass that reads what they rendered.
// Nothing here places a barrier -- the read declaration is the only thing standing between the
// scene pass's writes and this pass's sample, so a correct image is what proves the graph derived
// the transition.
TEST_CASE("a joined pass samples the scene colour the graph rendered", "[gpu]") {
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.joinedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/false);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    auto destination = makeProbeTarget(*gpu, "lmx.test.joinedDestination");
    INFO(errorOf(destination));
    REQUIRE(destination.has_value());

    auto library = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto copyPipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline(
            {.vertex = {library->get(), "vertexMain"},
             .fragment = {library->get(), "fragmentMain"},
             .raster = {.cull = rojoRHI::CullMode::Back},
             .depthStencil = {.depthTest = false,
                              .depthWrite = false,
                              .depthCompare = rojoRHI::CompareOp::Less},
             .colors = std::array{rojoRHI::ColorTarget{.format = Format::BGRA8Unorm}},
             .label = "lmx.test.joinedCopyPipeline"}));
    INFO(errorOf(copyPipeline));
    REQUIRE(copyPipeline.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);
    const FixtureSceneView view = litSceneView(items);

    // declarePasses() always declares bloom's transients (spec 10), so a caller building its own
    // graph around it needs a pool exactly as Renderer::render()'s convenience path does.
    lmx::render::TransientPool transients(*gpu);
    auto gpuFrame8 = gpu->beginFrame();
    auto& commands = gpuFrame8.commands;
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture sceneColor = (*renderer)->declarePasses(
        graph, commands, sceneCamera(), lmx::test::prepareSceneView(view, *gpu));
    const lmx::render::GraphTexture copyTarget =
        graph.importTexture(destination->ref(), "destination");

    lmx::render::PassDesc copy;
    copy.textureReads.push_back(sceneColor);
    // Magenta makes a pass that drew nothing at all obvious rather than merely wrong.
    copy.color =
        lmx::render::ColorAttachment{.handle = copyTarget, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
    graph.addPass(
        "lmx.pass.copy", std::move(copy), [&](const lmx::render::PassResources& resources) {
            const auto source = resources.texture(sceneColor);
            REQUIRE(source.texture.bits != 0);
            commands.setPipeline(copyPipeline->get());
            commands.pushRoot(
                lmx::test::FullscreenSampleRoot{.source = static_cast<uint32_t>(source.view)});
#ifndef NDEBUG
            const rojoRHI::ResourceAccess access{.texture = source.texture,
                                                 .range = {0, 1, 0, 1},
                                                 .stage = rojoRHI::Stage::FragmentShader,
                                                 .access = rojoRHI::Access::ShaderRead};
            commands.declareAccess(std::span(&access, 1));
#endif
            commands.draw(3);
        });
    graph.exportTexture(lmx::render::nextVersion(copyTarget));
    graph.execute(commands, gpu->frameNumber());

    gpu->submitAndEndFrame(gpuFrame8);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *destination,
            std::array{lmx::render::TextureRangeState{
                .range = {0, 1, 0, 1},
                .state = {rojoRHI::Stage::ColorAttachment, rojoRHI::Access::ColorAttachmentWrite,
                          rojoRHI::Layout::ColorAttachment}}});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }
    requireTwoCubeImage(pixels, "clear sampled through the graph");
}

//======================================================================================================================
// Validation has to be live in the path the frame actually takes, not only in a unit test of the
// declaration layer: a pass body reaching for a resource it never declared is refused mid-frame,
// with the pass and the resource named.
TEST_CASE("a pass resolving an undeclared texture is refused while the frame runs", "[gpu]") {
    if (!std::getenv("LMX_SCENE_REFUSAL_CHILD")) {
        const auto refusal = refusalDiagnostic();
        REQUIRE(refusal.contains("lmx.pass.probe"));
        REQUIRE(refusal.contains("undeclaredTarget"));
        REQUIRE(refusal.contains("did not declare"));
        return;
    }
    using namespace rojoRHI;

    auto gpu = createTestGpu();

    REQUIRE(gpu != nullptr);

    auto declaredTarget = makeProbeTarget(*gpu, "lmx.test.declaredTarget");
    INFO(errorOf(declaredTarget));
    REQUIRE(declaredTarget.has_value());
    auto undeclaredTarget = makeProbeTarget(*gpu, "lmx.test.undeclaredTarget");
    INFO(errorOf(undeclaredTarget));
    REQUIRE(undeclaredTarget.has_value());

    lmx::render::RenderGraph graph;
    const lmx::render::GraphTexture declared =
        graph.importTexture(declaredTarget->ref(), "declaredTarget");
    const lmx::render::GraphTexture undeclared =
        graph.importTexture(undeclaredTarget->ref(), "undeclaredTarget");

    lmx::render::PassDesc probe;
    probe.color = lmx::render::ColorAttachment{.handle = declared};
    graph.addPass("lmx.pass.probe", std::move(probe),
                  [&](const lmx::render::PassResources& resources) {
                      const rlimit limit{0, 0};
                      REQUIRE(::setrlimit(RLIMIT_CORE, &limit) == 0);
                      std::signal(SIGABRT, SIG_DFL);
                      std::fputs(kRefusalMarker, stderr);
                      std::fputc('\n', stderr);
                      std::fflush(stderr);
                      (void)resources.texture(undeclared);
                  });
    graph.exportTexture(lmx::render::nextVersion(declared));

    auto gpuFrame9 = gpu->beginFrame();
    auto& commands = gpuFrame9.commands;
    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(gpuFrame9);
    gpu->waitIdle();

    FAIL("undeclared resolution unexpectedly returned");
}
