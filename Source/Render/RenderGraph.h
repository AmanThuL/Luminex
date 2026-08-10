//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.h
/// @brief Declares the validating render graph and its resource handles.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"
#include "Render/TransientPool.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::render {

/// Validating, serial render-pass declaration graph.
class RenderGraph;

/// A declaration the graph cannot honour: two passes writing one version, a read of contents
/// nothing produces, a depth texture bound as a colour attachment. These are expected failures
/// rather than assertions because a caller assembles passes from scene data and can reach them
/// honestly. Misuse no honest declaration can reach -- a handle whose index names no resource, a
/// GraphTexture naming an imported buffer -- stays LMX_ASSERT.
///
/// The message names the offending pass and resource: "the graph is invalid" is not actionable,
/// "pass 'lmx.pass.scene' declares a read of texture 'shadowMap' version 1, which no pass writes"
/// is.
struct GraphError {
    std::string message; ///< Validation failure naming the offending pass and resource.
};

/// Expected result returned by render-graph validation operations.
template <typename T>
using GraphResult = std::expected<T, GraphError>;

/// A logical texture at one point in its history.
///
/// `index` names the resource for the graph's lifetime; `version` names its contents. importTexture
/// yields version 0 -- the contents the caller already put there -- and a pass declaring a write of
/// version v produces version v + 1. A read names the version it consumes, and that naming is the
/// entire dependency model: the pass producing a version runs before every pass naming it.
///
/// Handles are values that own nothing. They are meaningful only to the RenderGraph that issued
/// them, and only for as long as that graph lives.
struct GraphTexture {
    uint32_t index = 0;   ///< Graph-local resource index.
    uint32_t version = 0; ///< Logical contents version.
    /// Compares graph-local resource identity and version.
    friend bool operator==(GraphTexture, GraphTexture) = default;
};

/// The buffer counterpart of GraphTexture, with the same index/version contract. Textures and
/// buffers share one index space, so a GraphBuffer holding a texture's index is misuse and asserts.
struct GraphBuffer {
    uint32_t index = 0;   ///< Graph-local resource index.
    uint32_t version = 0; ///< Logical contents version.
    /// Compares graph-local resource identity and version.
    friend bool operator==(GraphBuffer, GraphBuffer) = default;
};

/// The handle naming a resource's contents after a pass writes `handle`. Version arithmetic is
/// deterministic and public so a caller names a pass's result without threading a value back out of
/// every addPass call.
constexpr GraphTexture nextVersion(GraphTexture handle) {
    return {.index = handle.index, .version = handle.version + 1};
}
/// Returns the buffer handle naming the version produced by writing `handle`.
constexpr GraphBuffer nextVersion(GraphBuffer handle) {
    return {.index = handle.index, .version = handle.version + 1};
}

/// Describes a texture the graph creates, owns, and destroys within one frame.
///
/// It mirrors rhi::TextureDesc minus the two things a transient cannot have: initial contents, and
/// CPU-visible storage. A transient is device-private memory the graph places in a heap, so a
/// caller that needs to read a result back imports a texture of its own instead. The name is passed
/// beside the descriptor, exactly as importTexture takes one, and becomes the GPU object's label.
struct TransientTextureDesc {
    uint32_t width = 0;                              ///< Extent in texels.
    uint32_t height = 0;                             ///< Extent in texels.
    rhi::Format format = rhi::Format::Unknown;       ///< Pixel format.
    rhi::TextureKind kind = rhi::TextureKind::Tex2D; ///< Two-dimensional or cubemap.
    uint32_t mipLevels = 1;                          ///< Mip levels allocated for each face.
    bool renderTarget = false;                       ///< Enables render-target use.
    bool sampled = false;                            ///< Enables shader reads.
    bool storageRead = false;                        ///< Enables storage-binding reads.
    bool storageWrite = false;                       ///< Enables storage-binding writes.
};

/// The buffer counterpart of TransientTextureDesc, on the same terms.
struct TransientBufferDesc {
    uint64_t size = 0;         ///< Allocation size in bytes.
    bool storageRead = false;  ///< Enables storage-binding reads.
    bool storageWrite = false; ///< Enables storage-binding writes.
};

/// What kind of work a declared pass encodes, and therefore which RHI pass scope execute() opens
/// for it. The kind is fixed by the declaration path -- addPass, addComputePass, addCopyPass -- so
/// a pass cannot carry a kind its declaration does not fit: attachments exist only on the raster
/// path, and copy source/destination uses only on the copy one.
enum class PassKind {
    Raster,  ///< Encoded as a render pass over its attachments.
    Compute, ///< Encoded as a compute pass.
    Copy     ///< Encoded as a copy pass.
};

