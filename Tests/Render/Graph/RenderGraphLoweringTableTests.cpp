#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/GraphDump.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/RenderGraphLowering.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;
namespace {
using S = rojoRHI::Stage;
using A = rojoRHI::Access;
using L = rojoRHI::Layout;
constexpr auto kNoWork = [](const PassResources&) {};

//======================================================================================================================
engine::TextureRef textureRef(uint64_t bits, rojoRHI::Format format = rojoRHI::Format::RGBA16Float,
                              uint32_t mips = 1, uint32_t layers = 1, L initial = L::Undefined) {
    return {{bits}, engine::kNoView, {64, 64}, format, mips, layers, initial};
}

//======================================================================================================================
BufferRef bufferRef(uint64_t bits) {
    return {{bits}, 4096, std::nullopt};
}

//======================================================================================================================
LoweredFrame lower(const RenderGraph& graph) {
    auto result = graph.lower(1);
    REQUIRE(result.has_value());
    return std::move(*result);
}

//======================================================================================================================
std::vector<LoweredTextureBarrier> textureBarriers(const LoweredFrame& frame, uint32_t resource) {
    std::vector<LoweredTextureBarrier> result;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<BarriersOp>(&op)) {
            for (const auto& barrier : batch->barriers) {
                if (const auto* texture = std::get_if<LoweredTextureBarrier>(&barrier);
                    texture && texture->resource == resource) {
                    result.push_back(*texture);
                }
            }
        }
    }
    return result;
}

//======================================================================================================================
GraphTexture rasterOutput(RenderGraph& graph, GraphTexture input, GraphTexture output,
                          std::string_view label = "sample") {
    PassDesc pass;
    pass.textureReads.push_back(input);
    pass.color = ColorAttachment{output};
    graph.addPass(label, pass, kNoWork);
    return nextVersion(output);
}
// A real seam implementation with no RHI object subclasses or device. Its bounds check is the
// same heap contract enforced by production placement, and query counts expose impure lowering.
struct PlanningBackend final : TransientBackend {
    uint64_t frame = 0;
    mutable uint32_t queries = 0;
    bool refuseQueries = false;
    bool failTexture = false;
    uint64_t nextHandle = 100;
    uint32_t nextHeap = 1;
    struct Heap {
        uint64_t bytes;
        std::vector<engine::TextureRef> textures;
        std::vector<rojoRHI::Buffer> buffers;
    };
    std::map<uint32_t, Heap> heaps;

    //==================================================================================================================
    uint64_t frameNumber() const override { return frame; }

    //==================================================================================================================
    TransientFootprint footprint(const TransientTextureDesc& desc) const override {
        LMX_ASSERT(!refuseQueries && desc.width && desc.height && desc.mipLevels,
                   "placement query outside declaration or invalid descriptor");
        ++queries;
        return {16384, 16384};
    }

    //==================================================================================================================
    TransientFootprint footprint(const TransientBufferDesc& desc,
                                 bool indirect = false) const override {
        LMX_ASSERT(!refuseQueries && desc.size,
                   "placement query outside declaration or invalid descriptor");
        ++queries;
        return {desc.size + (indirect ? 256u : 0u), 256};
    }

    //==================================================================================================================
    Result<TransientHeapId> createHeap(uint64_t bytes, std::string_view) override {
        const TransientHeapId id{nextHeap++};
        heaps.emplace(id.value, Heap{bytes, {}, {}});
        return id;
    }

    //==================================================================================================================
    void destroyHeap(TransientHeapId heap) override {
        REQUIRE(heaps.at(heap.value).textures.empty());
        REQUIRE(heaps.at(heap.value).buffers.empty());
        heaps.erase(heap.value);
    }

