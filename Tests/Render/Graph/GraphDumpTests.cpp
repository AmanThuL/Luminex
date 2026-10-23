#include <catch2/catch_test_macros.hpp>

#include "Render/Graph/GraphDump.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/ResourceUse.h"
#include "Render/Graph/TransientPool.h"
#include "Support/GoldenFile.h"
#include "Support/TransientBackendTestSupport.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {

const ExecuteFn kNoWork = [](const PassResources&) {};

using lmx::test::goldenPath;
using lmx::test::requireMatchesGolden;

} // namespace

//======================================================================================================================
// The shape of the shipped frame: a depth-only pass feeding a scene pass feeding a display pass,
// with the derived transitions between them. It is the case that would notice a change to the
// wording, the ordering, or the set of things a dump reports.
TEST_CASE("a frame's dump matches its golden file", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    FakeTexture sceneColor{64, 64};
    FakeTexture sceneDepth{64, 64};
    FakeTexture displayColor{64, 64};
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "lmx.render.shadowMap");
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rojoRHI::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rojoRHI::Format::BGRA8Unorm, "lmx.render.displayColor");

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
    FakeTexture chain{64, 64, "", 4};
    FakeTexture output{64, 64};
    FakeBuffer histogram{1024};
    FakeBuffer staging{1024};
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "lmx.bloomChain");
    const GraphTexture target =
        graph.importTexture(output, rojoRHI::Format::BGRA8Unorm, "lmx.composite");
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
    FakeTexture sceneColor{64, 64};
    FakeTexture displayColor{64, 64};
    FakeTexture unused{64, 64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.sceneColor");
    const GraphTexture display =
        graph.importTexture(displayColor, rojoRHI::Format::BGRA8Unorm, "lmx.displayColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rojoRHI::Format::BGRA8Unorm, "lmx.debugOverlay");

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
    FakeTransientBackend backend;
    TransientPool pool(backend);
    FakeTexture midTarget{64, 64};
    FakeTexture displayColor{64, 64};
    RenderGraph graph(pool);
    const GraphTexture scene = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rojoRHI::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.sceneColor");
    const GraphTexture bloom = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rojoRHI::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.bloom");
    const GraphTexture unused = graph.createTexture({.width = 64,
                                                     .height = 64,
                                                     .format = rojoRHI::Format::RGBA16Float,
                                                     .renderTarget = true,
                                                     .sampled = true},
                                                    "lmx.transient.unused");
    const GraphTexture mid = graph.importTexture(midTarget, rojoRHI::Format::BGRA8Unorm, "lmx.mid");
    const GraphTexture display =
        graph.importTexture(displayColor, rojoRHI::Format::BGRA8Unorm, "lmx.displayColor");

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
    FakeTexture sceneColor{64, 64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.sceneColor");

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
    FakeTexture sceneColor{64, 64};
    FakeTexture motionVectors{64, 64};
    FakeTexture sceneDepth{64, 64};
    FakeTexture displayColor{64, 64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture motion =
        graph.importTexture(motionVectors, rojoRHI::Format::RG16Float, "lmx.render.motionVectors");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rojoRHI::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rojoRHI::Format::BGRA8Unorm, "lmx.render.displayColor");

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
    FakeTexture sceneColor{64, 64};
    FakeTexture motionVectors{64, 64};
    FakeTexture reactive{64, 64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture motion =
        graph.importTexture(motionVectors, rojoRHI::Format::RG16Float, "lmx.render.motion");
    const GraphTexture weight =
        graph.importTexture(reactive, rojoRHI::Format::R8Unorm, "lmx.render.reactive");

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

//======================================================================================================================
// A frame that renders part of its attachments: the pass that declared a sub-rectangle says so, and
// the one that renders the whole target still prints exactly what it always has, so a dump reader
// can tell a scaled frame from a full-resolution one.
TEST_CASE("a render area appears in the dump", "[render][graph]") {
    FakeTexture sceneColor{64, 64};
    FakeTexture sceneDepth{64, 64};
    FakeTexture displayColor{64, 64};
    RenderGraph graph;
    const GraphTexture color =
        graph.importTexture(sceneColor, rojoRHI::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rojoRHI::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rojoRHI::Format::BGRA8Unorm, "lmx.render.displayColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = color};
    scene.depth = DepthAttachment{.handle = depth};
    scene.renderAreaWidth = 32;
    scene.renderAreaHeight = 16;
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(color));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(5);
    REQUIRE(record.has_value());
    requireMatchesGolden(dumpCompiledFrame(*record), "frame-render-area.txt");
}

//======================================================================================================================
TEST_CASE("external graph dumps name the operation and both external uses",
          "[render][graph][external]") {
    FakeTexture input{64, 64};
    FakeTexture output{64, 64};
    TextureStateTable states;
    states.set(input, lmx::render::TextureUse::StorageWrite);
    RenderGraph graph;
    graph.bindTextureStates(states);
    const auto source = graph.importPersistentTexture(input, rojoRHI::Format::R16Float, "exposure");
    const auto target = graph.importTexture(output, rojoRHI::Format::RGBA16Float, "history");
    graph.addExternalPass("vendor", {.textureReads = {source}, .textureWrites = {target}}, kNoWork);
    graph.addComputePass("consume",
                         {.shaderTextureReads = {nextVersion(target)}, .textureWrites = {source}},
                         kNoWork);
    graph.exportTexture(nextVersion(source));
    const auto record = graph.compileFrame(1);
    REQUIRE(record.has_value());
    const std::string dump = dumpCompiledFrame(*record);
    REQUIRE(dump.contains("external \"vendor\""));
    REQUIRE(dump.contains("R16Float"));
    REQUIRE(dump.contains("StorageWrite -> ExternalRead"));
    REQUIRE(dump.contains("ExternalWrite -> ShaderRead"));
}

//======================================================================================================================
TEST_CASE("resource use names match the dump vocabulary", "[render][graph]") {
    REQUIRE(textureUseName(lmx::render::TextureUse::RenderTarget) == "RenderTarget");
    REQUIRE(textureUseName(lmx::render::TextureUse::ShaderRead) == "ShaderRead");
    REQUIRE(textureUseName(lmx::render::TextureUse::StorageRead) == "StorageRead");
    REQUIRE(textureUseName(lmx::render::TextureUse::StorageWrite) == "StorageWrite");
    REQUIRE(textureUseName(lmx::render::TextureUse::CopySource) == "CopySource");
    REQUIRE(textureUseName(lmx::render::TextureUse::CopyDestination) == "CopyDestination");
    REQUIRE(textureUseName(lmx::render::TextureUse::ExternalRead) == "ExternalRead");
    REQUIRE(textureUseName(lmx::render::TextureUse::ExternalWrite) == "ExternalWrite");
    REQUIRE(bufferUseName(lmx::render::BufferUse::ShaderRead) == "ShaderRead");
    REQUIRE(bufferUseName(lmx::render::BufferUse::StorageRead) == "StorageRead");
    REQUIRE(bufferUseName(lmx::render::BufferUse::StorageWrite) == "StorageWrite");
    REQUIRE(bufferUseName(lmx::render::BufferUse::CopySource) == "CopySource");
    REQUIRE(bufferUseName(lmx::render::BufferUse::CopyDestination) == "CopyDestination");
    REQUIRE(bufferUseName(lmx::render::BufferUse::IndirectArgument) == "IndirectArgument");
}