/// What a pass does with one declared resource version. It is what the graph's validation messages
/// name, what decides read from write, and what the derived barriers are built from -- a compute
/// read and a raster read order differently on the way in, and the role plus the pass kind is what
/// says which.
enum class UseRole {
    Read,            ///< Consumed without being written.
    Write,           ///< Written other than as an attachment.
    ColorAttachment, ///< Written as the pass's colour attachment.
    DepthAttachment, ///< Written as the pass's depth attachment.
    CopySource,      ///< Read by a copy command.
    CopyDestination  ///< Written by a copy command.
};

/// Names a role in validation messages and frame dumps, in the wording the graph's diagnostics use.
std::string_view roleName(UseRole role);

/// Names a format as its enumerator, so a message says D32Float rather than an integer. It lives
/// here rather than in the RHI because naming an enumerator is a diagnostic concern and the graph
/// is what has the diagnostics.
std::string_view formatName(rhi::Format format);

/// Formats a subresource range as `mips[first..last] layers[first..last]`, with the last bound left
/// open -- `mips[1..]` -- where the range runs to the end of the chain. Shared by the graph's
/// validation messages and the frame dump so one range reads the same in both.
std::string describeRange(const rhi::TextureSubresourceRange& range);

/// A texture version together with the subresources of it a pass touches.
///
/// The default range is the whole resource, which is what a declaration with no subresource detail
/// means and what every raster pass here declares; a narrower one is how a pass addresses single
/// mips of a chain. Versions stay whole-resource either way (spec §6): writing any subresource
/// produces a new version of the whole texture, and the subresources the pass did not write carry
/// the previous version's contents forward.
///
/// A GraphTexture converts implicitly, so `textureReads.push_back(handle)` still declares the whole
/// resource and existing passes need no range of their own.
struct TextureUseDesc {
    GraphTexture handle;                ///< The version the pass touches.
    rhi::TextureSubresourceRange range; ///< Subresources of it the pass touches.

    /// Declares the whole of `handle`.
    TextureUseDesc(GraphTexture texture) : handle(texture) {}
    /// Declares `subresources` of `handle`.
    TextureUseDesc(GraphTexture texture, const rhi::TextureSubresourceRange& subresources)
        : handle(texture), range(subresources) {}
};

/// What an attachment does with the version it names on entry. Clear starts from the clear value;
/// Load keeps the existing contents, which makes the attachment a read of that version as well as a
/// write of it. Either way the pass is ordered after whatever produced that version -- a clear that
/// overtook its producer would erase the wrong contents.
enum class LoadOp {
    Clear, ///< Replace prior contents with the attachment clear value.
    Load,  ///< Preserve and consume prior attachment contents.
};

/// Whether the version the pass produces outlives the pass. Store keeps it readable by later passes
/// and by exportTexture; Discard states that nothing consumes it, as for a depth buffer used only
/// for the pass's own hidden-surface removal.
enum class StoreOp {
    Store,   ///< Preserve produced contents for later consumers.
    Discard, ///< Permit produced contents to expire with the pass.
};

/// The pass's single colour attachment. `handle` is the version the pass writes, so the pass
/// produces nextVersion(handle). The named resource must have been imported with a
/// colour-renderable format, and must share its extent with the depth attachment when the pass
/// declares both.
struct ColorAttachment {
    GraphTexture handle;            ///< Input version this attachment overwrites.
    LoadOp load = LoadOp::Clear;    ///< Whether prior contents are preserved.
    StoreOp store = StoreOp::Store; ///< Whether produced contents remain readable.
    /// Applies when load == LoadOp::Clear. The hardware clear writes it into the target unchanged,
    /// so it is a value in whatever encoding that target holds.
    float clearColor[4] = {0.f, 0.f, 0.f, 1.f};
};

/// The pass's single depth attachment, with ColorAttachment's write-and-produce contract. The named
/// resource must have been imported with a depth format.
struct DepthAttachment {
    GraphTexture handle;              ///< Input version this attachment overwrites.
    LoadOp load = LoadOp::Clear;      ///< Whether prior contents are preserved.
    StoreOp store = StoreOp::Discard; ///< Whether produced contents remain readable.
    /// 0 is the far plane, because depth is reversed everywhere above this layer (Camera.cpp
    /// derives it). It differs from rhi::RenderPassDesc's 1.0, which is the API's neutral default
    /// and belongs to no convention; this one belongs to the renderer's, so a pass that omits it
    /// clears to the value its Greater test will accept anything against rather than to the value
    /// that would reject every fragment it draws.
    float clearDepth = 0.0f;
};