    //==================================================================================================================
    Result<engine::TextureRef> placeTexture(TransientHeapId heap, uint64_t offset,
                                            const TransientTextureDesc& desc,
                                            std::string_view) override {
        LMX_ASSERT(offset <= heaps.at(heap.value).bytes &&
                       16384 <= heaps.at(heap.value).bytes - offset,
                   "placed texture exceeds its heap");
        if (failTexture) {
            return std::unexpected(Error{ErrorCode::Gpu, "injected placement refusal"});
        }
        auto ref = textureRef(nextHandle++, desc.format, desc.mipLevels,
                              desc.kind == rojoRHI::TextureType::Cube ? 6u : 1u);
        ref.view = static_cast<rojoRHI::ViewIndex>(ref.texture.bits);
        heaps.at(heap.value).textures.push_back(ref);
        return ref;
    }

    //==================================================================================================================
    Result<rojoRHI::Buffer> placeBuffer(TransientHeapId heap, uint64_t offset,
                                        const TransientBufferDesc& desc, std::string_view,
                                        bool indirect = false) override {
        const uint64_t size = desc.size + (indirect ? 256u : 0u);
        LMX_ASSERT(offset <= heaps.at(heap.value).bytes &&
                       size <= heaps.at(heap.value).bytes - offset,
                   "placed buffer exceeds its heap");
        const rojoRHI::Buffer buffer{nextHandle++};
        heaps.at(heap.value).buffers.push_back(buffer);
        return buffer;
    }

    //==================================================================================================================
    rojoRHI::ViewIndex storageView(TransientHeapId heap, rojoRHI::Texture texture,
                                   uint32_t mip) const override {
        const auto& textures = heaps.at(heap.value).textures;
        const auto found = std::ranges::find_if(
            textures, [texture](const auto& ref) { return ref.texture == texture; });
        REQUIRE(found != textures.end());
        REQUIRE(mip < found->mips);
        return static_cast<rojoRHI::ViewIndex>(texture.bits * 16 + mip);
    }

    //==================================================================================================================
    rojoRHI::Addr address(TransientHeapId heap, rojoRHI::Buffer buffer) const override {
        REQUIRE(std::ranges::find(heaps.at(heap.value).buffers, buffer) !=
                heaps.at(heap.value).buffers.end());
        return {static_cast<uint32_t>(buffer.bits * 256), 0};
    }

    //==================================================================================================================
    void releasePlaced(TransientHeapId heap) override {
        heaps.at(heap.value).textures.clear();
        heaps.at(heap.value).buffers.clear();
    }
};
} // namespace

//======================================================================================================================
TEST_CASE("the mapping table matches the design rows", "[render][graph]") {
    const auto color = textureSync(TextureUse::RenderTarget, PassKind::Raster, false);
    REQUIRE(color.stage == S::ColorAttachment);
    REQUIRE(color.access == (A::ColorAttachmentRead | A::ColorAttachmentWrite));
    REQUIRE(color.layout == L::ColorAttachment);
    const auto depth = textureSync(TextureUse::RenderTarget, PassKind::Raster, true);
    REQUIRE(depth.stage == S::DepthStencil);
    REQUIRE(depth.access == (A::DepthStencilRead | A::DepthStencilWrite));
    REQUIRE(depth.layout == L::DepthStencilWrite);
    REQUIRE(textureSync(TextureUse::ShaderRead, PassKind::Raster, false).stage ==
            (S::VertexShader | S::FragmentShader));
    REQUIRE(textureSync(TextureUse::ShaderRead, PassKind::Compute, false).stage ==
            S::ComputeShader);
    REQUIRE(textureSync(TextureUse::StorageRead, PassKind::Compute, false).layout ==
            L::ShaderWrite);
    REQUIRE(textureSync(TextureUse::StorageRead, PassKind::Compute, false).access == A::ShaderRead);
    REQUIRE(textureSync(TextureUse::StorageWrite, PassKind::Compute, false).access ==
            A::ShaderWrite);
    REQUIRE(textureSync(TextureUse::CopySource, PassKind::Copy, false).layout == L::CopySrc);
    REQUIRE(textureSync(TextureUse::CopyDestination, PassKind::Copy, false).stage == S::Copy);
    REQUIRE(textureSync(TextureUse::ExternalRead, PassKind::External, false).layout ==
            L::ShaderRead);
    REQUIRE(textureSync(TextureUse::ExternalWrite, PassKind::External, false).layout ==
            L::ShaderWrite);
    REQUIRE(bufferSync(BufferUse::ShaderRead, PassKind::Raster).stage ==
            (S::VertexShader | S::FragmentShader));
    REQUIRE(bufferSync(BufferUse::StorageRead, PassKind::Compute).access == A::ShaderRead);
    REQUIRE(bufferSync(BufferUse::StorageWrite, PassKind::Compute).access == A::ShaderWrite);
    REQUIRE(bufferSync(BufferUse::CopySource, PassKind::Copy).access == A::CopyRead);
    REQUIRE(bufferSync(BufferUse::CopyDestination, PassKind::Copy).access == A::CopyWrite);
    REQUIRE(bufferSync(BufferUse::IndirectArgument, PassKind::Raster).stage == S::IndirectArgs);
}

