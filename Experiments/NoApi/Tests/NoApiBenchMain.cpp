//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiBenchMain.cpp
/// @brief NoApiBench entry point. Implements `--check-manifest` (M5.1 stage 1 deliverable C): the
///        consistency test declaring the workload manifest's representative graph to the
///        production RenderGraph and asserting the compiled record against the manifest's frozen
///        expectations. Implements `--run-graph=<rhi|noapi> --frames=N --dump-dir=<dir>` (stage 3):
///        runs either the maintained-RHI adapter (Bench/RhiAdapter.h) or the address-first
///        prototype adapter (Bench/NoApiAdapter.h) through the adapter-neutral runner
///        (Bench/Runner.h) for N frames of the representative graph.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/NoApiAdapter.h"
#include "Bench/RhiAdapter.h"
#include "Bench/Runner.h"
#include "Workload/RepresentativeGraph.h"
#include "Workload/Types.h"

#include "Core/Assert.h"
#include "RHI/RHI.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"

#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {

// Test doubles on Tests/RenderGraphTests.cpp's FakeTexture/FakeBuffer pattern: RenderGraph
// validation reads only extent, mip/layer counts, and (for the format used in attachment
// validation) the format snapshotted at import -- never texture.format() itself -- so these report
// the sentinel `rhi::Format::Unknown` and exist only to give importTexture/importBuffer a real
// object to borrow. NoApiBench --check-manifest never executes the graph, so readback() is unused.
struct FakeTexture final : rhi::Texture {
    std::string name;

    FakeTexture(uint32_t width, uint32_t height, std::string label, uint32_t mips)
        : name(std::move(label)), m_width(width), m_height(height), m_mipLevels(mips) {}

    uint32_t width() const override { return m_width; }
    uint32_t height() const override { return m_height; }
    rhi::Format format() const override { return rhi::Format::Unknown; }
    uint32_t mipLevels() const override { return m_mipLevels; }
    uint32_t arrayLayers() const override { return 1; }
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
};

struct FakeBuffer final : rhi::Buffer {
    std::string name;

    FakeBuffer(uint64_t size, std::string label) : name(std::move(label)), m_size(size) {}

    uint64_t size() const override { return m_size; }
    void readback(void*, uint64_t) override {}

private:
    uint64_t m_size = 0;
};

const ExecuteFn kNoWork = [](const PassResources&) {};

//======================================================================================================================
rhi::Format mapFormat(lmx::noapi::workload::Format format) {
    using lmx::noapi::workload::Format;
    switch (format) {
    case Format::RGBA8Unorm:
        return rhi::Format::RGBA8Unorm;
    case Format::RGBA8Unorm_sRGB:
        return rhi::Format::RGBA8Unorm_sRGB;
    case Format::RGBA16Float:
        return rhi::Format::RGBA16Float;
    case Format::RG16Float:
        return rhi::Format::RG16Float;
    case Format::D32Float:
        return rhi::Format::D32Float;
    }
    LMX_ASSERT(false, "NoApiBench: unhandled workload::Format");
    return rhi::Format::Unknown;
}

//======================================================================================================================
rhi::TextureSubresourceRange mapRange(const lmx::noapi::workload::SubresourceRange& range) {
    return {.baseMipLevel = range.baseMipLevel,
            .mipLevelCount = range.mipLevelCount == 0 ? rhi::kAllMipLevels : range.mipLevelCount};
}