/// Everything one raster pass touches. The graph validates and orders passes from this alone: a
/// resource absent here is a resource the pass may not use, and PassResources refuses to resolve
/// it.
struct PassDesc {
    /// Versions the pass consumes without writing.
    std::vector<TextureUseDesc> textureReads; ///< Sampled or otherwise read textures.
    std::vector<GraphBuffer> bufferReads;     ///< Buffers consumed by the pass.
    /// At most one of each, matching the single-colour-attachment render pass this RHI models. A
    /// pass may declare neither, either, or both.
    std::optional<ColorAttachment> color; ///< Optional color target.
    std::optional<DepthAttachment> depth; ///< Optional depth target.
    /// Writes that are not attachments. Each names the version it consumes and produces the next.
    std::vector<TextureUseDesc> textureWrites; ///< Non-attachment texture writes.
    std::vector<GraphBuffer> bufferWrites;     ///< Non-attachment buffer writes.
};

/// Everything one compute pass touches, on PassDesc's terms minus the attachments a compute pass
/// has nowhere to put. Reads and writes are the storage bindings the pass's dispatches make, and
/// the ranges are the subresources those bindings address -- a downsample step reading mip N and
/// writing mip N + 1 declares the two as disjoint ranges of one texture.
struct ComputePassDesc {
    std::vector<TextureUseDesc> textureReads;  ///< Textures the dispatches read.
    std::vector<GraphBuffer> bufferReads;      ///< Buffers the dispatches read.
    std::vector<TextureUseDesc> textureWrites; ///< Textures the dispatches write.
    std::vector<GraphBuffer> bufferWrites;     ///< Buffers the dispatches write.
};

/// Everything one copy pass touches. A copy has no bindings, so its vocabulary is sources and
/// destinations rather than reads and writes; the versioning is identical -- a destination names
/// the version it overwrites and produces the next one, and a source names the version it consumes.
struct CopyPassDesc {
    std::vector<TextureUseDesc> textureSources;      ///< Textures the copies read from.
    std::vector<GraphBuffer> bufferSources;          ///< Buffers the copies read from.
    std::vector<TextureUseDesc> textureDestinations; ///< Textures the copies write into.
    std::vector<GraphBuffer> bufferDestinations;     ///< Buffers the copies write into or fill.
};

/// What a pass may touch while it runs.
///
/// Resolution is exact: a handle resolves only when the pass named that (index, version) pair in
/// its PassDesc, as a read, as an attachment, or as a write. Any other handle -- including the
/// right resource at the wrong version -- is a dependency the pass failed to declare, so it is
/// reported instead of resolved. Resolving it would hand the pass a resource the schedule never
/// ordered it against.
///
/// A borrowed view: it must not outlive the RenderGraph that produced it.
class PassResources {
public:
    /// The imported texture behind a declared handle, never null on success.
    GraphResult<rhi::Texture*> texture(GraphTexture handle) const;
    /// The imported buffer behind a declared handle, never null on success.
    GraphResult<rhi::Buffer*> buffer(GraphBuffer handle) const;

private:
    friend class RenderGraph;
    PassResources(const RenderGraph& graph, uint32_t passIndex)
        : m_graph(&graph), m_passIndex(passIndex) {}

    const RenderGraph* m_graph;
    uint32_t m_passIndex;
};

/// What a pass does when the schedule reaches it. Stored at declaration; the resources it is
/// allowed to touch arrive as its argument.
using ExecuteFn = std::function<void(const PassResources&)>;

/// The order compile() proved: pass indices, numbered by declaration order, arranged so every
/// producer precedes its consumers. Serial -- the graph models one queue.
struct Schedule {
    std::vector<uint32_t> passes; ///< Pass indices in validated execution order.
};

/// Which kind of resource a compiled-frame entry names. Textures and buffers share one index space,
/// so the kind is what says which half of an entry to read.
enum class GraphResourceKind {
    Texture, ///< The entry names an imported texture.
    Buffer   ///< The entry names an imported buffer.
};

/// How a rooted result leaves the frame.
///
/// The three are the whole list on purpose (spec §7): there is no generic side-effect flag a pass
/// can raise to exempt itself from culling, because such a flag is a way to keep work alive without
/// saying what it is for, and a frame nobody can read the output of is a frame with a missing sink.
enum class SinkKind {
    Export,  ///< Read by the caller after the frame, through the resource it imported.
    Present, ///< Handed to the swapchain as the image to present.
    Readback ///< Copied back to the CPU once the frame's work completes.
};