//======================================================================================================================
TEST_CASE("a depth shader read lowers to DepthStencilRead", "[render][graph]") {
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    auto depth = graph.importPersistentTexture(
        textureRef(1, rojoRHI::Format::D32Float, 1, 1, L::ShaderRead), "depth");
    auto color = graph.importTexture(textureRef(2), "color");
    graph.exportTexture(rasterOutput(graph, depth, color));
    const auto frame = lower(graph);
    const auto barriers = textureBarriers(frame, depth.index);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.layout == L::ShaderRead);
    REQUIRE(barriers[0].from.stage == S::None);
    REQUIRE(barriers[0].to.layout == L::DepthStencilRead);
    REQUIRE_FALSE(barriers[0].alias);
    REQUIRE(frame.record.debug.transitions.empty());
    REQUIRE(states.size() == 0);
}

//======================================================================================================================
TEST_CASE("transient first use starts from Undefined", "[render][graph]") {
    TextureStateTable states;
    states.setLogicalUse({1}, TextureUse::ShaderRead);
    RenderGraph graph;
    graph.bindTextureStates(states);
    auto target = graph.importPersistentTexture(textureRef(1), "replacement");
    graph.addPass("clear", {.color = ColorAttachment{target}}, kNoWork);
    graph.exportTexture(nextVersion(target));
    auto record = graph.compileFrame(1);
    REQUIRE(record.has_value());
    const auto frame = lower(graph);
    REQUIRE(dumpCompiledFrame(frame.record) == dumpCompiledFrame(*record));
    const auto barriers = textureBarriers(frame, target.index);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.layout == L::Undefined);
    REQUIRE(barriers[0].from.stage == S::None);
    REQUIRE(barriers[0].from.access == A::None);
    REQUIRE(barriers[0].to.layout == L::ColorAttachment);
    REQUIRE(states.logicalUse({1}) == TextureUse::ShaderRead);
    REQUIRE(states.physicalRanges({1}, {}).empty());
    SECTION("each drawable acquisition discards the previous presentation") {
        states.seedPhysical({50}, 1, 1, L::Present);
        for (uint64_t frameNumber = 1; frameNumber <= 2; ++frameNumber) {
            RenderGraph drawable;
            drawable.bindTextureStates(states);
            auto image = drawable.importTexture(textureRef(50), "drawable");
            drawable.addPass("draw", {.color = ColorAttachment{image}}, kNoWork);
            drawable.presentTexture(nextVersion(image));
            auto lowered = drawable.lower(frameNumber);
            REQUIRE(lowered.has_value());
            const auto physical = textureBarriers(*lowered, image.index);
            REQUIRE(physical.size() == 2);
            REQUIRE(physical.front().from.layout == L::Undefined);
            REQUIRE(physical.back().to.layout == L::Present);
            REQUIRE(physical.back().to.stage == S::None);
            REQUIRE(physical.back().to.access == A::None);
            REQUIRE(lowered->physicalStates.empty());
        }
        REQUIRE(states.find({50}, {})->layout == L::Present);
    }
    SECTION("an initialized color does not discard compatible sampling") {
        RenderGraph sampled;
        sampled.bindTextureStates(states);
        auto input = sampled.importPersistentTexture(
            textureRef(3, rojoRHI::Format::RGBA16Float, 1, 1, L::ShaderRead), "initialized");
        auto output = sampled.importTexture(textureRef(4), "out");
        sampled.exportTexture(rasterOutput(sampled, input, output));
        REQUIRE(textureBarriers(lower(sampled), input.index).empty());
    }
}

