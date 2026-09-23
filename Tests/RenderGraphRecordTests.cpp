#include "Support/RenderGraphTestSupport.h"

//======================================================================================================================
// The record is the frame as compilation saw it: what was imported, what each pass declared, and in
// which order it runs. An observer reads it without the graph, so everything it needs has to be in
// it -- names included, since a resource index alone names nothing to a reader.
TEST_CASE("compileFrame records the declarations it compiled", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture color{64, 64, "sceneColor"};
    FakeBuffer storage{256, "instances"};
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphBuffer instances = graph.importBuffer(storage, "instances");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.bufferReads.push_back(instances);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const auto record = graph.compileFrame(42);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->frameId == 42);

    REQUIRE(record->debug.resources.size() == 3);
    REQUIRE(record->debug.resources[0].name == "shadowMap");
    REQUIRE(record->debug.resources[0].kind == GraphResourceKind::Texture);
    REQUIRE(record->debug.resources[0].format == rojoRHI::Format::D32Float);
    REQUIRE(record->debug.resources[2].name == "instances");
    REQUIRE(record->debug.resources[2].kind == GraphResourceKind::Buffer);

    // Declaration order, not schedule order: the record describes the frame that was declared, and
    // the schedule is a separate answer about it.
    REQUIRE(record->debug.passes.size() == 2);
    REQUIRE(record->debug.passes[0].label == "lmx.pass.scene");
    REQUIRE(record->debug.passes[0].kind == PassKind::Raster);
    REQUIRE(record->debug.passes[1].label == "lmx.pass.shadow");

    const std::vector<DebugUse>& uses = record->debug.passes[0].uses;
    REQUIRE(uses.size() == 3);
    REQUIRE(uses[0].resource == shadow.index);
    REQUIRE(uses[0].version == 1);
    REQUIRE(uses[0].role == UseRole::Read);
    REQUIRE(uses[1].resource == instances.index);
    REQUIRE(uses[1].role == UseRole::Read);
    REQUIRE(uses[2].resource == sceneColor.index);
    REQUIRE(uses[2].role == UseRole::ColorAttachment);

    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{1, 0});
}

//======================================================================================================================
// The record's transitions are the barriers, not a second opinion about them: execute() emits what
// compilation recorded, so an inspector reading the record and a capture of the frame describe the
// same synchronisation.
TEST_CASE("execute emits exactly the transitions the record lists", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture color{64, 64, "sceneColor"};
    FakeBuffer storage{256, "histogram"};
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphBuffer bins = graph.importBuffer(storage, "histogram");

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.pass.clear", clear, kNoWork);

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.bufferReads.push_back(nextVersion(bins));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 7);

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 2);
    REQUIRE(record.debug.transitions[0].kind == GraphResourceKind::Texture);
    REQUIRE(record.debug.transitions[0].resource == shadow.index);
    REQUIRE(record.debug.transitions[0].textureFrom == rojoRHI::TextureUse::RenderTarget);
    REQUIRE(record.debug.transitions[0].textureTo == rojoRHI::TextureUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].kind == GraphResourceKind::Buffer);
    REQUIRE(record.debug.transitions[1].resource == bins.index);
    REQUIRE(record.debug.transitions[1].bufferFrom == rojoRHI::BufferUse::CopyDestination);
    REQUIRE(record.debug.transitions[1].bufferTo == rojoRHI::BufferUse::ShaderRead);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin lmx.pass.shadow", "end", "begin copy lmx.pass.clear",
                                     "end copy", "barrier shadowMap RenderTarget->ShaderRead",
                                     "barrier histogram CopyDestination->ShaderRead",
                                     "begin lmx.pass.scene", "end"});
}

//======================================================================================================================
// The frame number stamps the record and takes no part in compiling it, so two frames declaring the
// same passes differ in nothing else -- which is what lets a dump be compared across runs.
TEST_CASE("the frame id changes nothing else about a record", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const auto first = graph.compileFrame(1);
    const auto second = graph.compileFrame(9001);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->frameId != second->frameId);
    REQUIRE(first->debug.schedule.passes == second->debug.schedule.passes);
    REQUIRE(first->debug.passes.size() == second->debug.passes.size());
    REQUIRE(first->debug.passes[0].label == second->debug.passes[0].label);
    REQUIRE(first->debug.transitions.size() == second->debug.transitions.size());
}