/// One imported resource, in import order -- the index space every other compiled-frame entry uses.
struct DebugResource {
    std::string name;                                    ///< The name it was imported under.
    GraphResourceKind kind = GraphResourceKind::Texture; ///< Texture or buffer.
    rhi::Format format = rhi::Format::Unknown;           ///< Declared format; Unknown for a buffer.
};

/// One declared use of one resource version by one pass, in the order the pass declared it.
struct DebugUse {
    uint32_t resource = 0;              ///< Index into CompiledFrameDebug::resources.
    uint32_t version = 0;               ///< The version the pass named.
    UseRole role = UseRole::Read;       ///< What the pass does with it.
    rhi::TextureSubresourceRange range; ///< Subresources covered; whole-resource for a buffer.
};

/// Why a compiled frame left a declared pass out of its schedule.
///
/// Both reasons are honest failures of a frame to say what it wanted, and they are fixed
/// differently: a pass that produces nothing is missing a write declaration, and a pass no sink
/// reaches is missing a sink -- or is genuinely dead work the frame is right to drop.
enum class CullReason {
    ProducesNothing, ///< The pass writes no version, so nothing can name its output.
    NoSinkReachesIt  ///< No sink depends, however indirectly, on any version it produces.
};

/// One declared pass, in declaration order -- culled passes included, since a frame's declarations
/// are what an observer needs to see and a culled pass is the most interesting kind.
struct DebugPass {
    std::string label;                    ///< The label it was declared with.
    PassKind kind = PassKind::Raster;     ///< Which declaration path declared it.
    std::vector<DebugUse> uses;           ///< Every resource version it named.
    std::optional<CullReason> cullReason; ///< Why it was culled, or empty if it is scheduled.
};

/// One declared sink, in declaration order. Sinks are the only culling roots: a version no sink
/// reaches, directly or through the passes that consume it, is a version nothing in the frame asked
/// for.
struct DebugSink {
    SinkKind kind = SinkKind::Export; ///< How the result leaves the frame.
    GraphResourceKind resourceKind = GraphResourceKind::Texture; ///< Texture or buffer.
    uint32_t resource = 0; ///< Index into CompiledFrameDebug::resources.
    uint32_t version = 0;  ///< The version the sink roots.
};

/// One barrier the graph derived, positioned by the pass it precedes.
///
/// `kind` says which of the two use pairs below is the meaningful one: a texture transition carries
/// its `range` and the texture uses, a buffer transition the buffer uses. They share one struct
/// because the transitions of a frame are one ordered sequence, and splitting them by resource kind
/// would lose which came first.
struct DebugTransition {
    uint32_t beforePass = 0; ///< Index of the pass the barrier precedes.
    uint32_t resource = 0;   ///< Index into CompiledFrameDebug::resources.
    GraphResourceKind kind = GraphResourceKind::Texture; ///< Which use pair applies.
    rhi::TextureSubresourceRange range;                  ///< Subresources covered; textures only.
    rhi::TextureUse textureFrom = rhi::TextureUse::RenderTarget; ///< Producing texture use.
    rhi::TextureUse textureTo = rhi::TextureUse::ShaderRead;     ///< Consuming texture use.
    rhi::BufferUse bufferFrom = rhi::BufferUse::StorageWrite;    ///< Producing buffer use.
    rhi::BufferUse bufferTo = rhi::BufferUse::StorageRead;       ///< Consuming buffer use.
    /// Set when this is a transient reuse boundary rather than a read-after-write of one logical
    /// resource: `resource` is placed in memory that the named transient held until this point, so
    /// the barrier orders that resource's last use against this one's first. It covers the whole
    /// resource whatever the two declared, because the hazard is over the bytes, not over the
    /// subresources either side happened to name.
    std::optional<uint32_t> aliasedFrom;
};

/// One graph-created transient's lifetime and the place in the frame's transient heap it was
/// assigned.
///
/// A transient no scheduled pass touches -- one whose only readers were culled -- is reported with
/// `used` false and no assignment at all: it is a declaration the frame did not need, and giving it
/// memory would be paying for the pass that was dropped.
struct DebugTransient {
    uint32_t resource = 0;  ///< Index into CompiledFrameDebug::resources.
    bool used = false;      ///< Whether any scheduled pass touches it, and so whether it is placed.
    uint32_t firstPass = 0; ///< First scheduled pass that touches it.
    uint32_t lastPass = 0;  ///< Last scheduled pass that touches it.
    uint64_t offset = 0;    ///< Byte offset of its placement within the frame's transient heap.
    uint64_t size = 0;      ///< Bytes the RHI reports the descriptor occupies in a heap.
    uint64_t alignment = 0; ///< Alignment the RHI reports its heap offset must satisfy.
    bool aliases = false;   ///< Whether it took memory an earlier transient's lifetime had freed.
};