//======================================================================================================================
TEST_CASE("a final use transitions from every reader since the last write", "[render][graph]") {
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    auto image = graph.importPersistentTexture(textureRef(1), "image");
    auto marker = graph.importBuffer(bufferRef(2), "marker");
    auto output = graph.importTexture(textureRef(3), "out");
    graph.addComputePass("write", {.textureWrites = {image}}, kNoWork);
    graph.addComputePass("compute read",
                         {.shaderTextureReads = {nextVersion(image)}, .bufferWrites = {marker}},
                         kNoWork);
    graph.exportTexture(rasterOutput(graph, nextVersion(image), output, "raster read"));
    graph.exportBuffer(nextVersion(marker));
    graph.setFinalUse(image, TextureUse::CopySource);
    const auto frame = lower(graph);
    const auto barriers = textureBarriers(frame, image.index);
    REQUIRE(barriers.size() == 4);
    REQUIRE(barriers[2].from.stage == S::ComputeShader);
    REQUIRE(barriers[2].to.stage == (S::VertexShader | S::FragmentShader));
    REQUIRE(barriers.back().from.stage == (S::ComputeShader | S::VertexShader | S::FragmentShader));
    REQUIRE(barriers.back().from.access == A::ShaderRead);
    REQUIRE(barriers.back().from.layout == L::ShaderRead);
    REQUIRE(barriers.back().to.layout == L::CopySrc);
    SECTION("same-layout storage reads retain the writer dependency chain") {
        RenderGraph storage;
        storage.bindTextureStates(states);
        auto data = storage.importPersistentTexture(textureRef(20), "storage");
        auto firstMarker = storage.importBuffer(bufferRef(21), "first marker");
        auto secondMarker = storage.importBuffer(bufferRef(22), "second marker");
        storage.addComputePass("write", {.textureWrites = {data}}, kNoWork);
        storage.addComputePass("first read",
                               {.textureReads = {nextVersion(data)}, .bufferWrites = {firstMarker}},
                               kNoWork);
        storage.addComputePass(
            "second read", {.textureReads = {nextVersion(data)}, .bufferWrites = {secondMarker}},
            kNoWork);
        storage.addComputePass("overwrite", {.textureWrites = {nextVersion(data)}}, kNoWork);
        storage.exportTexture({data.index, 2});
        storage.exportBuffer(nextVersion(firstMarker));
        storage.exportBuffer(nextVersion(secondMarker));
        const auto physical = textureBarriers(lower(storage), data.index);
        REQUIRE(physical.size() == 3);
        REQUIRE(physical[1].from.access == A::ShaderWrite);
        REQUIRE(physical[1].to.access == A::ShaderRead);
        REQUIRE(physical[2].from.layout == L::ShaderWrite);
        REQUIRE(physical[2].from.access == A::ShaderRead);
        REQUIRE(physical[2].to.access == A::ShaderWrite);
    }
    SECTION("explicit final use conflicts honestly with presentation") {
        RenderGraph conflicting;
        conflicting.bindTextureStates(states);
        auto target = conflicting.importPersistentTexture(textureRef(30), "presented");
        conflicting.addPass("draw", {.color = ColorAttachment{target}}, kNoWork);
        conflicting.presentTexture(nextVersion(target));
        conflicting.setFinalUse(target, TextureUse::ShaderRead);
        REQUIRE(conflicting.compileFrame(1).has_value());
        const auto failed = conflicting.lower(1);
        REQUIRE_FALSE(failed.has_value());
        REQUIRE(failed.error().message == "texture 'presented' requests final use ShaderRead and "
                                          "presentation, which require different terminal states");
        REQUIRE(states.size() == 0);
    }
    SECTION("an unused explicit final request performs physical work") {
        RenderGraph unused;
        unused.bindTextureStates(states);
        auto target = unused.importPersistentTexture(textureRef(9), "unused");
        unused.setFinalUse(target, TextureUse::CopySource);
        const auto empty = lower(unused);
        REQUIRE(empty.record.debug.schedule.passes.empty());
        REQUIRE(empty.record.debug.transitions.empty());
        REQUIRE(textureBarriers(empty, target.index).size() == 1);
        REQUIRE(empty.finalStates.size() == 1);
        REQUIRE(empty.physicalStates.size() == 1);
    }
    SECTION("layout epochs do not retain invalid write access bits") {
        REQUIRE((barriers.back().from.access & A::ShaderWrite) == A::None);
        graph.addComputePass("overwrite", {.textureWrites = {nextVersion(image)}}, kNoWork);
        graph.exportTexture(GraphTexture{image.index, 2});
        const auto overwritten = textureBarriers(lower(graph), image.index);
        REQUIRE(overwritten[3].from.stage ==
                (S::ComputeShader | S::VertexShader | S::FragmentShader));
        REQUIRE(overwritten[3].from.access == A::ShaderRead);
        REQUIRE(overwritten[3].to.access == A::ShaderWrite);
    }
}