//======================================================================================================================
// Nothing roots this frame, so nothing in it is asked for. The passes are valid, and that is the
// point: culling is about what the frame is for, not about whether it is well formed.
TEST_CASE("a graph with no sink schedules nothing", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes.empty());
    REQUIRE(record->debug.passes[0].cullReason == CullReason::NoSinkReachesIt);
}

//======================================================================================================================
// The frame that says what culling is for: a chain that reaches the export survives whole, and a
// chain that does not is dropped whole. Both reasons appear, and the record keeps the culled passes
// in declaration order so an observer can see what was left out and why.
TEST_CASE("only the passes a sink reaches are scheduled", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture displayed{64, 64, "displayColor"};
    FakeTexture unused{64, 64, "unusedTarget"};
    FakeBuffer probe{256, "probe"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture displayColor =
        graph.importTexture(displayed, rojoRHI::Format::BGRA8Unorm, "displayColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rojoRHI::Format::BGRA8Unorm, "unusedTarget");
    const GraphBuffer stats = graph.importBuffer(probe, "probe");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    // Writes a target nothing else names: valid work with no consumer and no sink.
    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned, kNoWork);

    // Reads the scene colour and writes nothing at all, so no sink could name its output even in
    // principle -- a different mistake from the orphan's, and recorded as one.
    ComputePassDesc observer;
    observer.textureReads.push_back(nextVersion(sceneColor));
    observer.bufferReads.push_back(stats);
    graph.addComputePass("lmx.pass.observer", observer, kNoWork);

    PassDesc display;
    display.textureReads.push_back(nextVersion(sceneColor));
    display.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", display, kNoWork);

    graph.exportTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(3);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0, 3});
    REQUIRE_FALSE(record->debug.passes[0].cullReason.has_value());
    REQUIRE(record->debug.passes[1].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(record->debug.passes[2].cullReason == CullReason::ProducesNothing);
    REQUIRE_FALSE(record->debug.passes[3].cullReason.has_value());

    // The culled passes are still declared, so the record still describes them in full.
    REQUIRE(record->debug.passes.size() == 4);
    REQUIRE(record->debug.passes[2].label == "lmx.pass.observer");
    REQUIRE(record->debug.passes[2].uses.size() == 2);

    REQUIRE(record->debug.sinks.size() == 1);
    REQUIRE(record->debug.sinks[0].kind == SinkKind::Export);
    REQUIRE(record->debug.sinks[0].resource == displayColor.index);
    REQUIRE(record->debug.sinks[0].version == 1);
}

//======================================================================================================================
// Culling is a function of the declarations, so the same declarations answer the same way whichever
// order the sinks were declared in and however many times compilation is asked.
TEST_CASE("culling answers the same way every time", "[render][graph]") {
    const auto build = [](RenderGraph& graph, FakeTexture& first, FakeTexture& second,
                          bool exportFirst) {
        const GraphTexture a = graph.importTexture(first, rojoRHI::Format::BGRA8Unorm, "a");
        const GraphTexture b = graph.importTexture(second, rojoRHI::Format::BGRA8Unorm, "b");
        PassDesc writeA;
        writeA.color = ColorAttachment{.handle = a};
        graph.addPass("lmx.pass.a", writeA, kNoWork);
        PassDesc writeB;
        writeB.color = ColorAttachment{.handle = b};
        graph.addPass("lmx.pass.b", writeB, kNoWork);
        if (exportFirst) {
            graph.exportTexture(nextVersion(a));
            graph.exportTexture(nextVersion(b));
        } else {
            graph.exportTexture(nextVersion(b));
            graph.exportTexture(nextVersion(a));
        }
    };

    FakeTexture first{64, 64, "a"};
    FakeTexture second{64, 64, "b"};

    RenderGraph forward;
    build(forward, first, second, true);
    RenderGraph reversed;
    build(reversed, first, second, false);

    const auto once = forward.compileFrame(1);
    const auto twice = forward.compileFrame(1);
    const auto other = reversed.compileFrame(1);
    REQUIRE(once.has_value());
    REQUIRE(twice.has_value());
    REQUIRE(other.has_value());

    REQUIRE(once->debug.schedule.passes == std::vector<uint32_t>{0, 1});
    REQUIRE(twice->debug.schedule.passes == once->debug.schedule.passes);
    REQUIRE(other->debug.schedule.passes == once->debug.schedule.passes);
}

