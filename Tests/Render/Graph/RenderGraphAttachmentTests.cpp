#include "Support/RenderGraphTestSupport.h"

//======================================================================================================================
// The extra attachment is a write like the primary one: it declares its own attachment use, and the
// version it produces is what a later pass -- or, here, a sink -- names.
TEST_CASE("an extra color attachment produces the next version of its resource",
          "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(motionVectors));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0});

    const std::vector<DebugUse>& uses = record->debug.passes[0].uses;
    REQUIRE(uses.size() == 2);
    REQUIRE(uses[0].role == UseRole::ColorAttachment);
    REQUIRE(uses[0].resource == sceneColor.index);
    REQUIRE(uses[1].role == UseRole::ColorAttachment);
    REQUIRE(uses[1].resource == motionVectors.index);
    REQUIRE(uses[1].version == 0);
}

//======================================================================================================================
// The pass body resolves an extra attachment exactly as it resolves the primary: declaring it is
// what makes it resolvable.
TEST_CASE("PassResources resolves an extra color attachment", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const PassResources resources = graph.passResources(0);
    const GraphResult<rojoRHI::Texture*> resolved = resources.texture(motionVectors);
    INFO(errorOf(resolved));
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == &motion);
}

//======================================================================================================================
// execute() carries each extra's own target, load action, and clear value into the RHI descriptor,
// in the attachment order the pass declared them.
TEST_CASE("execute fills the RHI descriptor's extra color attachments", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    RecordingCommandList commands;

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(
        ColorAttachment{.handle = motionVectors, .clearColor = {-1.0f, 2.0f, 0.0f, 0.0f}});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(motionVectors));
    graph.execute(commands, 1);

    REQUIRE(commands.passes.size() == 1);
    REQUIRE(commands.passes[0].colorTarget == &color);
    REQUIRE(commands.passes[0].extraColorCount == 1);
    REQUIRE(commands.passes[0].extraColor[0].target == &motion);
    REQUIRE(commands.passes[0].extraColor[0].clear);
    REQUIRE(commands.passes[0].extraColor[0].clearColor[0] == -1.0f);
    REQUIRE(commands.passes[0].extraColor[0].clearColor[1] == 2.0f);
}

//======================================================================================================================
// An extra that keeps its contents is a load rather than a clear, and the flag the RHI carries is
// the only thing that says which.
TEST_CASE("an extra color attachment that loads clears nothing", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    RecordingCommandList commands;

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors, .load = LoadOp::Load});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(motionVectors));
    graph.execute(commands, 1);

    REQUIRE(commands.passes.size() == 1);
    REQUIRE(commands.passes[0].extraColorCount == 1);
    REQUIRE_FALSE(commands.passes[0].extraColor[0].clear);
}

//======================================================================================================================
// The RHI binds a fixed number of attachments past the primary, and a declaration past it is a
// frame asking for hardware that is not there.
TEST_CASE("more extra color attachments than the RHI binds are rejected", "[render][graph]") {
    // One past what the RHI binds, whatever that maximum is, so the case tracks the constant rather
    // than a count written out beside it. A deque keeps each texture's address stable while the
    // graph borrows it.
    constexpr uint32_t kExtras = rojoRHI::kMaxExtraColorTargets + 1;
    FakeTexture color{64, 64, "sceneColor"};
    std::deque<FakeTexture> extras;
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    for (uint32_t index = 0; index < kExtras; ++index) {
        const std::string name = "extra" + std::to_string(index);
        extras.emplace_back(64, 64, name);
        scene.extraColor.push_back(ColorAttachment{
            .handle = graph.importTexture(extras.back(), rojoRHI::Format::RG16Float, name)});
    }
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains(std::to_string(kExtras)));
    REQUIRE(schedule.error().message.contains(std::to_string(rojoRHI::kMaxExtraColorTargets)));
}

//======================================================================================================================
TEST_CASE("a depth format in an extra color attachment is rejected", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture depth{64, 64, "sceneDepth"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth =
        graph.importTexture(depth, rojoRHI::Format::D32Float, "sceneDepth");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = sceneDepth});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("extra color attachment"));
    REQUIRE(schedule.error().message.contains("D32Float"));
}

//======================================================================================================================
// The hardware writes every attachment through one rasterisation, so an extra of another extent is
// a pass whose fragments have nowhere to land.
TEST_CASE("an extra color attachment of another extent is rejected", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{32, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("extent"));
    REQUIRE(schedule.error().message.contains("64x64"));
    REQUIRE(schedule.error().message.contains("32x64"));
}

//======================================================================================================================
// Extras are attachments one and up, so a pass with no attachment zero cannot have them.
TEST_CASE("an extra color attachment without a primary is rejected", "[render][graph]") {
    FakeTexture depth{64, 64, "sceneDepth"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneDepth =
        graph.importTexture(depth, rojoRHI::Format::D32Float, "sceneDepth");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.depth = DepthAttachment{.handle = sceneDepth, .store = StoreOp::Store};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("extra color attachment"));
    REQUIRE(schedule.error().message.contains("no color attachment"));
}

//======================================================================================================================
// Two attachments over one texture would have the pass write the same memory twice in one
// rasterisation, and no version arithmetic can say what the result holds.
TEST_CASE("one texture as both the primary and an extra attachment is rejected",
          "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = nextVersion(sceneColor)});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("sceneColor"));
    REQUIRE(schedule.error().message.contains("twice"));
}

//======================================================================================================================
TEST_CASE("one texture as two extra attachments is rejected", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    scene.extraColor.push_back(ColorAttachment{.handle = nextVersion(motionVectors)});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("motionVectors"));
    REQUIRE(schedule.error().message.contains("twice"));
}