//======================================================================================================================
TEST_CASE("per-mip write-back keeps each mip's layout", "[render][graph]") {
    TextureStateTable states;
    states.setLogicalUse({1}, TextureUse::StorageWrite);
    states.seedPhysical({1}, 3, 2, L::ShaderRead);
    states.set({1}, {0, 1, 0, 1}, {S::ComputeShader, A::ShaderWrite, L::ShaderWrite});
    states.set({1}, {1, 1, 1, 1}, {S::Copy, A::CopyWrite, L::CopyDst});
    REQUIRE_FALSE(states.find({1}, {}).has_value());
    REQUIRE(states.physicalRanges({1}, {}).size() == 6);
    states.seedPhysical({1}, 3, 2, L::Undefined);
    REQUIRE(states.find({1}, {0, 1, 0, 1})->layout == L::ShaderWrite);
    REQUIRE(states.logicalUse({1}) == TextureUse::StorageWrite);
    RenderGraph graph;
    graph.bindTextureStates(states);
    auto input =
        graph.importPersistentTexture(textureRef(1, rojoRHI::Format::RGBA16Float, 3, 2), "mixed");
    auto out = graph.importTexture(textureRef(2), "out");
    graph.exportTexture(rasterOutput(graph, input, out));
    const auto frame = lower(graph);
    const auto barriers = textureBarriers(frame, input.index);
    REQUIRE(barriers.size() == 2);
    REQUIRE(barriers[0].range.baseMip == 0);
    REQUIRE(barriers[0].range.baseLayer == 0);
    REQUIRE(barriers[1].range.baseMip == 1);
    REQUIRE(barriers[1].range.baseLayer == 1);
    REQUIRE(barriers[0].range.mipCount == 1);
    REQUIRE(barriers[1].range.layerCount == 1);
    REQUIRE(states.find({1}, {1, 1, 1, 1})->layout == L::CopyDst);
    REQUIRE(states.find({1}, {2, 1, 0, 1})->layout == L::ShaderRead);
    REQUIRE(states.size() == 1);
    states.forget({1});
    REQUIRE(states.size() == 0);
    REQUIRE_FALSE(states.logicalUse({1}));
    REQUIRE(states.physicalRanges({1}, {}).empty());
}

