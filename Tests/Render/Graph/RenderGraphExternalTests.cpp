#include "Support/RenderGraphTestSupport.h"

//======================================================================================================================
TEST_CASE("external passes order inputs and outputs without opening an RHI scope",
          "[render][graph][external]") {
    auto input = textureFixture(64, 64, "input");
    auto output = textureFixture(64, 64, "output");
    auto display = textureFixture(64, 64, "display");
    RenderGraph graph;
    const auto a = graph.importTexture(formatted(input, rojoRHI::Format::RGBA16Float), "input");
    const auto b = graph.importTexture(formatted(output, rojoRHI::Format::RGBA16Float), "output");
    const auto c = graph.importTexture(formatted(display, rojoRHI::Format::BGRA8Unorm), "display");

    graph.addComputePass("pack", {.textureWrites = {a}}, kNoWork);
    graph.addExternalPass("vendor", {.textureReads = {nextVersion(a)}, .textureWrites = {b}},
                          kNoWork);
    graph.addPass("display",
                  {.textureReads = {nextVersion(b)}, .color = ColorAttachment{.handle = c}},
                  kNoWork);
    graph.presentTexture(nextVersion(c));

    const auto frame = lowered(graph, 3);
    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "scope pack",
                                                          "run pack", "end scope", "run vendor",
                                                          "begin display", "run display", "end",
                                                          "end scope"});
    const auto& record = frame.record;
    REQUIRE(record.debug.passes[1].kind == PassKind::External);
    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].textureFrom == lmx::render::TextureUse::StorageWrite);
    REQUIRE(record.debug.transitions[0].textureTo == lmx::render::TextureUse::ExternalRead);
    REQUIRE(record.debug.transitions[1].textureFrom == lmx::render::TextureUse::ExternalWrite);
    REQUIRE(record.debug.transitions[1].textureTo == lmx::render::TextureUse::ShaderRead);
    const auto resources = graph.passResources(frame, 1);
    REQUIRE(resources.texture(nextVersion(a)).texture == input.texture);
    REQUIRE(resources.texture(b).texture == output.texture);
    const auto wrongVersion = abortDiagnostic([&] { (void)resources.texture(a); });
    REQUIRE(wrongVersion.contains(
        "pass 'vendor' resolves texture 'input' version 0, which it did not declare"));

    const auto barriers = textureBarriers(frame);
    const auto inputRead = std::ranges::find_if(barriers, [&](const auto& barrier) {
        return barrier.resource == a.index && barrier.to.layout == rojoRHI::Layout::ShaderRead;
    });
    REQUIRE(inputRead != barriers.end());
    REQUIRE(inputRead->from.access == rojoRHI::Access::ShaderWrite);
    REQUIRE(inputRead->to.access == rojoRHI::Access::ShaderRead);
    const auto outputRead = std::ranges::find_if(barriers, [&](const auto& barrier) {
        return barrier.resource == b.index && barrier.to.layout == rojoRHI::Layout::ShaderRead;
    });
    REQUIRE(outputRead != barriers.end());
    REQUIRE(outputRead->from.access == rojoRHI::Access::ShaderWrite);
    REQUIRE(outputRead->to.access == rojoRHI::Access::ShaderRead);
}

//======================================================================================================================
TEST_CASE("external terminal uses preserve cross-frame hazards", "[render][graph][external]") {
    auto texture = textureFixture(64, 64);
    auto output = textureFixture(64, 64);

    SECTION("a prior external write is visible to a sampled read") {
        TextureStateTable states;
        states.setLogicalUse(texture.texture, lmx::render::TextureUse::ExternalWrite);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto input = graph.importPersistentTexture(
            formatted(texture, rojoRHI::Format::RGBA16Float), "history");
        const auto result =
            graph.importTexture(formatted(output, rojoRHI::Format::RGBA16Float), "output");
        graph.addComputePass("sample", {.shaderTextureReads = {input}, .textureWrites = {result}},
                             kNoWork);
        graph.exportTexture(nextVersion(result));
        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::ExternalWrite);
        REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::ShaderRead);
    }
    SECTION("a prior external read is ordered before a new write") {
        TextureStateTable states;
        states.setLogicalUse(texture.texture, lmx::render::TextureUse::ExternalRead);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto input = graph.importPersistentTexture(
            formatted(texture, rojoRHI::Format::RGBA16Float), "history");
        graph.addComputePass("overwrite", {.textureWrites = {input}}, kNoWork);
        graph.exportTexture(nextVersion(input));
        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::ExternalRead);
        REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::StorageWrite);
    }
    SECTION("a prior external read needs no barrier for another read") {
        TextureStateTable states;
        states.setLogicalUse(texture.texture, lmx::render::TextureUse::ExternalRead);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const auto input = graph.importPersistentTexture(
            formatted(texture, rojoRHI::Format::RGBA16Float), "history");
        const auto result =
            graph.importTexture(formatted(output, rojoRHI::Format::RGBA16Float), "output");
        graph.addExternalPass("vendor", {.textureReads = {input}, .textureWrites = {result}},
                              kNoWork);
        graph.exportTexture(nextVersion(result));
        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.empty());
    }
}

//======================================================================================================================
TEST_CASE("external declarations enforce texture versions and ranges",
          "[render][graph][external]") {
    auto texture = textureFixture(64, 64, "mips", 2);
    RenderGraph graph;
    const auto input =
        graph.importTexture(formatted(texture, rojoRHI::Format::RGBA16Float), "mips");

    SECTION("overlapping read and write") {
        graph.addExternalPass("vendor", {.textureReads = {input}, .textureWrites = {input}},
                              kNoWork);
    }
    SECTION("read before write") {
        graph.addExternalPass("vendor", {.textureReads = {nextVersion(input)}}, kNoWork);
    }
    SECTION("invalid subresource") {
        graph.addExternalPass("vendor", {.textureReads = {{input, {.baseMip = 2}}}}, kNoWork);
    }
    SECTION("multiple writers") {
        graph.addExternalPass("vendor", {.textureWrites = {input}}, kNoWork);
        graph.addComputePass("compute", {.textureWrites = {input}}, kNoWork);
    }
    const auto record = graph.compileFrame(1);
    REQUIRE_FALSE(record.has_value());
    REQUIRE(record.error().message.contains("vendor"));
}

//======================================================================================================================
TEST_CASE("external consumers bound transient lifetimes and unused work is culled",
          "[render][graph][external]") {
    FakeTransientBackend backend;
    TransientPool pool{backend};
    auto output = textureFixture(64, 64);
    RenderGraph graph{pool};
    const auto packed = graph.createTexture(kTransientColor, "packed");
    const auto unused = graph.createTexture(kTransientColor, "unused");
    const auto result =
        graph.importTexture(formatted(output, rojoRHI::Format::RGBA16Float), "result");
    graph.addComputePass("pack", {.textureWrites = {packed}}, kNoWork);
    graph.addExternalPass(
        "vendor", {.textureReads = {nextVersion(packed)}, .textureWrites = {result}}, kNoWork);
    graph.addExternalPass("unused", {.textureWrites = {unused}}, kNoWork);
    graph.exportTexture(nextVersion(result));
    const auto record = graph.compileFrame(1);
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0, 1});
    REQUIRE(record->debug.passes[2].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(record->debug.transients[0].used);
    REQUIRE(record->debug.transients[0].firstPass == 0);
    REQUIRE(record->debug.transients[0].lastPass == 1);
    REQUIRE_FALSE(record->debug.transients[1].used);
    REQUIRE(record->debug.memory.requested == FakeTransientBackend::kTextureAlignment);
}
