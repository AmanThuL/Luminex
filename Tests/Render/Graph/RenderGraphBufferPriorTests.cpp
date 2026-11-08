#include "Support/RenderGraphTestSupport.h"

#include "Render/Graph/GraphDump.h"

namespace {
//======================================================================================================================
LoweredFrame bufferPriorFrame(const BufferRef& source, const BufferRef& destination,
                              BufferUse nominal, std::optional<SyncPoint> physical,
                              bool write = false) {
    RenderGraph graph;
    const auto input = physical ? graph.importBuffer(source, "source", nominal, *physical)
                                : graph.importBuffer(source, "source", nominal);
    const auto output = graph.importBuffer(destination, "destination");
    if (write) {
        ComputePassDesc pass;
        pass.bufferWrites.push_back(input);
        graph.addComputePass("lmx.pass.overwrite", pass, kNoWork);
        graph.exportBuffer(nextVersion(input));
    } else {
        CopyPassDesc pass;
        pass.bufferSources.push_back(input);
        pass.bufferDestinations.push_back(output);
        graph.addCopyPass("lmx.pass.copy", pass, kNoWork);
        graph.exportBuffer(nextVersion(output));
    }
    return lowered(graph);
}
} // namespace

//======================================================================================================================
TEST_CASE("buffer physical prior preserves mixed producers without changing logical records",
          "[render][graph]") {
    const auto source = bufferFixture(32, "source");
    const auto destination = bufferFixture(32, "destination");
    const SyncPoint physical{rojoRHI::Stage::Copy | rojoRHI::Stage::ComputeShader,
                             rojoRHI::Access::CopyWrite | rojoRHI::Access::ShaderWrite,
                             rojoRHI::Layout::Undefined};
    const auto exact = bufferPriorFrame(source, destination, BufferUse::StorageWrite, physical);
    const auto nominal =
        bufferPriorFrame(source, destination, BufferUse::StorageWrite, std::nullopt);
    const auto barriers = bufferBarriers(exact);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.stage == physical.stage);
    REQUIRE(barriers[0].from.access == physical.access);
    REQUIRE(barriers[0].from.layout == rojoRHI::Layout::Undefined);
    REQUIRE(barriers[0].to.stage == rojoRHI::Stage::Copy);
    REQUIRE(barriers[0].to.access == rojoRHI::Access::CopyRead);
    REQUIRE(dumpCompiledFrame(exact.record) == dumpCompiledFrame(nominal.record));
}

//======================================================================================================================
TEST_CASE("buffer physical reader union reaches a later compute write", "[render][graph]") {
    const auto source = bufferFixture(32, "source");
    const auto destination = bufferFixture(32, "destination");
    const SyncPoint physical{rojoRHI::Stage::Copy | rojoRHI::Stage::ComputeShader,
                             rojoRHI::Access::CopyRead | rojoRHI::Access::ShaderRead,
                             rojoRHI::Layout::Undefined};
    const auto frame =
        bufferPriorFrame(source, destination, BufferUse::StorageRead, physical, true);
    const auto barriers = bufferBarriers(frame);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.stage == physical.stage);
    REQUIRE(barriers[0].from.access == physical.access);
    REQUIRE(barriers[0].to.stage == rojoRHI::Stage::ComputeShader);
    REQUIRE(barriers[0].to.access == (rojoRHI::Access::ShaderRead | rojoRHI::Access::ShaderWrite));
}

//======================================================================================================================
TEST_CASE("an explicit empty buffer prior takes precedence over nominal history",
          "[render][graph]") {
    const auto source = bufferFixture(32, "source");
    const auto destination = bufferFixture(32, "destination");
    const auto frame = bufferPriorFrame(
        source, destination, BufferUse::StorageWrite,
        SyncPoint{rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined});
    REQUIRE(bufferBarriers(frame).empty());
}

//======================================================================================================================
TEST_CASE("absent buffer physical prior retains nominal shader widening", "[render][graph]") {
    const auto source = bufferFixture(32, "source");
    const auto destination = bufferFixture(32, "destination");
    const auto frame = bufferPriorFrame(source, destination, BufferUse::StorageWrite, std::nullopt);
    const auto barriers = bufferBarriers(frame);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.stage ==
            (rojoRHI::Stage::VertexShader | rojoRHI::Stage::FragmentShader |
             rojoRHI::Stage::ComputeShader));
    REQUIRE(barriers[0].from.access ==
            (rojoRHI::Access::ShaderRead | rojoRHI::Access::ShaderWrite));
}