//======================================================================================================================
TEST_CASE("a culled persistent import keeps its layout", "[render][graph]") {
    TextureStateTable states;
    states.seedPhysical({1}, 2, 1, L::ShaderRead);
    states.set({1}, {1, 1, 0, 1}, {S::ComputeShader, A::ShaderWrite, L::ShaderWrite});
    states.setLogicalUse({1}, TextureUse::StorageWrite);
    RenderGraph graph;
    graph.bindTextureStates(states);
    auto input =
        graph.importPersistentTexture(textureRef(1, rojoRHI::Format::RGBA16Float, 2), "history");
    auto output = graph.importTexture(textureRef(2), "culled");
    rasterOutput(graph, input, output);
    auto first = lower(graph);
    auto second = lower(graph);
    REQUIRE(first.physicalStates.empty());
    REQUIRE(first.finalStates.empty());
    REQUIRE(dumpCompiledFrame(first.record) == dumpCompiledFrame(second.record));
    REQUIRE(states.find({1}, {1, 1, 0, 1})->layout == L::ShaderWrite);
    SECTION("incompatible simultaneous sampled and storage layouts fail without writeback") {
        RenderGraph invalid;
        invalid.bindTextureStates(states);
        auto history = invalid.importPersistentTexture(
            textureRef(1, rojoRHI::Format::RGBA16Float, 2), "history");
        auto result = invalid.importTexture(textureRef(3), "result");
        invalid.addComputePass(
            "conflicting reads",
            {.textureReads = {history}, .shaderTextureReads = {history}, .textureWrites = {result}},
            kNoWork);
        invalid.exportTexture(nextVersion(result));
        REQUIRE(invalid.compile().has_value());
        const auto failed = invalid.lower(1);
        REQUIRE_FALSE(failed.has_value());
        REQUIRE(failed.error().message == "pass 'conflicting reads' declares incompatible "
                                          "simultaneous layouts for texture 'history'");
        REQUIRE(states.find({1}, {1, 1, 0, 1})->layout == L::ShaderWrite);
    }
    graph.exportTexture({output.index, 9});
    REQUIRE_FALSE(graph.lower(1).has_value());
    REQUIRE(states.find({1}, {1, 1, 0, 1})->layout == L::ShaderWrite);
    REQUIRE(states.logicalUse({1}) == TextureUse::StorageWrite);
}

//======================================================================================================================
TEST_CASE("compile and lowering consume declaration footprint snapshots", "[render][graph]") {
    PlanningBackend backend;
    TransientPool pool(backend);
    RenderGraph graph(pool);
    auto texture = graph.createTexture({64, 64, rojoRHI::Format::RGBA16Float,
                                        rojoRHI::TextureType::Tex2D, 1, false, true, false, true},
                                       "scratch");
    auto args = graph.createBuffer({256, false, true}, "args");
    auto out = graph.importBuffer(bufferRef(1), "out");
    graph.addComputePass("write", {.textureWrites = {texture}, .bufferWrites = {args}}, kNoWork);
    graph.addComputePass(
        "indirect", {.indirectBufferReads = {nextVersion(args)}, .bufferWrites = {out}}, kNoWork);
    graph.exportBuffer(nextVersion(out));
    REQUIRE(backend.queries == 3);
    backend.refuseQueries = true;
    REQUIRE(graph.compile().has_value());
    auto first = lower(graph);
    auto second = lower(graph);
    REQUIRE(backend.queries == 3);
    REQUIRE(dumpCompiledFrame(first.record) == dumpCompiledFrame(second.record));
    REQUIRE(first.transientLayout.placements.back().footprint.size == 512);
    REQUIRE(std::get<TransientBufferPlacementDesc>(first.transientLayout.placements.back().desc)
                .indirectArgs);
    SECTION("a culled indirect consumer does not change a live buffer") {
        PlanningBackend unusedBackend;
        TransientPool unusedPool(unusedBackend);
        RenderGraph unused(unusedPool);
        auto data = unused.createBuffer({256, false, true}, "data");
        auto live = unused.importBuffer(bufferRef(2), "live");
        auto dead = unused.importBuffer(bufferRef(3), "dead");
        unused.addComputePass("write", {.bufferWrites = {data}}, kNoWork);
        unused.addComputePass(
            "read", {.shaderBufferReads = {nextVersion(data)}, .bufferWrites = {live}}, kNoWork);
        unused.addComputePass("culled",
                              {.indirectBufferReads = {nextVersion(data)}, .bufferWrites = {dead}},
                              kNoWork);
        unused.exportBuffer(nextVersion(live));
        unusedBackend.refuseQueries = true;
        const auto frame = lower(unused);
        REQUIRE(frame.transientLayout.placements[0].footprint.size == 256);
        REQUIRE_FALSE(
            std::get<TransientBufferPlacementDesc>(frame.transientLayout.placements[0].desc)
                .indirectArgs);
    }
}

