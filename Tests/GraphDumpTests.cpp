#include <catch2/catch_test_macros.hpp>

#include "GraphTestSupport.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {

// The dump reads mip and layer counts through the range formatting, and formats through the import
// declaration, so a fake needs nothing else.
struct FakeTexture final : rhi::Texture {

    //==================================================================================================================
    FakeTexture(uint32_t extent, uint32_t mips = 1, uint32_t layers = 1)
        : m_extent(extent), m_mipLevels(mips), m_arrayLayers(layers) {}

    //==================================================================================================================
    uint32_t width() const override { return m_extent; }

    //==================================================================================================================
    uint32_t height() const override { return m_extent; }

    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::Unknown; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return m_mipLevels; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return m_arrayLayers; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_extent = 0;
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
};

struct FakeBuffer final : rhi::Buffer {

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size) : m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint64_t m_size = 0;
};

const ExecuteFn kNoWork = [](const PassResources&) {};

//======================================================================================================================
// The Tests binary runs with its own target dir as CWD, so the golden files are addressed from the
// repo root the build passes in.
std::string goldenPath(std::string_view name) {
    return std::string(LMX_REPO_ROOT) + "/Tests/Golden/" + std::string(name);
}

//======================================================================================================================
// Compares against the golden file, and on a mismatch writes what was actually produced beside it
// so a failing run leaves the two versions to diff rather than a boolean.
void requireMatchesGolden(const std::string& dump, std::string_view name) {
    const std::string path = goldenPath(name);
    std::ifstream file(path, std::ios::binary);
    INFO("golden file: " + path);
    REQUIRE(file.good());

    std::ostringstream expected;
    expected << file.rdbuf();

    if (expected.str() != dump) {
        std::ofstream actual(path + ".actual", std::ios::binary | std::ios::trunc);
        actual << dump;
        INFO("actual output written to: " + path + ".actual");
        INFO("--- actual ---\n" + dump);
    }
    REQUIRE(expected.str() == dump);
}

} // namespace

//======================================================================================================================
// The shape of the shipped frame: a depth-only pass feeding a scene pass feeding a display pass,
// with the derived transitions between them. It is the case that would notice a change to the
// wording, the ordering, or the set of things a dump reports.
TEST_CASE("a frame's dump matches its golden file", "[render][graph]") {
    FakeTexture shadowMap{1024};
    FakeTexture sceneColor{64};
    FakeTexture sceneDepth{64};
    FakeTexture displayColor{64};
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rhi::Format::D32Float, "lmx.render.shadowMap");
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rhi::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.render.displayColor");

    // Declared before its producer, so the dump's scheduled order differs from its pass indices.
    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = color};
    scene.depth = DepthAttachment{.handle = depth, .store = StoreOp::Store};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(color));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(12);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-scene.txt");
}