/// What a frame's transients cost and what aliasing saved.
///
/// `aliasSavings` is `requested - highWater`, floored at zero: the alignment padding a packing
/// forces counts against the saving, because it is memory the heap really holds. With pooling off
/// every transient gets its own bytes, so the saving is zero by construction and the high-water
/// mark is the whole footprint.
struct TransientMemory {
    uint64_t requested = 0;    ///< Sum of every placed transient's size.
    uint64_t highWater = 0;    ///< Bytes the frame's transient heap must provide.
    uint64_t aliasSavings = 0; ///< Bytes the heap does not have to hold because lifetimes reused.
};

/// Everything compilation decided about one frame, in a form an observer can read without the graph
/// that produced it: what was imported, what each pass declared, the order that was proved, and the
/// barriers derived from it.
///
/// It holds only values the compiler produces deterministically. GPU timings and driver-reported
/// values are joined to it by an observer, never carried in it, so the same declarations always
/// compile to the same record.
struct CompiledFrameDebug {
    std::vector<DebugResource> resources;     ///< Declared resources, in declaration order.
    std::vector<DebugSink> sinks;             ///< Declared sinks, in declaration order.
    std::vector<DebugPass> passes;            ///< Declared passes, in declaration order.
    Schedule schedule;                        ///< Surviving pass indices in execution order.
    std::vector<DebugTransition> transitions; ///< Derived barriers, in the order they are emitted.
    /// Graph-created transients, in declaration order, with their lifetimes and assignments.
    std::vector<DebugTransient> transients;
    TransientMemory memory;      ///< What the transients cost and what aliasing saved.
    bool poolingEnabled = false; ///< Whether the assignment above was allowed to reuse memory.
};

/// A compiled frame together with the frame it belongs to.
///
/// `frameId` is the RHI device's number for the frame being recorded (rhi::Device::frameNumber()),
/// which is the same numbering rhi::Device::passTimingsFrame() reports -- so an observer holding
/// records for the frames in flight joins a retired frame's timings to the record that describes it
/// by comparing the two numbers rather than by guessing at a lag.
struct CompiledFrameRecord {
    uint64_t frameId = 0;     ///< The device frame number this frame was compiled for.
    CompiledFrameDebug debug; ///< What compilation decided.
};

/// A frame's passes, declared as resources rather than as commands.
///
/// A caller imports the textures and buffers the frame already owns, declares passes over them,
/// exports the results it needs, and compiles. compile() proves the declarations form a directed
/// acyclic graph and answers with the order to run them in, so a mis-declared frame fails on the
/// CPU with a message instead of on the GPU as a hazard.
///
/// The graph borrows what it does not own: an imported rhi::Texture or rhi::Buffer must outlive it,
/// and the handles it issues mean nothing once it is gone. Declaring a fresh graph per frame is the
/// intended use -- it holds no GPU memory of its own between frames.
///
/// A resource is either imported or transient (ADR 0008). An imported one is the caller's for the
/// caller's own reasons -- it persists, it can be exported, read back, or presented, and it is
/// never pooled or aliased. A transient one is the graph's for exactly one frame: it is declared by
/// descriptor, placed in a TransientPool's heap, may share bytes with another transient whose
/// lifetime does not overlap it, and cannot leave the frame at all.
class RenderGraph {
public:
    /// A graph that declares no transients, and asserts if one is declared on it.
    RenderGraph() = default;

    /// A graph whose transients are placed in `transients`, which must outlive it. The pool's
    /// device is also what sizes the frame's transient descriptors, so a graph can plan its whole
    /// heap layout at compile time without creating anything.
    explicit RenderGraph(TransientPool& transients) : m_transients(&transients) {}

    /// Brings an existing texture into the graph as version 0. `format` is declared here because
    /// rhi::Texture does not report its own, and it is what the attachment rules check -- the
    /// caller is answerable for it matching the texture it created. `name` appears in validation
    /// messages and is copied. `texture` must outlive the graph.
    GraphTexture importTexture(rhi::Texture& texture, rhi::Format format, std::string_view name);

