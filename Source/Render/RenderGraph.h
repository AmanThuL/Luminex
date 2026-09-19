//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.h
/// @brief Declares the validating render graph and its resource handles.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <rojoRHI/RHI.h>
#include "Render/CompiledFrameRecord.h"
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
/// It mirrors rojoRHI::TextureDesc minus the two things a transient cannot have: initial contents, and
/// CPU-visible storage. A transient is device-private memory the graph places in a heap, so a
/// caller that needs to read a result back imports a texture of its own instead. The name is passed
/// beside the descriptor, exactly as importTexture takes one, and becomes the GPU object's label.
struct TransientTextureDesc {
    uint32_t width = 0;                              ///< Extent in texels.
    uint32_t height = 0;                             ///< Extent in texels.
    rojoRHI::Format format = rojoRHI::Format::Unknown;       ///< Pixel format.
    rojoRHI::TextureKind kind = rojoRHI::TextureKind::Tex2D; ///< Two-dimensional or cubemap.
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
    rojoRHI::TextureSubresourceRange range; ///< Subresources of it the pass touches.

    /// Declares the whole of `handle`.
    TextureUseDesc(GraphTexture texture) : handle(texture) {}
    /// Declares `subresources` of `handle`.
    TextureUseDesc(GraphTexture texture, const rojoRHI::TextureSubresourceRange& subresources)
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

/// One colour attachment of a pass. `handle` is the version the pass writes, so the pass produces
/// nextVersion(handle). The named resource must have been imported with a colour-renderable format,
/// and must share its extent with every other attachment the pass declares.
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
    /// derives it). It differs from rojoRHI::RenderPassDesc's 1.0, which is the API's neutral default
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
    std::vector<TextureUseDesc> textureReads;     ///< Sampled or otherwise read textures.
    std::vector<GraphBuffer> bufferReads;         ///< Buffers consumed by the pass.
    std::vector<GraphBuffer> indirectBufferReads; ///< Buffers consumed by indirect draws.
    /// At most one of each. A pass may declare neither, either, or both.
    std::optional<ColorAttachment> color; ///< Optional color target.
    std::optional<DepthAttachment> depth; ///< Optional depth target.
    /// Colour attachments past the primary, in attachment order: `extraColor[0]` is attachment 1,
    /// which a fragment shader writes as SV_Target1. Each is an attachment on `color`'s terms --
    /// it declares its own attachment use and produces the next version of what it names -- and
    /// each shares the primary's extent. A pass may declare at most rojoRHI::kMaxExtraColorTargets of
    /// them, may not declare one without a primary, since extras are attachments 1 and up, and may
    /// not name one texture twice across its colour attachments.
    std::vector<ColorAttachment> extraColor;
    /// Writes that are not attachments. Each names the version it consumes and produces the next.
    std::vector<TextureUseDesc> textureWrites; ///< Non-attachment texture writes.
    std::vector<GraphBuffer> bufferWrites;     ///< Non-attachment buffer writes.
    /// Origin-anchored sub-rectangle the pass rasterises into; 0/0 means the whole attachment.
    /// Both zero or both non-zero, and neither side larger than any of the pass's attachments.
    /// The clear still covers the whole attachment, so texels outside the area hold the clear
    /// value rather than whatever the pass would have drawn there.
    uint32_t renderAreaWidth = 0;
    uint32_t renderAreaHeight = 0; ///< Height of the origin-anchored render area; see the width.
};