//======================================================================================================================
TEST_CASE("aliased texture discards cover every previous reader stage", "[render][graph]") {
    PlanningBackend backend;
    TransientPool pool(backend);
    RenderGraph graph(pool);
    const TransientTextureDesc desc{
        64,    64,  rojoRHI::Format::RGBA16Float, rojoRHI::TextureType::Tex2D, 1, false, true,
        false, true};
    auto first = graph.createTexture(desc, "first");
    auto second = graph.createTexture(desc, "second");
    auto marker = graph.importBuffer(bufferRef(1), "marker");
    auto color = graph.importTexture(textureRef(2), "color");
    auto final = graph.importTexture(textureRef(3), "final");
    graph.addComputePass("write", {.textureWrites = {first}}, kNoWork);
    graph.addComputePass("compute read",
                         {.shaderTextureReads = {nextVersion(first)}, .bufferWrites = {marker}},
                         kNoWork);
    auto raster = rasterOutput(graph, nextVersion(first), color, "raster read");
    graph.addComputePass("reuse",
                         {.shaderTextureReads = {raster},
                          .shaderBufferReads = {nextVersion(marker)},
                          .textureWrites = {second}},
                         kNoWork);
    graph.exportTexture(rasterOutput(graph, nextVersion(second), final, "finish"));
    const auto frame = lower(graph);
    const auto barriers = textureBarriers(frame, second.index);
    REQUIRE(frame.record.debug.transients[1].aliases);
    REQUIRE(barriers.front().alias);
    REQUIRE(barriers.front().from.layout == L::Undefined);
    REQUIRE(barriers.front().from.access == A::None);
    REQUIRE(barriers.front().from.stage ==
            (S::ComputeShader | S::VertexShader | S::FragmentShader));
}

//======================================================================================================================
TEST_CASE("aliased buffers retain prior write accesses in a global dependency", "[render][graph]") {
    PlanningBackend backend;
    TransientPool pool(backend);
    RenderGraph graph(pool);
    auto first = graph.createBuffer({256, false, true}, "first");
    auto second = graph.createBuffer({256, false, true}, "second");
    auto marker = graph.importBuffer(bufferRef(1), "marker");
    auto result = graph.importBuffer(bufferRef(2), "result");
    graph.addComputePass("write first", {.bufferWrites = {first, marker}}, kNoWork);
    graph.addComputePass("write second",
                         {.shaderBufferReads = {nextVersion(marker)}, .bufferWrites = {second}},
                         kNoWork);
    graph.addCopyPass("finish",
                      {.bufferSources = {nextVersion(second)}, .bufferDestinations = {result}},
                      kNoWork);
    graph.exportBuffer(nextVersion(result));
    const auto frame = lower(graph);
    bool found = false;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<BarriersOp>(&op)) {
            for (const auto& entry : batch->barriers) {
                if (const auto* buffer = std::get_if<LoweredBufferBarrier>(&entry);
                    buffer && buffer->alias) {
                    REQUIRE(buffer->from.stage == S::ComputeShader);
                    REQUIRE(buffer->from.access == A::ShaderWrite);
                    found = true;
                }
            }
        }
    }
    REQUIRE(found);
}