//======================================================================================================================
// A culled pass emits nothing at all -- not the scope, not the body, and not the barrier a read of
// its output would otherwise have justified.
TEST_CASE("execute runs none of a culled pass", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture unused{64, 64, "unusedTarget"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rojoRHI::Format::BGRA8Unorm, "unusedTarget");

    bool orphanRan = false;
    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned,
                  [&orphanRan](const PassResources&) { orphanRan = true; });

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(orphan));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    // Roots the orphan's own output rather than the scene's, so the scene pass is the culled one
    // and the transition its read would have justified goes with it.
    graph.exportTexture(nextVersion(orphan));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(orphanRan);
    REQUIRE(commands.events == std::vector<std::string>{"begin lmx.pass.orphan", "end"});
}

//======================================================================================================================
// Presentation and readback root work exactly as an export does; they are separate kinds because
// what a frame does with a result is part of what the frame declared, not a detail of the export.
TEST_CASE("presentation and readback root work like an export", "[render][graph]") {
    FakeTexture drawable{64, 64, "drawable"};
    FakeBuffer histogram{1024, "histogram"};
    RenderGraph graph;
    const GraphTexture backbuffer =
        graph.importTexture(drawable, rojoRHI::Format::BGRA8Unorm, "drawable");
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    PassDesc ui;
    ui.color = ColorAttachment{.handle = backbuffer};
    graph.addPass("lmx.pass.ui", ui, kNoWork);

    ComputePassDesc metering;
    metering.bufferWrites.push_back(bins);
    graph.addComputePass("lmx.pass.metering", metering, kNoWork);

    graph.presentTexture(nextVersion(backbuffer));
    graph.readbackBuffer(nextVersion(bins));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0, 1});
    REQUIRE(record->debug.sinks[0].kind == SinkKind::Present);
    REQUIRE(record->debug.sinks[0].resourceKind == GraphResourceKind::Texture);
    REQUIRE(record->debug.sinks[1].kind == SinkKind::Readback);
    REQUIRE(record->debug.sinks[1].resourceKind == GraphResourceKind::Buffer);
}

//======================================================================================================================
// Every sink kind roots a version a pass produced, and says which kind of declaration it was: a
// presented drawable nothing drew into is as broken as an exported one, and the message has to name
// the declaration the caller actually made.
TEST_CASE("a sink naming a version no pass wrote is rejected", "[render][graph]") {
    FakeTexture drawable{64, 64, "drawable"};
    FakeBuffer histogram{1024, "histogram"};

    RenderGraph presented;
    const GraphTexture backbuffer =
        presented.importTexture(drawable, rojoRHI::Format::BGRA8Unorm, "drawable");
    presented.presentTexture(nextVersion(backbuffer));
    const auto presentFailure = presented.compile();
    REQUIRE_FALSE(presentFailure.has_value());
    REQUIRE(presentFailure.error().message.contains("presented texture 'drawable'"));
    REQUIRE(presentFailure.error().message.contains("not written by any pass"));

    RenderGraph exported;
    const GraphBuffer bins = exported.importBuffer(histogram, "histogram");
    exported.exportBuffer(nextVersion(bins));
    const auto exportFailure = exported.compile();
    REQUIRE_FALSE(exportFailure.has_value());
    REQUIRE(exportFailure.error().message.contains("exported buffer 'histogram'"));
}

//======================================================================================================================
// Validation covers every declared pass, not only the scheduled ones: a pass that would never run
// is still a pass the caller wrote down wrong, and reporting it only once a sink happens to reach
// it would make the failure depend on unrelated declarations.
TEST_CASE("a culled pass is still validated", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    // Unreachable from the export, and mis-declared: a depth texture in the colour slot.
    PassDesc broken;
    broken.color = ColorAttachment{.handle = shadow};
    graph.addPass("lmx.pass.broken", broken, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.broken"));
    REQUIRE(schedule.error().message.contains("D32Float"));
}
