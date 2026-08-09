#pragma once
#include "RHI/RHI.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::render {

class RenderGraph;

// A declaration the graph cannot honour: two passes writing one version, a read of contents
// nothing produces, a depth texture bound as a colour attachment. These are expected failures
// rather than assertions because a caller assembles passes from scene data and can reach them
// honestly. Misuse no honest declaration can reach -- a handle whose index names no resource, a
// GraphTexture naming an imported buffer -- stays LMX_ASSERT.
//
// The message names the offending pass and resource: "the graph is invalid" is not actionable,
// "pass 'lmx.pass.scene' declares a read of texture 'shadowMap' version 1, which no pass writes"
// is.
struct GraphError {
    std::string message;
};

template <typename T>
using GraphResult = std::expected<T, GraphError>;

// A logical texture at one point in its history.
//
// `index` names the resource for the graph's lifetime; `version` names its contents. importTexture
// yields version 0 -- the contents the caller already put there -- and a pass declaring a write of
// version v produces version v + 1. A read names the version it consumes, and that naming is the
// entire dependency model: the pass producing a version runs before every pass naming it.
//
// Handles are values that own nothing. They are meaningful only to the RenderGraph that issued
// them, and only for as long as that graph lives.
struct GraphTexture {
    uint32_t index = 0;
    uint32_t version = 0;
    friend bool operator==(GraphTexture, GraphTexture) = default;
};

// The buffer counterpart of GraphTexture, with the same index/version contract. Textures and
// buffers share one index space, so a GraphBuffer holding a texture's index is misuse and asserts.
struct GraphBuffer {
    uint32_t index = 0;
    uint32_t version = 0;
    friend bool operator==(GraphBuffer, GraphBuffer) = default;
};

// The handle naming a resource's contents after a pass writes `handle`. Version arithmetic is
// deterministic and public so a caller names a pass's result without threading a value back out of
// every addPass call.
constexpr GraphTexture nextVersion(GraphTexture handle) {
    return {.index = handle.index, .version = handle.version + 1};
}
constexpr GraphBuffer nextVersion(GraphBuffer handle) {
    return {.index = handle.index, .version = handle.version + 1};
}

// What an attachment does with the version it names on entry. Clear starts from the clear value;
// Load keeps the existing contents, which makes the attachment a read of that version as well as a
// write of it. Either way the pass is ordered after whatever produced that version -- a clear that
// overtook its producer would erase the wrong contents.
enum class LoadOp { Clear, Load };

// Whether the version the pass produces outlives the pass. Store keeps it readable by later passes
// and by exportTexture; Discard states that nothing consumes it, as for a depth buffer used only
// for the pass's own hidden-surface removal.
enum class StoreOp { Store, Discard };

// The pass's single colour attachment. `handle` is the version the pass writes, so the pass
// produces nextVersion(handle). The named resource must have been imported with a colour-renderable
// format, and must share its extent with the depth attachment when the pass declares both.
struct ColorAttachment {
    GraphTexture handle;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    // Applies when load == LoadOp::Clear. The hardware clear writes it into the target unchanged,
    // so it is a value in whatever encoding that target holds.
    float clearColor[4] = {0.f, 0.f, 0.f, 1.f};
};

// The pass's single depth attachment, with ColorAttachment's write-and-produce contract. The named
// resource must have been imported with a depth format.
struct DepthAttachment {
    GraphTexture handle;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Discard;
    float clearDepth = 1.0f;
};

// Everything one pass touches. The graph validates and orders passes from this alone: a resource
// absent here is a resource the pass may not use, and PassResources refuses to resolve it.
struct PassDesc {
    // Versions the pass consumes without writing.
    std::vector<GraphTexture> textureReads;
    std::vector<GraphBuffer> bufferReads;
    // At most one of each, matching the single-colour-attachment render pass this RHI models. A
    // pass may declare neither, either, or both.
    std::optional<ColorAttachment> color;
    std::optional<DepthAttachment> depth;
    // Writes that are not attachments. Each names the version it consumes and produces the next.
    std::vector<GraphTexture> textureWrites;
    std::vector<GraphBuffer> bufferWrites;
};

