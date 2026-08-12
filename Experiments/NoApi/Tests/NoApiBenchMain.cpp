//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiBenchMain.cpp
/// @brief Implements NoApiBenchMain for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details NoApiBench entry point. Implements `--check-manifest` (M5.1 stage 1 deliverable C): the
///        consistency test declaring the workload manifest's representative graph to the
///        production RenderGraph and asserting the compiled record against the manifest's frozen
///        expectations. Implements `--run-graph=<rhi|noapi> --frames=N --dump-dir=<dir>` (stage 3):
///        runs either the maintained-RHI adapter (Bench/RhiAdapter.h) or the address-first
///        prototype adapter (Bench/NoApiAdapter.h) through the adapter-neutral runner
///        (Bench/Runner.h) for N frames of the representative graph.

#include "Bench/NoApiAdapter.h"
#include "Bench/RhiAdapter.h"
#include "Bench/Runner.h"
#include "Bench/StressCommon.h"
#include "Bench/StressRunner.h"
#include "Workload/RepresentativeGraph.h"
#include "Workload/Types.h"

#include "Core/Assert.h"
#include "RHI/RHI.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
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

    //==================================================================================================================
    FakeTexture(uint32_t width, uint32_t height, std::string label, uint32_t mips)
        : name(std::move(label)), m_width(width), m_height(height), m_mipLevels(mips) {}

    //==================================================================================================================
    uint32_t width() const override { return m_width; }
    //==================================================================================================================
    uint32_t height() const override { return m_height; }
    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::Unknown; }
    //==================================================================================================================
    uint32_t mipLevels() const override { return m_mipLevels; }
    //==================================================================================================================
    uint32_t arrayLayers() const override { return 1; }
    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
};

struct FakeBuffer final : rhi::Buffer {
    std::string name;

    //==================================================================================================================
    FakeBuffer(uint64_t size, std::string label) : name(std::move(label)), m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }
    //==================================================================================================================
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

//======================================================================================================================
// M5.1 Stage 4 measurement mode (plan Stage 4 item 4-5, spec section 8): `--measure=<graph|
// bind1024|bind4096|pipelines> --adapter=<rhi|noapi> --warmup=N --frames=N --json=<path>`.
//
// Escapes the two characters JSON requires, matching Source/Engine/TextureBake.cpp's own
// jsonEscape -- the project's existing precedent for hand-rolled JSON output rather than a new
// dependency (AGENTS.md: "xrepo deps only as needed").
std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

//======================================================================================================================
// The spec's median-per-frame statistic (section 8: "the repetition's statistic is the median
// per-frame value"), computed the same way for every --measure call so collect.py's
// paired-repetition median-of-medians composes over a value this process already computed once,
// deterministically.
uint64_t medianNs(std::vector<uint64_t> values) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return (values[mid - 1] + values[mid]) / 2;
}

//======================================================================================================================
std::string countersToJson(const lmx::noapi::bench::FrameBindingCounters& counters) {
    return std::format(
        "{{\"setAddressCalls\":{},\"pushRootCalls\":{},\"pushRootBytes\":{},"
        "\"tableWriteCalls\":{},\"tableWriteBytes\":{},\"oneTimeTableWriteCalls\":{},"
        "\"oneTimeTableWriteBytes\":{},\"bindCalls\":{},\"setUniformsCalls\":{},"
        "\"setUniformsBytes\":{},\"bufferCreateCalls\":{},\"bufferCreateBytes\":{},"
        "\"barrierCalls\":{}}}",
        counters.setAddressCalls, counters.pushRootCalls, counters.pushRootBytes,
        counters.tableWriteCalls, counters.tableWriteBytes, counters.oneTimeTableWriteCalls,
        counters.oneTimeTableWriteBytes, counters.bindCalls, counters.setUniformsCalls,
        counters.setUniformsBytes, counters.bufferCreateCalls, counters.bufferCreateBytes,
        counters.barrierCalls);
}