    /// Brings an existing buffer into the graph as version 0, on importTexture's terms. Buffers
    /// carry no format because no rule inspects one.
    GraphBuffer importBuffer(rhi::Buffer& buffer, std::string_view name);

    /// Declares a texture the graph creates for this frame and nothing else, as version 0.
    ///
    /// Version 0 of a transient is uninitialised memory rather than contents, so naming it as
    /// anything but a write -- a read, a copy source, a loaded attachment -- fails compilation.
    /// That is what makes the picture the same with pooling on and off: nothing can observe what
    /// the previous occupant of those bytes left behind.
    ///
    /// The graph must have been constructed with a TransientPool; declaring a transient without one
    /// is misuse and asserts. `name` is copied and becomes the placed resource's label.
    GraphTexture createTexture(const TransientTextureDesc& desc, std::string_view name);

    /// The buffer counterpart of createTexture, on the same terms.
    GraphBuffer createBuffer(const TransientBufferDesc& desc, std::string_view name);

    /// Whether compilation may let two transients whose lifetimes do not overlap share bytes.
    ///
    /// On by default. Off gives every transient its own memory, which costs the frame the alias
    /// savings and nothing else: the picture is identical either way, because a transient's
    /// contents are undeclarable before its first write. It exists so the two can be compared
    /// against each other -- in a test, and from the editor's render settings.
    void setPoolingEnabled(bool enabled) { m_poolingEnabled = enabled; }

    /// Declares a raster pass. `label` names it in validation messages and is copied; `execute`
    /// must be non-empty. Declaration order is the pass index space Schedule reports, and is the
    /// tie-break compile() uses between passes that do not depend on each other -- the three
    /// declaration paths share one index space, in the order they were called.
    ///
    /// Nothing is validated here beyond handle sanity, because a pass may legitimately name a
    /// version that a later-declared pass produces. Everything else is compile()'s answer.
    void addPass(std::string_view label, PassDesc desc, ExecuteFn execute);

    /// Declares a compute pass, on addPass's terms. execute() opens an RHI compute pass around the
    /// body, so the body records dispatches rather than draws.
    void addComputePass(std::string_view label, ComputePassDesc desc, ExecuteFn execute);

    /// Declares a copy pass, on addPass's terms. execute() opens an RHI copy pass around the body,
    /// so the body records copies and fills and binds nothing.
    void addCopyPass(std::string_view label, CopyPassDesc desc, ExecuteFn execute);

    /// Roots a result so it survives the frame, for the caller to read through the texture it
    /// imported. The version must be one a pass produced: exporting an imported texture that no
    /// pass ever wrote fails compilation, since the graph produced nothing to root.
    ///
    /// Rooting is also what keeps work alive. A pass reaches a sink or it is culled, so a frame
    /// whose result nothing exports, presents, or reads back schedules nothing at all.
    void exportTexture(GraphTexture handle);

    /// The buffer counterpart of exportTexture, on the same terms -- the way a buffer a pass filled
    /// stays meaningful past the frame that filled it.
    void exportBuffer(GraphBuffer handle);

    /// Roots the version handed to the swapchain. It is a sink of its own rather than an export
    /// because presentation is what the frame is for, and because a graph must never infer that a
    /// result is live from the fact that something outside it happens to be looking.
    void presentTexture(GraphTexture handle);

    /// Roots a version the caller reads back to the CPU once the frame's work completes. Stated on
    /// the graph so the passes producing it survive culling; the readback itself is the caller's.
    void readbackTexture(GraphTexture handle);

    /// The buffer counterpart of readbackTexture.
    void readbackBuffer(GraphBuffer handle);

    /// Validates every declaration and answers with the serial order to execute the passes in.
    ///
    /// Hard failures, in the order they are reported: an attachment whose format does not match its
    /// role, or a colour and depth attachment of differing extents; a subresource range that is
    /// empty or runs past the texture it names; one pass reading and writing a texture through
    /// overlapping ranges; a transient consumed at version 0, whose contents nothing produced; a
    /// sink naming a transient, which cannot outlive the frame; two passes writing one version; a
    /// declaration naming a version no pass writes (read before write); a sink naming a version no
    /// pass wrote; and a cycle, named by the passes it involves. Validation covers every declared
    /// pass, culled ones included: a mis-declared pass is mis-declared whether or not the frame
    /// needs it.
    ///
    /// The schedule holds only the passes a sink reaches. Liveness runs backwards from the declared
    /// sinks alone and follows the versions each live pass names, so it is a function of the
    /// declarations and answers the same way every time.
    ///
    /// Subresource ranges narrow what a pass touches, not what a version covers (spec §6): two
    /// passes writing disjoint ranges of one version are still a double write, because the second
    /// one has to declare itself over the first's output version for the order to be stated at all.
    ///
    /// Compilation also answers where each transient lives. A transient's lifetime is the span of
    /// the schedule between the first and last surviving pass that names it; two transients whose
    /// lifetimes do not overlap may share bytes when their descriptors agree on every axis that
    /// decides their layout -- resource kind, format, extent, mip count, usage, and the size and
    /// alignment the RHI reports for them. Storage mode needs no comparison because a transient is
    /// always device-private. Offsets are assigned first-fit in lifetime order, tie-broken by
    /// declaration order, so the layout is a function of the declarations alone and two identical
    /// frames plan identically.
    GraphResult<Schedule> compile() const;

