#include "Support/RenderGraphTestSupport.h"

//======================================================================================================================
TEST_CASE("external passes order inputs and outputs without opening an RHI scope",
          "[render][graph][external]") {
    FakeTexture input{64, 64, "input"};
    FakeTexture output{64, 64, "output"};
    FakeTexture display{64, 64, "display"};
    RecordingCommandList commands;
    FakeDevice::FakeTemporalScaler scaler;
    RenderGraph graph;
    const auto a = graph.importTexture(input, rojoRHI::Format::RGBA16Float, "input");
    const auto b = graph.importTexture(output, rojoRHI::Format::RGBA16Float, "output");
    const auto c = graph.importTexture(display, rojoRHI::Format::BGRA8Unorm, "display");

    graph.addComputePass("pack", {.textureWrites = {a}}, kNoWork);
    graph.addExternalPass("vendor", {.textureReads = {nextVersion(a)}, .textureWrites = {b}},
                          [&](const PassResources& resources) {
                              REQUIRE(resources.texture(nextVersion(a)).has_value());
                              REQUIRE(resources.texture(b).has_value());
                              REQUIRE_FALSE(resources.texture(a).has_value());
                              commands.temporalScale(scaler,
                                                     {.color = *resources.texture(nextVersion(a)),
                                                      .output = *resources.texture(b),
                                                      .inputContentWidth = 32,
                                                      .inputContentHeight = 32,
                                                      .reset = true,
                                                      .label = "vendor"});
                          });
    graph.addPass("display",
                  {.textureReads = {nextVersion(b)}, .color = ColorAttachment{.handle = c}},
                  kNoWork);
    graph.presentTexture(nextVersion(c));

    const auto record = graph.execute(commands, 3);
    REQUIRE(record.debug.passes[1].kind == PassKind::External);
    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].textureFrom == rojoRHI::TextureUse::StorageWrite);
    REQUIRE(record.debug.transitions[0].textureTo == rojoRHI::TextureUse::ExternalRead);
    REQUIRE(record.debug.transitions[1].textureFrom == rojoRHI::TextureUse::ExternalWrite);
    REQUIRE(record.debug.transitions[1].textureTo == rojoRHI::TextureUse::ShaderRead);
    REQUIRE(commands.events == std::vector<std::string>{"begin compute pack", "end compute",
                                                        "barrier input StorageWrite->ExternalRead",
                                                        "external vendor",
                                                        "barrier output ExternalWrite->ShaderRead",
                                                        "begin display", "end"});
    REQUIRE(commands.temporalScales.size() == 1);
    REQUIRE(commands.temporalScales.front().color == &input);
    REQUIRE(commands.temporalScales.front().output == &output);
    REQUIRE(commands.temporalScales.front().inputContentWidth == 32);
    REQUIRE(commands.temporalScales.front().reset);
}

//======================================================================================================================
TEST_CASE("external terminal uses preserve cross-frame hazards", "[render][graph][external]") {
    FakeTexture texture{64, 64};
    FakeTexture output{64, 64};

    SECTION("a prior external write is visible to a sampled read") {
        RenderGraph graph;
        const auto input = graph.importTexture(texture, rojoRHI::Format::RGBA16Float, "history",
                                               rojoRHI::TextureUse::ExternalWrite);
        const auto result = graph.importTexture(output, rojoRHI::Format::RGBA16Float, "output");
        graph.addComputePass("sample", {.shaderTextureReads = {input}, .textureWrites = {result}},
                             kNoWork);
        graph.exportTexture(nextVersion(result));
        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == rojoRHI::TextureUse::ExternalWrite);
        REQUIRE(record->debug.transitions[0].textureTo == rojoRHI::TextureUse::ShaderRead);
    }
    SECTION("a prior external read is ordered before a new write") {
        RenderGraph graph;
        const auto input = graph.importTexture(texture, rojoRHI::Format::RGBA16Float, "history",
                                               rojoRHI::TextureUse::ExternalRead);
        graph.addComputePass("overwrite", {.textureWrites = {input}}, kNoWork);
        graph.exportTexture(nextVersion(input));
        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == rojoRHI::TextureUse::ExternalRead);
        REQUIRE(record->debug.transitions[0].textureTo == rojoRHI::TextureUse::StorageWrite);
    }
    SECTION("a prior external read needs no barrier for another read") {
        RenderGraph graph;
        const auto input = graph.importTexture(texture, rojoRHI::Format::RGBA16Float, "history",
                                               rojoRHI::TextureUse::ExternalRead);
        const auto result = graph.importTexture(output, rojoRHI::Format::RGBA16Float, "output");
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
    FakeTexture texture{64, 64, "mips", 2};
    RenderGraph graph;
    const auto input = graph.importTexture(texture, rojoRHI::Format::RGBA16Float, "mips");

    SECTION("overlapping read and write") {
        graph.addExternalPass("vendor", {.textureReads = {input}, .textureWrites = {input}},
                              kNoWork);
    }
    SECTION("read before write") {
        graph.addExternalPass("vendor", {.textureReads = {nextVersion(input)}}, kNoWork);
    }
    SECTION("invalid subresource") {
        graph.addExternalPass("vendor", {.textureReads = {{input, {.baseMipLevel = 2}}}}, kNoWork);
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
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture output{64, 64};
    RenderGraph graph{pool};
    const auto packed = graph.createTexture(kTransientColor, "packed");
    const auto unused = graph.createTexture(kTransientColor, "unused");
    const auto result = graph.importTexture(output, rojoRHI::Format::RGBA16Float, "result");
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
    REQUIRE(record->debug.memory.requested == FakeDevice::kTextureAlignment);
}