//======================================================================================================================
// Both byte fields are explicitly unscored: requestedBytes is descriptive logical size, while
// metalReportedBytes is prototype-only Metal diagnostic data (see Bench/Metrics.h).
std::string allocationToJson(const lmx::noapi::bench::AllocationSnapshot& snapshot) {
    const std::string metalReportedBytes = snapshot.metalReportedBytes.has_value()
                                               ? std::to_string(*snapshot.metalReportedBytes)
                                               : "null";
    return std::format(
        "{{\"textureCreateCalls\":{},\"bufferCreateCalls\":{},\"samplerCreateCalls\":{},"
        "\"pipelineCreateCalls\":{},\"requestedBytes\":{},\"metalReportedBytes\":{}}}",
        snapshot.textureCreateCalls, snapshot.bufferCreateCalls, snapshot.samplerCreateCalls,
        snapshot.pipelineCreateCalls, snapshot.requestedBytes, metalReportedBytes);
}

//======================================================================================================================
// Refuses measurement runs under Metal validation (spec section 8: "performance runs use release
// builds with Metal validation and capture off ... Validation state never mixes within a run").
bool validationIsOff() {
    return std::getenv("MTL_DEBUG_LAYER") == nullptr;
}

//======================================================================================================================
// Writes the JSON blob spec'd for `--measure`'s output ({workload, adapter, perFrameTimedRegionNs,
// medianNs, counters, endOfSetup, endOfRun}) to `path`. `endOfSetup`/`endOfRun` extend the frozen
// {workload, adapter, perFrameTimedRegionNs, medianNs, counters} shape with the allocation
// dimension's two named sample points (spec section 9); paired_bootstrap.py only ever reads
// `medianNs` out of a metric's "pairs", so the extra keys are additive and do not change its input
// contract.
bool writeMeasureJson(const std::filesystem::path& path, std::string_view workload,
                      std::string_view adapter, const std::vector<uint64_t>& perFrameNs,
                      const lmx::noapi::bench::FrameBindingCounters& counters, bool countersStable,
                      const lmx::noapi::bench::AllocationSnapshot& endOfSetup,
                      const lmx::noapi::bench::AllocationSnapshot& endOfRun) {
    std::error_code errorCode;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), errorCode);
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        std::cerr << "NoApiBench --measure: cannot open '" << path.string() << "' for writing\n";
        return false;
    }
    file << "{\n";
    file << "  \"workload\": \"" << jsonEscape(workload) << "\",\n";
    file << "  \"adapter\": \"" << jsonEscape(adapter) << "\",\n";
    file << "  \"perFrameTimedRegionNs\": [";
    for (size_t i = 0; i < perFrameNs.size(); ++i) {
        file << (i == 0 ? "" : ",") << perFrameNs[i];
    }
    file << "],\n";
    file << "  \"medianNs\": " << medianNs(perFrameNs) << ",\n";
    file << "  \"countersStableAcrossFrames\": " << (countersStable ? "true" : "false") << ",\n";
    file << "  \"counters\": " << countersToJson(counters) << ",\n";
    file << "  \"endOfSetup\": " << allocationToJson(endOfSetup) << ",\n";
    file << "  \"endOfRun\": " << allocationToJson(endOfRun) << "\n";
    file << "}\n";
    return static_cast<bool>(file);
}