// Declares `manifest` to `graph` over imported FakeTexture/FakeBuffer stand-ins for every resource
// it lists (spec section 6: "imported resources standing in for R1-R10"). Storage for the
// stand-ins is owned by `textureStorage`/`bufferStorage`, which must outlive `graph`; both are
// std::deque so a growing container never invalidates a reference already handed to importTexture/
// importBuffer.
//
// A resource's current version is tracked here rather than threaded through the manifest itself:
// the manifest states only which resource a use touches and what it does with it, and the version
// arithmetic -- read the current version, write bumps it by one -- is exactly RenderGraph's own
// rule (RenderGraph.h's GraphTexture/GraphBuffer doc comment), so restating it here would be a
// second copy of a rule the graph already owns.
//======================================================================================================================
void declareToRenderGraph(RenderGraph& graph,
                          const lmx::noapi::workload::RepresentativeGraph& manifest,
                          std::deque<FakeTexture>& textureStorage,
                          std::deque<FakeBuffer>& bufferStorage) {
    namespace workload = lmx::noapi::workload;

    std::map<std::string, GraphTexture> textureBase;
    std::map<std::string, uint32_t> textureVersion;
    std::map<std::string, GraphBuffer> bufferBase;
    std::map<std::string, uint32_t> bufferVersion;

    for (const workload::TextureResource& resource : manifest.textures) {
        textureStorage.emplace_back(resource.width, resource.height, resource.name,
                                    resource.mipLevels);
        const GraphTexture handle =
            graph.importTexture(textureStorage.back(), mapFormat(resource.format), resource.name);
        textureBase[resource.id] = handle;
        textureVersion[resource.id] = 0;
    }
    for (const workload::BufferResource& resource : manifest.buffers) {
        bufferStorage.emplace_back(resource.size, resource.name);
        // R5 (exposure) is the one persistent resource: it is read by an earlier frame's resolve
        // dispatch before this frame's own P05 reads it (spec section 6's cross-frame feedback
        // edge), so its import states that terminal producer, mirroring
        // Source/Render/Renderer.cpp's auto-exposure exposureImport.
        const GraphBuffer handle = resource.persistent
                                       ? graph.importBuffer(bufferStorage.back(), resource.name,
                                                            rhi::BufferUse::StorageWrite)
                                       : graph.importBuffer(bufferStorage.back(), resource.name);
        bufferBase[resource.id] = handle;
        bufferVersion[resource.id] = 0;
    }

    const auto currentTexture = [&](const std::string& id) {
        return GraphTexture{textureBase.at(id).index, textureVersion.at(id)};
    };
    const auto currentBuffer = [&](const std::string& id) {
        return GraphBuffer{bufferBase.at(id).index, bufferVersion.at(id)};
    };

    // A depth attachment defaults to StoreOp::Discard (RenderGraph.h): correct for R3, which
    // nothing reads again this frame, but wrong for R1, which P04 reads after P03 writes it. This
    // set names every resource with a later read/shaderRead/copySource use, so that one attachment
    // rule reads off the manifest instead of being hard-coded per resource id.
    std::set<std::string> readElsewhere;
    for (const workload::PassDeclaration& pass : manifest.passes) {
        for (const workload::ResourceUse& use : pass.uses) {
            if (use.role == workload::UseRole::Read || use.role == workload::UseRole::ShaderRead ||
                use.role == workload::UseRole::CopySource) {
                readElsewhere.insert(use.resourceId);
            }
        }
    }

    for (const workload::PassDeclaration& pass : manifest.passes) {
        std::vector<std::string> writtenTextures;
        std::vector<std::string> writtenBuffers;

        if (pass.kind == workload::PassKind::Raster) {
            PassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::Read:
                    if (texture) {
                        desc.textureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                    } else {
                        desc.bufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::ColorAttachment:
                    desc.color = ColorAttachment{.handle = currentTexture(use.resourceId)};
                    writtenTextures.push_back(use.resourceId);
                    break;
                case workload::UseRole::DepthAttachment:
                    desc.depth = DepthAttachment{.handle = currentTexture(use.resourceId),
                                                 .store = readElsewhere.contains(use.resourceId)
                                                              ? StoreOp::Store
                                                              : StoreOp::Discard};
                    writtenTextures.push_back(use.resourceId);
                    break;
                default:
                    LMX_ASSERT(false, "NoApiBench: unsupported raster use role in manifest");
                }
            }
            graph.addPass(pass.label, std::move(desc), kNoWork);
        } else if (pass.kind == workload::PassKind::Compute) {
            ComputePassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::Read:
                    if (texture) {
                        desc.textureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                    } else {
                        desc.bufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::ShaderRead:
                    if (texture) {
                        desc.shaderTextureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                    } else {
                        desc.shaderBufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::Write:
                    if (texture) {
                        desc.textureWrites.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                        writtenTextures.push_back(use.resourceId);
                    } else {
                        desc.bufferWrites.push_back(currentBuffer(use.resourceId));
                        writtenBuffers.push_back(use.resourceId);
                    }
                    break;
                default:
                    LMX_ASSERT(false, "NoApiBench: unsupported compute use role in manifest");
                }
            }
            graph.addComputePass(pass.label, std::move(desc), kNoWork);
        } else {
            CopyPassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::CopySource:
                    if (texture) {
                        desc.textureSources.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                    } else {
                        desc.bufferSources.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::CopyDestination:
                    if (texture) {
                        desc.textureDestinations.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), mapRange(use.range)));
                        writtenTextures.push_back(use.resourceId);
                    } else {
                        desc.bufferDestinations.push_back(currentBuffer(use.resourceId));
                        writtenBuffers.push_back(use.resourceId);
                    }
                    break;
                default:
                    LMX_ASSERT(false, "NoApiBench: unsupported copy use role in manifest");
                }
            }
            graph.addCopyPass(pass.label, std::move(desc), kNoWork);
        }

        for (const std::string& id : writtenTextures) {
            textureVersion[id] += 1;
        }
        for (const std::string& id : writtenBuffers) {
            bufferVersion[id] += 1;
        }
    }

    for (const workload::SinkDeclaration& sink : manifest.sinks) {
        if (textureBase.contains(sink.resourceId)) {
            const GraphTexture handle = currentTexture(sink.resourceId);
            if (sink.kind == workload::SinkKind::Export) {
                graph.exportTexture(handle);
            } else {
                graph.readbackTexture(handle);
            }
        } else {
            const GraphBuffer handle = currentBuffer(sink.resourceId);
            if (sink.kind == workload::SinkKind::Export) {
                graph.exportBuffer(handle);
            } else {
                graph.readbackBuffer(handle);
            }
        }
    }
}