    /// The same compilation, answering with everything it decided rather than the order alone.
    ///
    /// `frameId` is the device frame number the record is stamped with; it takes no part in
    /// compilation, so two frames declaring the same passes compile to records differing in nothing
    /// else. Failures are compile()'s, in compile()'s order.
    GraphResult<CompiledFrameRecord> compileFrame(uint64_t frameId) const;

    /// Validates the declarations and runs them: every scheduled pass becomes one RHI pass of its
    /// own kind, labelled with the pass's name, and the pass body is called between that scope's
    /// begin and end with the resources it declared. A raster pass's scope is built from its
    /// attachments; a compute or copy pass's carries only the label.
    ///
    /// Compilation happens here rather than in the caller so that nothing can reach the GPU
    /// unvalidated. A frame that fails to compile is programmer error and aborts with compile()'s
    /// message; a caller that wants the failure as a value calls compile() itself.
    ///
    /// The frame's transients are placed before anything is encoded: the pool is asked for a heap
    /// of the compiled high-water mark, and every used transient is created at the offset the plan
    /// assigned it. They live until their frame slot comes round again, which is the pool's
    /// contract, so nothing here has to know when the GPU finished with them.
    ///
    /// The synchronisation this emits is read-after-write, derived from the declarations alone: a
    /// resource an earlier pass wrote and a later pass reads gets a barrier before that pass, from
    /// the use that wrote it to the use that reads it. Write-after-write within one logical
    /// resource is ordered by the version chain and emits no barrier of its own. An export emits
    /// nothing either -- it roots a result for the caller to read once the queue drains, which is
    /// not another pass reading it.
    ///
    /// Reuse of transient memory is the one hazard the version chain cannot state, because the two
    /// sides are different logical resources: where a transient takes bytes an earlier one held, a
    /// whole-resource barrier is emitted before its first pass, from the earlier transient's last
    /// use to this one's first. It is emitted whatever ranges either side declared -- the hazard is
    /// over the memory, not over the subresources -- and it is what makes an aliased frame match an
    /// unaliased one.
    ///
    /// Every reader's declared subresources are covered: a later reader is left unbarriered only
    /// when an already-emitted barrier for that same write named a range enclosing the whole of
    /// what it reads. A barrier orders the passes it sits between, so one that named mip 0 for an
    /// earlier reader does not order a later reader of mip 1, and that reader gets its own.
    /// Whole-resource declarations -- what every raster pass here makes -- yield one whole-resource
    /// barrier that encloses every later whole-resource read, so the common frame still transitions
    /// once. Writing the resource again clears what was covered and owes the transition afresh.
    ///
    /// A raster pass must declare an attachment; a pass with no attachment is a compute or copy
    /// pass and is declared through the path that says so. The RHI's own attachment rules bind here
    /// too and are asserted with the offending pass named: a colour attachment is always stored, a
    /// depth attachment always clears, a pass carrying both clears both, and a depth-only pass must
    /// store its depth.
    ///
    /// Answers with the record compileFrame(`frameId`) produced, which is the record of what this
    /// call encoded: the barriers listed in it are the barriers emitted, because both come from the
    /// one compilation rather than from two derivations that could drift apart.
    CompiledFrameRecord execute(rhi::CommandList& commands, uint64_t frameId);

    /// The resources pass `passIndex` declared, for the pass body to resolve handles through.
    /// Resolution depends on declarations alone, so this is answerable before and independently of
    /// compile(). `passIndex` must name a declared pass.
    PassResources passResources(uint32_t passIndex) const;

private:
    friend class PassResources;

    enum class ResourceKind { Texture, Buffer };