//======================================================================================================================
// The substrate frame: compute and copy passes over a mip chain and a buffer, with narrowed
// subresource ranges and buffer transitions -- everything the raster frame above cannot show.
TEST_CASE("a compute and copy frame's dump matches its golden file", "[render][graph]") {
    FakeTexture chain{64, 4};
    FakeTexture output{64};
    FakeBuffer histogram{1024};
    FakeBuffer staging{1024};
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(chain, rhi::Format::RGBA16Float, "lmx.bloomChain");
    const GraphTexture target =
        graph.importTexture(output, rhi::Format::BGRA8Unorm, "lmx.composite");
    const GraphBuffer bins = graph.importBuffer(histogram, "lmx.histogram");
    const GraphBuffer readback = graph.importBuffer(staging, "lmx.staging");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.pass.histogramClear", clear, kNoWork);

    ComputePassDesc prefilter;
    prefilter.bufferReads.push_back(nextVersion(bins));
    prefilter.textureWrites.push_back({bloom, {.baseMipLevel = 0, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.bloomPrefilter", prefilter, kNoWork);

    ComputePassDesc downsample;
    downsample.textureReads.push_back(
        {nextVersion(bloom), {.baseMipLevel = 0, .mipLevelCount = 1}});
    downsample.textureWrites.push_back(
        {nextVersion(bloom), {.baseMipLevel = 1, .mipLevelCount = 2}});
    graph.addComputePass("lmx.pass.bloomDownsample", downsample, kNoWork);

    PassDesc composite;
    composite.textureReads.push_back(GraphTexture{bloom.index, 2});
    composite.color = ColorAttachment{.handle = target};
    graph.addPass("lmx.pass.composite", composite, kNoWork);

    CopyPassDesc capture;
    capture.textureSources.push_back(nextVersion(target));
    capture.bufferDestinations.push_back(readback);
    graph.addCopyPass("lmx.pass.capture", capture, kNoWork);

    graph.readbackBuffer(nextVersion(readback));

    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-compute.txt");
}

//======================================================================================================================
// A frame with dead work in it, which is the dump's whole reason for listing culled passes: the
// scheduled section says what runs, and the culled one says what was declared, was not needed, and
// why -- both reasons appearing so neither can silently stop being reported.
TEST_CASE("a culled frame's dump matches its golden file", "[render][graph]") {
    FakeTexture sceneColor{64};
    FakeTexture displayColor{64};
    FakeTexture unused{64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.sceneColor");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rhi::Format::BGRA8Unorm, "lmx.debugOverlay");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = color};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc overlay;
    overlay.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.overlay", overlay, kNoWork);

    ComputePassDesc histogram;
    histogram.textureReads.push_back(nextVersion(color));
    graph.addComputePass("lmx.pass.histogram", histogram, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(color));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.exportTexture(nextVersion(display));

    const auto record = graph.compileFrame(1);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-culled.txt");
}

//======================================================================================================================
// A frame with nothing in it still dumps every section, so the shape a reader parses does not
// depend on what a frame happened to contain.
TEST_CASE("an empty frame's dump matches its golden file", "[render][graph]") {
    RenderGraph graph;

    const auto record = graph.compileFrame(0);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-empty.txt");
}

//======================================================================================================================
// The transient frame: two graph-created textures whose lifetimes do not overlap sharing one
// placement, a third the frame culled away and therefore never paid for, and the reuse boundary
// between the two that alias. It is the case that would notice a change to how a plan is reported.
TEST_CASE("a transient frame's dump matches its golden file", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture midTarget{64};
    FakeTexture displayColor{64};
    RenderGraph graph(pool);
    const GraphTexture scene = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.sceneColor");
    const GraphTexture bloom = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.bloom");
    const GraphTexture unused = graph.createTexture({.width = 64,
                                                     .height = 64,
                                                     .format = rhi::Format::RGBA16Float,
                                                     .renderTarget = true,
                                                     .sampled = true},
                                                    "lmx.transient.unused");
    const GraphTexture mid = graph.importTexture(midTarget, rhi::Format::BGRA8Unorm, "lmx.mid");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");

    PassDesc writeScene;
    writeScene.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", writeScene, kNoWork);

    PassDesc resolve;
    resolve.textureReads.push_back(nextVersion(scene));
    resolve.color = ColorAttachment{.handle = mid};
    graph.addPass("lmx.pass.resolve", resolve, kNoWork);

    PassDesc writeBloom;
    writeBloom.color = ColorAttachment{.handle = bloom};
    graph.addPass("lmx.pass.bloom", writeBloom, kNoWork);

    PassDesc composite;
    composite.textureReads.push_back(nextVersion(bloom));
    composite.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.composite", composite, kNoWork);

    // Declared and never reached: the pass that would have filled it has no sink.
    PassDesc dead;
    dead.color = ColorAttachment{.handle = unused};
    graph.addPass("lmx.pass.dead", dead, kNoWork);

    graph.exportTexture(nextVersion(mid));
    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(9);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-transient.txt");
}

//======================================================================================================================
// The property the golden files rest on: the dump is a function of the declarations, so compiling
// the same graph twice produces identical bytes.
TEST_CASE("the same declarations dump identically", "[render][graph]") {
    FakeTexture sceneColor{64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = color};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(color));

    const auto first = graph.compileFrame(5);
    const auto second = graph.compileFrame(5);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(dumpCompiledFrame(*first) == dumpCompiledFrame(*second));
}

//======================================================================================================================
// A pass with two colour attachments, and a reader of each. The dump has to distinguish the extra
// from the primary, and to keep the two derived transitions apart, which is what a single "color
// attachment" line for both would hide.
TEST_CASE("a multi-attachment frame's dump matches its golden file", "[render][graph]") {
    FakeTexture sceneColor{64};
    FakeTexture motionVectors{64};
    FakeTexture sceneDepth{64};
    FakeTexture displayColor{64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture motion =
        graph.importTexture(motionVectors, rhi::Format::RG16Float, "lmx.render.motionVectors");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rhi::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.render.displayColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = color};
    scene.extraColor.push_back(ColorAttachment{.handle = motion});
    scene.depth = DepthAttachment{.handle = depth};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(color));
    displayPass.textureReads.push_back(nextVersion(motion));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-mrt.txt");
}

//======================================================================================================================
// A pass with three colour attachments, which is the shape the temporal frame's scene pass declares
// once motion and the reactive weight join the picture. Each extra has to be numbered by its own
// attachment index rather than collapsed with the one before it.
TEST_CASE("a third colour attachment is numbered in the dump", "[render][graph]") {
    FakeTexture sceneColor{64};
    FakeTexture motionVectors{64};
    FakeTexture reactive{64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture motion =
        graph.importTexture(motionVectors, rhi::Format::RG16Float, "lmx.render.motion");
    const GraphTexture weight =
        graph.importTexture(reactive, rhi::Format::R8Unorm, "lmx.render.reactive");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = color};
    scene.extraColor.push_back(ColorAttachment{.handle = motion});
    scene.extraColor.push_back(ColorAttachment{.handle = weight});
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(weight));

    const auto record = graph.compileFrame(4);
    REQUIRE(record.has_value());
    const std::string dump = dumpCompiledFrame(*record);
    INFO(dump);
    REQUIRE(dump.find("color attachment[1] r1 v0") != std::string::npos);
    REQUIRE(dump.find("color attachment[2] r2 v0") != std::string::npos);
}