/// Everything one compute pass touches, on PassDesc's terms minus the attachments a compute pass
/// has nowhere to put. `textureReads`/`bufferReads` are storage reads; `shader*Reads` are ordinary
/// sampled/SRV/CBV reads; and `indirectBufferReads` are dispatch arguments. The distinction is the
/// RHI use a derived barrier names, so declarations must match the callback's binding command.
/// Ranges are the subresources those bindings address -- a downsample step reading mip N and
/// writing mip N + 1 declares the two as disjoint ranges of one texture.
struct ComputePassDesc {
    std::vector<TextureUseDesc> textureReads;       ///< Storage textures the dispatches read.
    std::vector<TextureUseDesc> shaderTextureReads; ///< Textures sampled by the dispatches.
    std::vector<GraphBuffer> bufferReads;           ///< Storage buffers the dispatches read.
    std::vector<GraphBuffer> shaderBufferReads;     ///< Buffers ordinary shader loads consume.
    std::vector<GraphBuffer> indirectBufferReads;   ///< Buffers consumed by indirect dispatches.
    std::vector<TextureUseDesc> textureWrites;      ///< Textures the dispatches write.
    std::vector<GraphBuffer> bufferWrites;          ///< Buffers the dispatches write.
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

/// Textures an RHI-owned operation reads and writes. Versioning and subresource validation follow
/// ComputePassDesc; barriers use ExternalRead and ExternalWrite. The callback runs between RHI
/// passes and owns the operation's encoding and timing through its RHI command.
struct ExternalPassDesc {
    std::vector<TextureUseDesc> textureReads; ///< Textures the external operation consumes.
    std::vector<TextureUseDesc>
        textureWrites; ///< Textures it overwrites, producing the next version.
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
    GraphResult<rojoRHI::Texture*> texture(GraphTexture handle) const;
    /// The imported buffer behind a declared handle, never null on success.
    GraphResult<rojoRHI::Buffer*> buffer(GraphBuffer handle) const;

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

/// A frame's passes, declared as resources rather than as commands.
///
/// A caller imports the textures and buffers the frame already owns, declares passes over them,
/// exports the results it needs, and compiles. compile() proves the declarations form a directed
/// acyclic graph and answers with the order to run them in, so a mis-declared frame fails on the
/// CPU with a message instead of on the GPU as a hazard.
///
/// The graph borrows what it does not own: an imported rojoRHI::Texture or rojoRHI::Buffer must outlive it,
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

    /// Brings an existing texture into the graph as version 0. `format` snapshots the attachment
    /// interpretation the graph validates and must match texture.format(); `name` appears in
    /// validation messages and is copied. `texture` must outlive the graph.
    GraphTexture importTexture(rojoRHI::Texture& texture, rojoRHI::Format format, std::string_view name);

    /// The same import when a pass in an earlier frame last accessed this persistent texture.
    /// `previousUse` seeds hazard derivation across the command-buffer boundary: a prior write is
    /// ordered before this frame's first read or write, while a prior read is ordered only before
    /// this frame's first write. The whole texture is assumed because no previous-frame range is
    /// available to this fresh graph.
    GraphTexture importTexture(rojoRHI::Texture& texture, rojoRHI::Format format, std::string_view name,
                               rojoRHI::TextureUse previousUse);

    /// Brings an existing buffer into the graph as version 0, on importTexture's terms. Buffers
    /// carry no format because no rule inspects one.
    GraphBuffer importBuffer(rojoRHI::Buffer& buffer, std::string_view name);

    /// The same import when a pass in an earlier frame last accessed this persistent buffer.
    ///
    /// Barriers are derived from the passes declared in this graph, so a producer that ran in
    /// another frame is an edge derivation cannot see. `previousUse` states that terminal access:
    /// a prior write is ordered before this frame's first read or write, while a prior read is
    /// ordered only before this frame's first write.
    ///
    /// This is for persistent feedback and other buffers reused across in-flight frames. A buffer
    /// with no earlier-frame access uses the overload above.
    GraphBuffer importBuffer(rojoRHI::Buffer& buffer, std::string_view name,
                             rojoRHI::BufferUse previousUse);

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
    /// tie-break compile() uses between passes that do not depend on each other -- all
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