//======================================================================================================================
// `--measure=graph`: drives either adapter through the representative graph's runFrame() for
// warmup+frames iterations, reading each adapter's own instrumentation (Bench/Runner.h's three
// measurement accessors) after every call rather than computing anything itself.
int measureGraph(const std::string& adapterName, uint32_t warmupFrames, uint32_t measuredFrames,
                 const std::filesystem::path& jsonPath) {
    std::unique_ptr<lmx::noapi::bench::Adapter> adapter;
    if (adapterName == "rhi") {
        adapter = std::make_unique<lmx::noapi::bench::RhiAdapter>(false);
    } else if (adapterName == "noapi") {
        adapter = std::make_unique<lmx::noapi::bench::NoApiAdapter>();
    } else {
        std::cerr << "NoApiBench --measure=graph: unknown --adapter '" << adapterName << "'\n";
        return 1;
    }

    if (!adapter->setup()) {
        std::cerr << "NoApiBench --measure=graph: adapter setup failed\n";
        return 1;
    }
    const lmx::noapi::bench::AllocationSnapshot endOfSetup = adapter->allocationSnapshot();

    std::vector<uint64_t> perFrameNs;
    perFrameNs.reserve(measuredFrames);
    lmx::noapi::bench::FrameBindingCounters counters{};
    bool countersEverSet = false;
    bool countersStable = true;
    std::vector<uint8_t> readback;
    const uint32_t totalFrames = warmupFrames + measuredFrames;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        readback.clear();
        adapter->runFrame(frame, readback);
        if (frame >= warmupFrames) {
            perFrameNs.push_back(adapter->lastFrameTimedRegionNs());
            const lmx::noapi::bench::FrameBindingCounters frameCounters =
                adapter->lastFrameBindingCounters();
            if (!countersEverSet) {
                counters = frameCounters;
                countersEverSet = true;
            } else if (frameCounters != counters) {
                countersStable = false;
            }
        }
    }
    const lmx::noapi::bench::AllocationSnapshot endOfRun = adapter->allocationSnapshot();
    adapter->teardown();

    std::cout << "measure graph adapter=" << adapterName << " medianNs=" << medianNs(perFrameNs)
              << " countersStable=" << (countersStable ? "true" : "false") << "\n";
    return writeMeasureJson(jsonPath, "graph", adapterName, perFrameNs, counters, countersStable,
                            endOfSetup, endOfRun)
               ? 0
               : 1;
}

//======================================================================================================================
// `--measure=bind1024`/`bind4096`: S-BIND is also a timed workload (BindRhi.cpp/BindNoApi.cpp's
// header comments document its setup/per-frame split); this drives the measured variants those
// files implement.
int measureBind(uint32_t drawCount, const std::string& adapterName, uint32_t warmupFrames,
                uint32_t measuredFrames, const std::filesystem::path& jsonPath) {
    const lmx::noapi::bench::MeasuredRun run =
        adapterName == "rhi"
            ? lmx::noapi::bench::measureBindScaleRhi(drawCount, warmupFrames, measuredFrames)
            : lmx::noapi::bench::measureBindScaleNoApi(drawCount, warmupFrames, measuredFrames);
    if (!run.ok) {
        std::cerr << "NoApiBench --measure=bind" << drawCount << ": " << run.error << "\n";
        return 1;
    }
    const std::string workload = "bind" + std::to_string(drawCount);
    std::cout << "measure " << workload << " adapter=" << adapterName
              << " medianNs=" << medianNs(run.perFrameTimedRegionNs)
              << " countersStable=" << (run.countersStableAcrossFrames ? "true" : "false") << "\n";
    return writeMeasureJson(jsonPath, workload, adapterName, run.perFrameTimedRegionNs,
                            run.counters, run.countersStableAcrossFrames, run.endOfSetup,
                            run.endOfRun)
               ? 0
               : 1;
}