// What a pass may touch while it runs.
//
// Resolution is exact: a handle resolves only when the pass named that (index, version) pair in its
// PassDesc, as a read, as an attachment, or as a write. Any other handle -- including the right
// resource at the wrong version -- is a dependency the pass failed to declare, so it is reported
// instead of resolved. Resolving it would hand the pass a resource the schedule never ordered it
// against.
//
// A borrowed view: it must not outlive the RenderGraph that produced it.
class PassResources {
public:
    // The imported texture behind a declared handle, never null on success.
    GraphResult<rhi::Texture*> texture(GraphTexture handle) const;
    // The imported buffer behind a declared handle, never null on success.
    GraphResult<rhi::Buffer*> buffer(GraphBuffer handle) const;

private:
    friend class RenderGraph;
    PassResources(const RenderGraph& graph, uint32_t passIndex)
        : m_graph(&graph), m_passIndex(passIndex) {}

    const RenderGraph* m_graph;
    uint32_t m_passIndex;
};

// What a pass does when the schedule reaches it. Stored at declaration; the resources it is allowed
// to touch arrive as its argument.
using ExecuteFn = std::function<void(const PassResources&)>;

// The order compile() proved: pass indices, numbered by addPass declaration order, arranged so
// every producer precedes its consumers. Serial -- the graph models one queue.
struct Schedule {
    std::vector<uint32_t> passes;
};

// A frame's passes, declared as resources rather than as commands.
//
// A caller imports the textures and buffers the frame already owns, declares passes over them,
// exports the results it needs, and compiles. compile() proves the declarations form a directed
// acyclic graph and answers with the order to run them in, so a mis-declared frame fails on the CPU
// with a message instead of on the GPU as a hazard.
//
// The graph borrows everything: an imported rhi::Texture or rhi::Buffer must outlive it, and the
// handles it issues mean nothing once it is gone. Declaring a fresh graph per frame is the intended
// use -- it owns no GPU memory and creates no GPU objects.
//
// Resources are imported, never created. Transient allocation, aliasing, culling, and pass merging
// are deliberately absent: this is a validating declaration layer over resources the frame already
// holds, and it grows only when a feature needs it to.
class RenderGraph {
public:
    // Brings an existing texture into the graph as version 0. `format` is declared here because
    // rhi::Texture does not report its own, and it is what the attachment rules check -- the caller
    // is answerable for it matching the texture it created. `name` appears in validation messages
    // and is copied. `texture` must outlive the graph.
    GraphTexture importTexture(rhi::Texture& texture, rhi::Format format, std::string_view name);

    // Brings an existing buffer into the graph as version 0, on importTexture's terms. Buffers
    // carry no format because no rule inspects one.
    GraphBuffer importBuffer(rhi::Buffer& buffer, std::string_view name);

    // Declares a pass. `label` names it in validation messages and is copied; `execute` must be
    // non-empty. Declaration order is the pass index space Schedule reports, and is the tie-break
    // compile() uses between passes that do not depend on each other.
    //
    // Nothing is validated here beyond handle sanity, because a pass may legitimately name a
    // version that a later-declared pass produces. Everything else is compile()'s answer.
    void addPass(std::string_view label, PassDesc desc, ExecuteFn execute);

    // Roots a result so it survives the frame. The version must be one a pass produced: exporting
    // an imported texture that no pass ever wrote fails compilation, since the graph produced
    // nothing to root.
    void exportTexture(GraphTexture handle);

    // Validates every declaration and answers with the serial order to execute the passes in.
    //
    // Hard failures, in the order they are reported: an attachment whose format does not match its
    // role, or a colour and depth attachment of differing extents; two passes writing one version;
    // a declaration naming a version no pass writes (read before write); an export of a version no
    // pass wrote; and a cycle, named by the passes it involves.
    GraphResult<Schedule> compile() const;

    // The resources pass `passIndex` declared, for the pass body to resolve handles through.
    // Resolution depends on declarations alone, so this is answerable before and independently of
    // compile(). `passIndex` must name a declared pass.
    PassResources passResources(uint32_t passIndex) const;

private:
    friend class PassResources;

    enum class ResourceKind { Texture, Buffer };

    struct Resource {
        ResourceKind kind = ResourceKind::Texture;
        std::string name;
        rhi::Texture* texture = nullptr;
        rhi::Buffer* buffer = nullptr;
        rhi::Format format = rhi::Format::Unknown;
    };

    struct Pass {
        std::string label;
        PassDesc desc;
        ExecuteFn execute;
    };

    // Whether pass `passIndex` named exactly this resource version anywhere in its PassDesc.
    bool passDeclares(uint32_t passIndex, uint32_t resourceIndex, uint32_t version) const;

    std::vector<Resource> m_resources;
    std::vector<Pass> m_passes;
    std::vector<GraphTexture> m_exports;
};

} // namespace lmx::render