//======================================================================================================================
TEST_CASE("a transient cache matches complete layouts and rejects partial creation",
          "[render][transient]") {
    PlanningBackend backend;
    TransientPool pool(backend);
    const TransientTextureDesc desc{
        64,    64,  rojoRHI::Format::RGBA16Float, rojoRHI::TextureType::Tex2D, 1, false, true,
        false, true};
    TransientLayout layout{32768,
                           {{0, 0, {16384, 16384}, desc, "cached"},
                            {1,
                             16384,
                             {256, 256},
                             TransientBufferPlacementDesc{{256, false, true}, false},
                             "cached buffer"}}};
    rojoRHI::Texture first{};
    rojoRHI::ViewIndex firstView{};
    rojoRHI::Buffer firstBuffer{};
    rojoRHI::Addr firstAddress{};
    for (uint64_t frame = 1; frame <= 4; ++frame) {
        backend.frame = frame;
        pool.beginFrame();
        REQUIRE(pool.reserve(layout).has_value());
        auto result = pool.placeTexture(desc, 0, "cached");
        REQUIRE(result.has_value());
        auto buffer = pool.placeBuffer({256, false, true}, 16384, "cached buffer");
        REQUIRE(buffer.has_value());
        pool.finishPlacement();
        if (frame == 1) {
            first = result->texture;
            firstView = pool.storageView(first, 0);
            firstBuffer = *buffer;
            firstAddress = pool.address(firstBuffer);
        }
        if (frame == 4) {
            REQUIRE(result->texture == first);
            REQUIRE(pool.storageView(first, 0) == firstView);
            REQUIRE(*buffer == firstBuffer);
            REQUIRE(pool.address(*buffer).lo == firstAddress.lo);
            REQUIRE(pool.address(*buffer).hi == firstAddress.hi);
        }
        REQUIRE_FALSE(pool.reserve(layout).has_value());
    }
    auto changed = desc;
    changed.format = rojoRHI::Format::RGBA8Unorm;
    layout.placements[0].desc = changed;
    backend.frame = 7;
    pool.beginFrame();
    REQUIRE(pool.reserve(layout).has_value());
    auto replacement = pool.placeTexture(changed, 0, "cached");
    REQUIRE(replacement.has_value());
    REQUIRE(replacement->texture != first);
    auto replacementBuffer = pool.placeBuffer({256, false, true}, 16384, "cached buffer");
    REQUIRE(replacementBuffer.has_value());
    REQUIRE(*replacementBuffer != firstBuffer);
    pool.finishPlacement();
    SECTION("a failed partial layout is rebuilt") {
        TransientLayout partial{
            32768,
            {{0, 0, {256, 256}, TransientBufferPlacementDesc{{256, false, true}, false}, "buffer"},
             {1, 16384, {16384, 16384}, desc, "texture"}}};
        backend.frame = 10;
        pool.beginFrame();
        REQUIRE(pool.reserve(partial).has_value());
        auto buffer = pool.placeBuffer({256, false, true}, 0, "buffer");
        REQUIRE(buffer.has_value());
        backend.failTexture = true;
        auto failure = pool.placeTexture(desc, 16384, "texture");
        REQUIRE_FALSE(failure.has_value());
        REQUIRE(failure.error().message == "injected placement refusal");
        backend.frame = 13;
        pool.beginFrame();
        REQUIRE(pool.reserve(partial).has_value());
        auto rebuilt = pool.placeBuffer({256, false, true}, 0, "buffer");
        REQUIRE(rebuilt.has_value());
        REQUIRE(*rebuilt != *buffer);
        backend.failTexture = false;
        REQUIRE(pool.placeTexture(desc, 16384, "texture").has_value());
        pool.finishPlacement();
    }
}