//======================================================================================================================
// `--measure=pipelines`: descriptive only (spec section 8: "never triggers adoption thresholds"),
// kept out of the paired/bootstrap path -- its own JSON shape is a flat pipeline table, not the
// {perFrameTimedRegionNs, medianNs} shape paired_bootstrap.py consumes.
int measurePipelines(const std::string& adapterName, const std::filesystem::path& jsonPath) {
    std::vector<std::tuple<std::string, uint64_t, bool>> records;
    if (adapterName == "rhi") {
        lmx::noapi::bench::RhiAdapter adapter(false);
        if (!adapter.setup()) {
            std::cerr << "NoApiBench --measure=pipelines: rhi adapter setup failed\n";
            return 1;
        }
        for (const auto& record : adapter.pipelineCompileTimes()) {
            records.emplace_back(record.label, record.coldNs, record.loadedMetallib);
        }
        adapter.teardown();
    } else if (adapterName == "noapi") {
        lmx::noapi::bench::NoApiAdapter adapter;
        if (!adapter.setup()) {
            std::cerr << "NoApiBench --measure=pipelines: noapi adapter setup failed\n";
            return 1;
        }
        for (const auto& record : adapter.pipelineCompileTimes()) {
            records.emplace_back(record.label, record.coldNs, record.loadedMetallib);
        }
        adapter.teardown();
    } else {
        std::cerr << "NoApiBench --measure=pipelines: unknown --adapter '" << adapterName << "'\n";
        return 1;
    }

    std::cout << "pipeline\tcoldNs\tsource\n";
    for (const auto& [label, coldNs, loadedMetallib] : records) {
        std::cout << label << "\t" << coldNs << "\t"
                  << (loadedMetallib ? "metallib" : "runtime-msl") << "\n";
    }

    if (jsonPath.empty()) {
        return 0;
    }
    std::error_code errorCode;
    if (jsonPath.has_parent_path()) {
        std::filesystem::create_directories(jsonPath.parent_path(), errorCode);
    }
    std::ofstream file(jsonPath, std::ios::trunc);
    if (!file) {
        std::cerr << "NoApiBench --measure=pipelines: cannot open '" << jsonPath.string()
                  << "' for writing\n";
        return 1;
    }
    file << "{\n  \"workload\": \"pipelines\",\n  \"adapter\": \"" << jsonEscape(adapterName)
         << "\",\n  \"pipelines\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& [label, coldNs, loadedMetallib] = records[i];
        file << "    {\"label\": \"" << jsonEscape(label) << "\", \"coldNs\": " << coldNs
             << ", \"source\": \"" << (loadedMetallib ? "metallib" : "runtime-msl") << "\"}"
             << (i + 1 < records.size() ? ",\n" : "\n");
    }
    file << "  ]\n}\n";
    return 0;
}

struct MeasureOptions {
    std::string workload;
    std::string adapter = "rhi";
    uint32_t warmupFrames = 16;
    uint32_t measuredFrames = 256;
    std::filesystem::path jsonPath;
};

//======================================================================================================================
int runMeasure(const MeasureOptions& options);

//======================================================================================================================
// Checks process-launch conditions once, before either half creates a Metal device. Metal may
// mutate diagnostic environment state during device creation, so re-reading it between paired
// halves would confuse that process-internal side effect with the launch configuration.
bool measurementEnvironmentIsValid() {
#ifndef NDEBUG
    std::cerr << "NoApiBench --measure: refusing to run a non-release build (NDEBUG is not set)\n";
    return false;
#endif
    if (!validationIsOff()) {
        std::cerr << "NoApiBench --measure: refusing to run with MTL_DEBUG_LAYER set -- "
                     "measurement mode requires validation off (spec section 8: performance runs "
                     "use release builds with Metal validation and capture off). Unset "
                     "MTL_DEBUG_LAYER and rerun.\n";
        return false;
    }
    if (std::getenv("MTL_CAPTURE_ENABLED") != nullptr ||
        std::getenv("LMX_BENCH_CAPTURE_PATH") != nullptr) {
        std::cerr << "NoApiBench --measure: refusing to run with capture enabled or requested "
                     "(MTL_CAPTURE_ENABLED/LMX_BENCH_CAPTURE_PATH must be unset)\n";
        return false;
    }
    return true;
}

