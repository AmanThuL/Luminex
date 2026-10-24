#pragma once

#include <catch2/catch_test_macros.hpp>

#include "Render/Graph/LoweredFrame.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/TransientPool.h"
#include "Support/TransientBackendTestSupport.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <deque>
#include <functional>
#include <ranges>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {
[[maybe_unused]] const ExecuteFn kNoWork = [](const PassResources&) {};

template <typename T>
//======================================================================================================================
inline std::string errorOf(const GraphResult<T>& result) {
    return result ? std::string{} : result.error().message;
}

//======================================================================================================================
inline LoweredFrame lowered(const RenderGraph& graph, uint64_t frameId = 1) {
    auto frame = graph.lower(frameId);
    INFO(errorOf(frame));
    REQUIRE(frame.has_value());
    return std::move(*frame);
}

//======================================================================================================================
inline std::string abortDiagnostic(const std::function<void()>& misuse) {
    int output[2];
    REQUIRE(pipe(output) == 0);
    std::fflush(nullptr);
    const auto child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGABRT, SIG_DFL);
        const rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        dup2(output[1], STDERR_FILENO);
        close(output[1]);
        misuse();
        _exit(0);
    }
    close(output[1]);
    std::string diagnostic;
    std::array<char, 1024> bytes;
    ssize_t count;
    while ((count = read(output[0], bytes.data(), bytes.size())) > 0) {
        diagnostic.append(bytes.data(), static_cast<size_t>(count));
    }
    close(output[0]);
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
    return diagnostic;
}

// Pass placement and logical observer dependencies have separate oracles. Neither converts
// physical synchronization masks back into nominal graph uses.
//======================================================================================================================
inline std::vector<std::string> passEvents(const LoweredFrame& frame) {
    std::vector<std::string> lines;
    for (const auto& op : frame.ops) {
        if (const auto* begin = std::get_if<BeginRenderPassOp>(&op)) {
            lines.push_back("begin " + begin->label);
        } else if (std::holds_alternative<EndRenderPassOp>(op)) {
            lines.emplace_back("end");
        } else if (const auto* scope = std::get_if<BeginScopeOp>(&op)) {
            lines.push_back("scope " + scope->label);
        } else if (std::holds_alternative<EndScopeOp>(op)) {
            lines.emplace_back("end scope");
        } else if (const auto* run = std::get_if<RunOp>(&op)) {
            lines.push_back("run " + run->label);
        }
    }
    return lines;
}

//======================================================================================================================
inline std::vector<std::string> logicalTransitions(const CompiledFrameRecord& record) {
    std::vector<std::string> lines;
    for (const auto& transition : record.debug.transitions) {
        const auto& name = record.debug.resources[transition.resource].name;
        if (transition.kind == GraphResourceKind::Texture) {
            const auto& range = transition.range;
            const bool whole = range.baseMip == 0 && range.mipCount == rojoRHI::kAllMips &&
                               range.baseLayer == 0 && range.layerCount == rojoRHI::kAllLayers;
            lines.push_back("barrier " + name + (whole ? "" : " " + describeRange(range)) + " " +
                            std::string(textureUseName(transition.textureFrom)) + "->" +
                            std::string(textureUseName(transition.textureTo)));
        } else {
            lines.push_back("barrier " + name + " " +
                            std::string(bufferUseName(transition.bufferFrom)) + "->" +
                            std::string(bufferUseName(transition.bufferTo)));
        }
    }
    return lines;
}

//======================================================================================================================
inline std::vector<BeginRenderPassOp> renderPasses(const LoweredFrame& frame) {
    std::vector<BeginRenderPassOp> result;
    for (const auto& op : frame.ops) {
        if (const auto* pass = std::get_if<BeginRenderPassOp>(&op))
            result.push_back(*pass);
    }
    return result;
}

//======================================================================================================================
inline std::vector<LoweredTextureBarrier> textureBarriers(const LoweredFrame& frame) {
    std::vector<LoweredTextureBarrier> result;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<BarriersOp>(&op)) {
            for (const auto& barrier : batch->barriers) {
                if (const auto* texture = std::get_if<LoweredTextureBarrier>(&barrier)) {
                    result.push_back(*texture);
                }
            }
        }
    }
    return result;
}

//======================================================================================================================
inline std::vector<LoweredBufferBarrier> bufferBarriers(const LoweredFrame& frame) {
    std::vector<LoweredBufferBarrier> result;
    for (const auto& op : frame.ops) {
        if (const auto* batch = std::get_if<BarriersOp>(&op)) {
            for (const auto& barrier : batch->barriers) {
                if (const auto* buffer = std::get_if<LoweredBufferBarrier>(&barrier)) {
                    result.push_back(*buffer);
                }
            }
        }
    }
    return result;
}

// One transient the fake backend sizes to exactly one alignment unit: 64 * 64 texels at four bytes
// each is 16 KiB, which is FakeTransientBackend::kTextureAlignment. Every offset below is
// therefore a multiple of the size, which keeps the packing readable in the assertions.
[[maybe_unused]] constexpr TransientTextureDesc kTransientColor{.width = 64,
                                                                .height = 64,
                                                                .format =
                                                                    rojoRHI::Format::RGBA16Float,
                                                                .renderTarget = true,
                                                                .sampled = true};

// Two transients used one after the other, with an imported target between them so the first one's
// lifetime closes before the second one's opens. It is the shape aliasing exists for.
struct DisjointFrame {
    FakeTransientBackend transients;
    TransientPool pool{transients};
    engine::TextureRef midTarget = textureFixture(64, 64, "mid");
    engine::TextureRef outTarget = textureFixture(64, 64, "out");
    RenderGraph graph{pool};

    //==================================================================================================================
    // `secondDesc` is a parameter so a test can make the second transient incompatible with the
    // first without restating the frame.
    void declare(const TransientTextureDesc& secondDesc = kTransientColor) {
        const GraphTexture first = graph.createTexture(kTransientColor, "lmx.transient.first");
        const GraphTexture second = graph.createTexture(secondDesc, "lmx.transient.second");
        const GraphTexture mid =
            graph.importTexture(formatted(midTarget, rojoRHI::Format::BGRA8Unorm), "mid");
        const GraphTexture out =
            graph.importTexture(formatted(outTarget, rojoRHI::Format::BGRA8Unorm), "out");

        PassDesc writeFirst;
        writeFirst.color = ColorAttachment{.handle = first};
        graph.addPass("lmx.pass.writeFirst", writeFirst, kNoWork);

        PassDesc readFirst;
        readFirst.textureReads.push_back(nextVersion(first));
        readFirst.color = ColorAttachment{.handle = mid};
        graph.addPass("lmx.pass.readFirst", readFirst, kNoWork);

        PassDesc writeSecond;
        writeSecond.color = ColorAttachment{.handle = second};
        graph.addPass("lmx.pass.writeSecond", writeSecond, kNoWork);

        PassDesc readSecond;
        readSecond.textureReads.push_back(nextVersion(second));
        readSecond.color = ColorAttachment{.handle = out};
        graph.addPass("lmx.pass.readSecond", readSecond, kNoWork);

        graph.exportTexture(nextVersion(mid));
        graph.exportTexture(nextVersion(out));
    }
};

} // namespace