//======================================================================================================================
// Culling is rooted in sinks and in nothing else: an extra attachment is another output of the
// pass, not a reason to keep it.
TEST_CASE("an extra color attachment no sink reaches keeps nothing alive", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    FakeTexture display{64, 64, "displayColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");
    const GraphTexture displayColor =
        graph.importTexture(display, rojoRHI::Format::BGRA8Unorm, "displayColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc displayPass;
    displayPass.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{1});
    REQUIRE(record->debug.passes[0].cullReason == CullReason::NoSinkReachesIt);
}

//======================================================================================================================
// The other half of the same rule: a sink on the extra's version is a sink on the pass, so the pass
// survives on the strength of its second output alone.
TEST_CASE("a sink on an extra attachment's version keeps its pass alive", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(motionVectors));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0});
}

//======================================================================================================================
// The motion-vector format the RHI renders into. The graph's own predicate has to agree with the
// RHI's, or a target the device accepts is refused a pass before it ever reaches one.
TEST_CASE("RG16Float is accepted as a color attachment", "[render][graph]") {
    FakeTexture motion{64, 64, "motionVectors"};
    RenderGraph graph;
    const GraphTexture motionVectors =
        graph.importTexture(motion, rojoRHI::Format::RG16Float, "motionVectors");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = motionVectors};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(motionVectors));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
}

//======================================================================================================================
// The single-channel mask format, for the same reason: the graph's predicate and the RHI's have to
// answer alike, or a target the device accepts is refused a pass.
TEST_CASE("R8Unorm is accepted as a color attachment", "[render][graph]") {
    FakeTexture reactive{64, 64, "reactive"};
    RenderGraph graph;
    const GraphTexture mask = graph.importTexture(reactive, rojoRHI::Format::R8Unorm, "reactive");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = mask};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(mask));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
}

//======================================================================================================================
// The transient rule reaches every attachment: an extra that loads consumes contents the frame
// never produced just as the primary would.
TEST_CASE("a transient loaded as an extra color attachment fails to compile", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph(pool);
    const GraphTexture scratch = graph.createTexture(kTransientColor, "lmx.transient.scratch");
    const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

    PassDesc load;
    load.color = ColorAttachment{.handle = out};
    load.extraColor.push_back(ColorAttachment{.handle = scratch, .load = LoadOp::Load});
    graph.addPass("lmx.pass.load", load, kNoWork);

    graph.exportTexture(nextVersion(out));

    const auto record = graph.compileFrame(1);
    REQUIRE_FALSE(record.has_value());
    REQUIRE(record.error().message ==
            "pass 'lmx.pass.load' loads transient texture 'lmx.transient.scratch' version 0 as its "
            "extra color attachment 0, whose contents no pass produced: a transient holds nothing "
            "until a pass writes it");
}

//======================================================================================================================
// A render area with one side set would reach the RHI as a viewport of zero area, rasterising
// nothing at all rather than the sub-rectangle the pass meant to draw into.
TEST_CASE("a half-set render area is rejected", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.renderAreaWidth = 32;
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("render area"));
    REQUIRE(schedule.error().message.contains("32x0"));
}

//======================================================================================================================
// The area is a sub-rectangle of what the pass renders into, so one larger than an attachment is a
// pass asking to rasterise texels that attachment does not have.
TEST_CASE("a render area larger than an attachment is rejected", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture depth{64, 64, "sceneDepth"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth =
        graph.importTexture(depth, rojoRHI::Format::D32Float, "sceneDepth");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.depth = DepthAttachment{.handle = sceneDepth};
    scene.renderAreaWidth = 32;
    scene.renderAreaHeight = 96;
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message ==
            "pass 'lmx.pass.scene' render area 32x96 exceeds attachment 'sceneColor' 64x64");
}

//======================================================================================================================
// The record is what a reader of a compiled frame has, so an area a pass declared has to be in it;
// a pass that declared none carries the zero pair that means the whole attachment.
TEST_CASE("a compiled record carries the render area a pass declared", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture display{64, 64, "displayColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::RGBA16Float, "sceneColor");
    const GraphTexture displayColor =
        graph.importTexture(display, rojoRHI::Format::BGRA8Unorm, "displayColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.renderAreaWidth = 32;
    scene.renderAreaHeight = 16;
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(sceneColor));
    displayPass.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.passes.size() == 2);
    REQUIRE(record->debug.passes[0].renderAreaWidth == 32);
    REQUIRE(record->debug.passes[0].renderAreaHeight == 16);
    REQUIRE(record->debug.passes[1].renderAreaWidth == 0);
    REQUIRE(record->debug.passes[1].renderAreaHeight == 0);
}

//======================================================================================================================
// The declaration only means anything if the pass the backend begins carries it, so execution has
// to hand the pair to the RHI descriptor unchanged.
TEST_CASE("execute fills the RHI descriptor's render area", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    RecordingCommandList commands;

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.renderAreaWidth = 32;
    scene.renderAreaHeight = 16;
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));
    graph.execute(commands, 1);

    REQUIRE(commands.passes.size() == 1);
    REQUIRE(commands.passes[0].renderAreaWidth == 32);
    REQUIRE(commands.passes[0].renderAreaHeight == 16);
}

//======================================================================================================================
// A pass that declares no area renders the whole attachment, which the RHI spells as the zero pair.
TEST_CASE("execute leaves an undeclared render area zero", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    RecordingCommandList commands;

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));
    graph.execute(commands, 1);

    REQUIRE(commands.passes.size() == 1);
    REQUIRE(commands.passes[0].renderAreaWidth == 0);
    REQUIRE(commands.passes[0].renderAreaHeight == 0);
}