//======================================================================================================================
// Runs the two halves of one paired repetition in this process and in the requested order. Keeping
// both calls behind one process boundary is part of the frozen protocol: setup/teardown still run
// independently per adapter, but process-level conditions are shared by the pair.
int runMeasurePair(const MeasureOptions& base, std::string_view adapterOrder,
                   const std::filesystem::path& rhiJsonPath,
                   const std::filesystem::path& noApiJsonPath) {
    std::array<std::string_view, 2> order{};
    if (adapterOrder == "rhi,noapi") {
        order = {"rhi", "noapi"};
    } else if (adapterOrder == "noapi,rhi") {
        order = {"noapi", "rhi"};
    } else {
        std::cerr << "NoApiBench --measure: unknown --adapter-order '" << adapterOrder
                  << "' (expected 'rhi,noapi' or 'noapi,rhi')\n";
        return 1;
    }
    if (rhiJsonPath.empty() || noApiJsonPath.empty()) {
        std::cerr << "NoApiBench --measure: paired mode requires --json-rhi and --json-noapi\n";
        return 1;
    }

    for (const std::string_view adapter : order) {
        MeasureOptions half = base;
        half.adapter = adapter;
        half.jsonPath = adapter == "rhi" ? rhiJsonPath : noApiJsonPath;
        if (const int result = runMeasure(half); result != 0) {
            return result;
        }
    }
    return 0;
}

//======================================================================================================================
int runMeasure(const MeasureOptions& options) {
    if (options.workload == "graph") {
        return measureGraph(options.adapter, options.warmupFrames, options.measuredFrames,
                            options.jsonPath);
    }
    if (options.workload == "bind1024") {
        return measureBind(1024, options.adapter, options.warmupFrames, options.measuredFrames,
                           options.jsonPath);
    }
    if (options.workload == "bind4096") {
        return measureBind(4096, options.adapter, options.warmupFrames, options.measuredFrames,
                           options.jsonPath);
    }
    if (options.workload == "pipelines") {
        return measurePipelines(options.adapter, options.jsonPath);
    }
    std::cerr << "NoApiBench --measure: unknown workload '" << options.workload
              << "' (expected 'graph', 'bind1024', 'bind4096', or 'pipelines')\n";
    return 1;
}