    // One declared resource, imported or transient. The shape fields are carried here rather than
    // read back off the rhi::Texture because a transient has no texture until execute() places it,
    // and the range and attachment rules have to answer the same way for both kinds.
    struct Resource {
        ResourceKind kind = ResourceKind::Texture;
        std::string name;
        // Null for a transient until execute() places it; borrowed for an import.
        rhi::Texture* texture = nullptr;
        rhi::Buffer* buffer = nullptr;
        rhi::Format format = rhi::Format::Unknown;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        bool transient = false;
        // Set for transients only, and the descriptor the placed resource is created from. The
        // label is filled in from `name` at placement time rather than stored, so no view into
        // this struct's own string can outlive a reallocation of the resource list.
        TransientTextureDesc textureDesc;
        TransientBufferDesc bufferDesc;
    };

    // Where one transient sits in the frame's heap, alongside the lifetime that justified it.
    // Carried separately from Resource because it is compilation's answer, not a declaration.
    struct TransientPlan {
        uint32_t resource = 0;
        bool used = false;
        uint32_t firstPosition = 0; ///< Position in the schedule, not a pass index.
        uint32_t lastPosition = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t alignment = 0;
        bool aliases = false;
        // The transient whose bytes this one took, when it took any.
        std::optional<uint32_t> aliasedFrom;
    };

    struct AliasPlan {
        std::vector<TransientPlan> transients;
        TransientMemory memory;
    };

    // One resource version, flattened out of whichever declaration field named it. Validation, the
    // dependency edges, the derived barriers, and PassResources all read this single flattening, so
    // no rule can quietly overlook one of the declaration forms.
    struct Declaration {
        uint32_t resource = 0;
        uint32_t version = 0;
        UseRole role = UseRole::Read;
        rhi::TextureSubresourceRange range;
        bool isWrite = false;
    };

    struct Pass {
        std::string label;
        PassKind kind = PassKind::Raster;
        // Attachments are kept whole because execute() needs their load, store, and clear values;
        // everything else about a pass is in its declarations.
        std::optional<ColorAttachment> color;
        std::optional<DepthAttachment> depth;
        ExecuteFn execute;
        std::vector<Declaration> declarations;
    };

    // Asserts every handle in a texture or buffer declaration names a resource of that kind.
    void checkTexture(GraphTexture handle) const;
    void checkBuffer(GraphBuffer handle) const;

    // Appends one flattened declaration per handle in a use list.
    void flattenTextures(std::vector<Declaration>& into, const std::vector<TextureUseDesc>& uses,
                         UseRole role) const;
    void flattenBuffers(std::vector<Declaration>& into, const std::vector<GraphBuffer>& handles,
                        UseRole role) const;

    // Whether pass `passIndex` named exactly this resource version anywhere in its declarations.
    bool passDeclares(uint32_t passIndex, uint32_t resourceIndex, uint32_t version) const;

    // The one place read-after-write barriers are decided. execute() emits what this recorded
    // rather than deriving its own, so the record and the command stream cannot disagree. The plan
    // is taken as well as the schedule because a reuse boundary is a barrier the declarations
    // alone cannot show.
    std::vector<DebugTransition> deriveTransitions(const Schedule& schedule,
                                                   const AliasPlan& plan) const;

    // Lifetimes over the schedule, then first-fit offsets over the lifetimes. Deterministic in
    // both halves; see compile()'s contract for the rules it implements.
    AliasPlan planTransients(const Schedule& schedule) const;

    // The RHI descriptors a transient's Resource compiles to, labelled with its name.
    rhi::TextureDesc textureDescOf(const Resource& resource) const;
    rhi::BufferDesc bufferDescOf(const Resource& resource) const;

    // Reserves the frame's transient heap and places every used transient in it, filling in the
    // resource pointers the pass bodies resolve. Failure is programmer error and aborts, on
    // execute()'s terms.
    void placeTransients(const CompiledFrameDebug& debug);

    struct Sink {
        SinkKind kind = SinkKind::Export;
        ResourceKind resourceKind = ResourceKind::Texture;
        uint32_t resource = 0;
        uint32_t version = 0;
    };

    // Records one sink of any kind; the five public declaration paths differ only in the kind.
    void addSink(SinkKind kind, ResourceKind resourceKind, uint32_t resource, uint32_t version);

    std::vector<Resource> m_resources;
    std::vector<Pass> m_passes;
    std::vector<Sink> m_sinks;
    // Null for an import-only graph; the pool a transient is placed in and the device its
    // descriptors are sized against.
    TransientPool* m_transients = nullptr;
    bool m_poolingEnabled = true;
};

} // namespace lmx::render