//======================================================================================================================
int checkManifest() {
    namespace workload = lmx::noapi::workload;

    RenderGraph graph;
    std::deque<FakeTexture> textureStorage;
    std::deque<FakeBuffer> bufferStorage;
    const workload::RepresentativeGraph& manifest = workload::representativeGraph();
    declareToRenderGraph(graph, manifest, textureStorage, bufferStorage);

    const GraphResult<CompiledFrameRecord> compiled = graph.compileFrame(0);
    if (!compiled) {
        std::cerr << "manifest consistency check FAILED: the manifest's declared graph did not "
                     "compile against the production RenderGraph: "
                  << compiled.error().message << "\n";
        return 1;
    }

    bool ok = true;

    std::vector<std::string> actualOrder;
    actualOrder.reserve(compiled->debug.schedule.passes.size());
    for (uint32_t passIndex : compiled->debug.schedule.passes) {
        actualOrder.push_back(manifest.passes.at(passIndex).id);
    }
    const std::vector<std::string>& expectedOrder = workload::expectedScheduleOrder();
    if (actualOrder != expectedOrder) {
        ok = false;
        std::cerr << "schedule mismatch:\n  expected:";
        for (const std::string& id : expectedOrder) {
            std::cerr << " " << id;
        }
        std::cerr << "\n  actual:  ";
        for (const std::string& id : actualOrder) {
            std::cerr << " " << id;
        }
        std::cerr << "\n";
    }

    const std::string dump = dumpCompiledFrame(*compiled);
    if (dump != workload::expectedGraphDump()) {
        ok = false;
        std::cerr << "graph dump mismatch against the manifest's stored expectation.\n"
                  << "--- expected ---\n"
                  << workload::expectedGraphDump() << "\n--- actual ---\n"
                  << dump << "\n----------------\n";
    }

    if (!ok) {
        std::cerr << "manifest consistency check FAILED\n";
        return 1;
    }
    std::cout << "manifest consistency check passed: " << actualOrder.size() << " passes, "
              << compiled->debug.transitions.size() << " derived transitions\n";
    return 0;
}

//======================================================================================================================
// Parses `--run-graph=<value>` out of `arg`, or returns std::nullopt if `arg` names a different
// flag.
std::optional<std::string_view> parseFlagValue(std::string_view arg, std::string_view flag) {
    if (!arg.starts_with(flag) || arg.size() <= flag.size() || arg[flag.size()] != '=') {
        return std::nullopt;
    }
    return arg.substr(flag.size() + 1);
}

//======================================================================================================================
// Stage 3's correctness-run entry point (spec section 6, plan Stage 3 item 3): drives `--run-graph`
// through lmx::noapi::bench::runBench for exactly one frame count, one adapter, one dump directory.
int runGraph(const lmx::noapi::bench::RunOptions& options) {
    if (options.frames == 0) {
        std::cerr << "NoApiBench --run-graph: --frames must be given and non-zero\n";
        return 1;
    }
    if (options.dumpDir.empty()) {
        std::cerr << "NoApiBench --run-graph: --dump-dir must be given\n";
        return 1;
    }

    if (options.graph == "rhi") {
        lmx::noapi::bench::RhiAdapter adapter;
        return lmx::noapi::bench::runBench(adapter, options);
    }
    if (options.graph == "noapi") {
        lmx::noapi::bench::NoApiAdapter adapter;
        return lmx::noapi::bench::runBench(adapter, options);
    }
    std::cerr << "NoApiBench --run-graph: unknown graph '" << options.graph
              << "' (expected 'rhi' or 'noapi')\n";
    return 1;
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    bool checkManifestRequested = false;
    lmx::noapi::bench::RunOptions runOptions;
    bool runGraphRequested = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--check-manifest") {
            checkManifestRequested = true;
        } else if (const auto graph = parseFlagValue(arg, "--run-graph")) {
            runOptions.graph = std::string(*graph);
            runGraphRequested = true;
        } else if (const auto frames = parseFlagValue(arg, "--frames")) {
            runOptions.frames = static_cast<uint32_t>(std::stoul(std::string(*frames)));
        } else if (const auto dumpDir = parseFlagValue(arg, "--dump-dir")) {
            runOptions.dumpDir = std::string(*dumpDir);
        } else {
            std::cerr << "NoApiBench: unrecognized argument '" << arg << "'\n";
            return 1;
        }
    }

    if (checkManifestRequested) {
        return checkManifest();
    }
    if (runGraphRequested) {
        return runGraph(runOptions);
    }
    std::cerr << "usage: NoApiBench --check-manifest\n"
              << "       NoApiBench --run-graph=<rhi|noapi> --frames=N --dump-dir=<dir>\n";
    return 1;
}