//======================================================================================================================
// M5.1 Stage 4's misuse-child re-exec protocol (Bench/StressRunner.cpp's header comment): a parent
// process's runMisuse() spawns this same binary with these two variables set, expecting the child
// never to return -- checked before any normal argument parsing so a re-exec never sees the
// parent's own argv.
int maybeRunMisuseChild() {
    const char* caseId = std::getenv(lmx::noapi::bench::kMisuseEnvCase);
    const char* adapterName = std::getenv(lmx::noapi::bench::kMisuseEnvAdapter);
    if (caseId == nullptr || adapterName == nullptr) {
        return -1; // Not a misuse-child invocation.
    }
    const lmx::noapi::bench::AdapterKind adapter = std::string_view(adapterName) == "rhi"
                                                       ? lmx::noapi::bench::AdapterKind::Rhi
                                                       : lmx::noapi::bench::AdapterKind::NoApi;
    return lmx::noapi::bench::runMisuseChild(caseId, adapter);
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    lmx::noapi::bench::setMisuseChildExecutablePath(argv[0]);
    if (const int childResult = maybeRunMisuseChild(); childResult >= 0) {
        return childResult;
    }

    bool checkManifestRequested = false;
    lmx::noapi::bench::RunOptions runOptions;
    bool runGraphRequested = false;
    bool runStressRequested = false;
    bool runMisuseRequested = false;
    bool measureRequested = false;
    bool adapterOrderRequested = false;
    bool adapterRequested = false;
    std::string stressCaseId;
    std::string misuseCaseId;
    lmx::noapi::bench::AdapterKind adapterKind = lmx::noapi::bench::AdapterKind::Rhi;
    MeasureOptions measureOptions;
    std::string adapterOrder;
    std::filesystem::path rhiJsonPath;
    std::filesystem::path noApiJsonPath;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--check-manifest") {
            checkManifestRequested = true;
        } else if (const auto graph = parseFlagValue(arg, "--run-graph")) {
            runOptions.graph = std::string(*graph);
            runGraphRequested = true;
        } else if (const auto frames = parseFlagValue(arg, "--frames")) {
            runOptions.frames = static_cast<uint32_t>(std::stoul(std::string(*frames)));
            measureOptions.measuredFrames = runOptions.frames;
        } else if (const auto dumpDir = parseFlagValue(arg, "--dump-dir")) {
            runOptions.dumpDir = std::string(*dumpDir);
        } else if (const auto stress = parseFlagValue(arg, "--run-stress")) {
            stressCaseId = std::string(*stress);
            runStressRequested = true;
        } else if (const auto misuse = parseFlagValue(arg, "--run-misuse")) {
            misuseCaseId = std::string(*misuse);
            runMisuseRequested = true;
        } else if (const auto measure = parseFlagValue(arg, "--measure")) {
            measureOptions.workload = std::string(*measure);
            measureRequested = true;
        } else if (const auto warmup = parseFlagValue(arg, "--warmup")) {
            measureOptions.warmupFrames = static_cast<uint32_t>(std::stoul(std::string(*warmup)));
        } else if (const auto json = parseFlagValue(arg, "--json")) {
            measureOptions.jsonPath = std::string(*json);
        } else if (const auto json = parseFlagValue(arg, "--json-rhi")) {
            rhiJsonPath = std::string(*json);
        } else if (const auto json = parseFlagValue(arg, "--json-noapi")) {
            noApiJsonPath = std::string(*json);
        } else if (const auto order = parseFlagValue(arg, "--adapter-order")) {
            adapterOrder = std::string(*order);
            adapterOrderRequested = true;
        } else if (const auto adapter = parseFlagValue(arg, "--adapter")) {
            adapterRequested = true;
            if (*adapter == "rhi") {
                adapterKind = lmx::noapi::bench::AdapterKind::Rhi;
            } else if (*adapter == "noapi") {
                adapterKind = lmx::noapi::bench::AdapterKind::NoApi;
            } else {
                std::cerr << "NoApiBench: unknown --adapter '" << *adapter
                          << "' (expected 'rhi' or "
                             "'noapi')\n";
                return 1;
            }
            measureOptions.adapter = std::string(*adapter);
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
    if (runStressRequested) {
        return lmx::noapi::bench::runStress(adapterKind, stressCaseId);
    }
    if (runMisuseRequested) {
        return lmx::noapi::bench::runMisuse(adapterKind, misuseCaseId);
    }
    if (measureRequested) {
        if (!measurementEnvironmentIsValid()) {
            return 1;
        }
        if (adapterOrderRequested) {
            if (adapterRequested) {
                std::cerr << "NoApiBench --measure: --adapter and --adapter-order are mutually "
                             "exclusive\n";
                return 1;
            }
            return runMeasurePair(measureOptions, adapterOrder, rhiJsonPath, noApiJsonPath);
        }
        return runMeasure(measureOptions);
    }
    std::cerr << "usage: NoApiBench --check-manifest\n"
              << "       NoApiBench --run-graph=<rhi|noapi> --frames=N --dump-dir=<dir>\n"
              << "       NoApiBench --run-stress=<caseId|all> --adapter=<rhi|noapi>\n"
              << "       NoApiBench --run-misuse=<caseId|all> --adapter=<rhi|noapi>\n"
              << "       NoApiBench --measure=<graph|bind1024|bind4096|pipelines> "
                 "--adapter=<rhi|noapi> [--warmup=N] [--frames=N] --json=<path>\n"
              << "       NoApiBench --measure=<graph|bind1024|bind4096> "
                 "--adapter-order=<rhi,noapi|noapi,rhi> [--warmup=N] [--frames=N] "
                 "--json-rhi=<path> --json-noapi=<path>\n";
    return 1;
}