    /// Declares an external operation, on addPass's terms. The callback runs without an RHI pass
    /// scope and must encode the operation through an RHI-owned object, which supplies its timing.
    void addExternalPass(std::string_view label, ExternalPassDesc desc, ExecuteFn execute);

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
    /// The synchronisation this emits covers read-after-write, write-after-read, and
    /// write-after-write conflicts, derived from the declarations alone. Texture writers are
    /// tracked per subresource range, so a write to mip N replaces only that range's producer while
    /// untouched mips keep the use that actually wrote them. Persistent-import overloads seed the
    /// last texture or buffer access from an earlier frame, so the first conflicting
    /// access in this frame is barriered like any other. An export emits nothing -- it roots a
    /// result for the caller to read once the queue drains, which is not another pass reading it.
    ///
    /// Reuse of transient memory is the one hazard the version chain cannot state, because the two
    /// sides are different logical resources: where a transient takes bytes an earlier one held, a
    /// whole-resource alias barrier is emitted before its first pass, from every distinct use the
    /// earlier transient makes in its closing pass to this one's first use. It requests the RHI's
    /// ResourceAlias visibility because the two logical resources name the same physical bytes.
    /// It is emitted whatever ranges either side declared -- the hazard is over the memory, not
    /// over the subresources -- and it is what makes an aliased frame match an unaliased one.
    ///
    /// Every reader is covered, on both axes a barrier is scoped on (rojoRHI::CommandList::
    /// textureBarrier states the model): a later reader is left unbarriered only when an
    /// already-emitted barrier for that same write named a range enclosing the whole of what it
    /// reads *and* was consumed by a pass of its own kind. A barrier orders the passes it sits
    /// between, so one that named mip 0 for an earlier reader does not order a later reader of mip
    /// 1, and one a compute pass consumed orders no raster pass however wide its range -- either
    /// reader gets its own. Whole-resource declarations -- what every raster pass here makes --
    /// yield one whole-resource barrier that encloses every later whole-resource read by a pass of
    /// the same kind, so the common frame still transitions once. Writing the resource again clears
    /// what was covered and owes the transition afresh.
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
    CompiledFrameRecord execute(rojoRHI::CommandList& commands, uint64_t frameId);

    /// The resources pass `passIndex` declared, for the pass body to resolve handles through.
    /// Resolution depends on declarations alone, so this is answerable before and independently of
    /// compile(). `passIndex` must name a declared pass.
    PassResources passResources(uint32_t passIndex) const;

private:
    friend class PassResources;

    enum class ResourceKind { Texture, Buffer };

    // One declared resource, imported or transient. The shape fields are carried here rather than
    // read back off the rojoRHI::Texture because a transient has no texture until execute() places it,
    // and the range and attachment rules have to answer the same way for both kinds.
    struct Resource {
        ResourceKind kind = ResourceKind::Texture;
        std::string name;
        // Null for a transient until execute() places it; borrowed for an import.
        rojoRHI::Texture* texture = nullptr;
        rojoRHI::Buffer* buffer = nullptr;
        rojoRHI::Format format = rojoRHI::Format::Unknown;
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
        // Set only by the persistent-import overloads. A fresh per-frame graph cannot otherwise
        // see the last access still in flight in an earlier command buffer.
        std::optional<rojoRHI::TextureUse> priorTextureAccess;
        std::optional<rojoRHI::BufferUse> priorBufferAccess;
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
        rojoRHI::TextureSubresourceRange range;
        bool isWrite = false;
    };

    struct Pass {
        std::string label;
        PassKind kind = PassKind::Raster;
        // Attachments are kept whole because execute() needs their load, store, and clear values;
        // everything else about a pass is in its declarations.
        std::optional<ColorAttachment> color;
        std::vector<ColorAttachment> extraColor;
        std::optional<DepthAttachment> depth;
        uint32_t renderAreaWidth = 0;
        uint32_t renderAreaHeight = 0;
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

    // The rules a pass's extra colour attachments answer to on their own: how many the hardware
    // binds past attachment zero, that attachment zero is there at all, and that each extra is a
    // renderable target of the primary's extent named once across the pass's attachments.
    // Validates pass-local declarations before building cross-pass dependencies.
    GraphResult<void> validateDeclarations() const;
    GraphResult<void> validateExtraColorAttachments(const Pass& pass) const;

    // The rules a pass's render area answers to on its own: a pair that is whole or set on both
    // sides, and one that fits inside every attachment the pass rasterises into.
    GraphResult<void> validateRenderArea(const Pass& pass) const;

    // The colour attachment a ColorAttachment declaration was flattened from. One pass names a
    // resource at most once across its colour attachments -- compilation refuses a frame that does
    // not -- so the resource index picks out exactly one of them.
    const ColorAttachment& colorAttachmentOf(const Pass& pass,
                                             const Declaration& declaration) const;

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
    rojoRHI::TextureDesc textureDescOf(const Resource& resource) const;
    rojoRHI::BufferDesc bufferDescOf(const Resource& resource) const;

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
