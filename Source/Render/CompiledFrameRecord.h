//----------------------------------------------------------------------------------------------------------------------
/// @file CompiledFrameRecord.h
/// @brief Declares compiled-frame records and shared diagnostic value vocabulary.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/Buffer.h>
#include <rojoRHI/Texture.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::render {

/// What kind of work a declared pass encodes, and therefore which RHI pass scope execute() opens
/// for it, if any. Each declaration path fixes the kind, so
/// a pass cannot carry a kind its declaration does not fit: attachments exist only on the raster
/// path, and copy source/destination uses only on the copy one.
enum class PassKind {
    Raster,  ///< Encoded as a render pass over its attachments.
    Compute, ///< Encoded as a compute pass.
    Copy,    ///< Encoded as a copy pass.
    External ///< Encoded by an RHI-owned object, outside any graph-opened RHI pass scope.
};

/// What a pass does with one declared resource version. It is what the graph's validation messages
/// name, what decides read from write, and what the derived barriers are built from -- a compute
/// read and a raster read order differently on the way in, and the role plus the pass kind is what
/// says which.
enum class UseRole {
    Read,             ///< Consumed without being written.
    ShaderRead,       ///< Consumed through an ordinary sampled/SRV/CBV shader binding.
    IndirectArgument, ///< Buffer consumed as draw or dispatch arguments.
    Write,            ///< Written other than as an attachment.
    ColorAttachment,  ///< Written as the pass's colour attachment.
    DepthAttachment,  ///< Written as the pass's depth attachment.
    CopySource,       ///< Read by a copy command.
    CopyDestination   ///< Written by a copy command.
};

/// Names a role in validation messages and frame dumps, in the wording the graph's diagnostics use.
std::string_view roleName(UseRole role);

/// Names a format as its enumerator, so a message says D32Float rather than an integer. It lives
/// here rather than in the RHI because naming an enumerator is a diagnostic concern and the graph
/// is what has the diagnostics.
std::string_view formatName(rojoRHI::Format format);

/// Formats a subresource range as `mips[first..last] layers[first..last]`, with the last bound left
/// open -- `mips[1..]` -- where the range runs to the end of the chain. Shared by the graph's
/// validation messages and the frame dump so one range reads the same in both.
std::string describeRange(const rojoRHI::TextureSubresourceRange& range);

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
    rojoRHI::Format format = rojoRHI::Format::Unknown;           ///< Declared format; Unknown for a buffer.
};

/// One declared use of one resource version by one pass, in the order the pass declared it.
struct DebugUse {
    uint32_t resource = 0;              ///< Index into CompiledFrameDebug::resources.
    uint32_t version = 0;               ///< The version the pass named.
    UseRole role = UseRole::Read;       ///< What the pass does with it.
    rojoRHI::TextureSubresourceRange range; ///< Subresources covered; whole-resource for a buffer.
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
    /// The render area the pass declared, 0/0 when it renders the whole attachment.
    uint32_t renderAreaWidth = 0;
    uint32_t renderAreaHeight = 0; ///< Height of the declared render area; see the width.
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
    rojoRHI::TextureSubresourceRange range;                  ///< Subresources covered; textures only.
    rojoRHI::TextureUse textureFrom = rojoRHI::TextureUse::RenderTarget; ///< Producing texture use.
    rojoRHI::TextureUse textureTo = rojoRHI::TextureUse::ShaderRead;     ///< Consuming texture use.
    rojoRHI::BufferUse bufferFrom = rojoRHI::BufferUse::StorageWrite;    ///< Producing buffer use.
    rojoRHI::BufferUse bufferTo = rojoRHI::BufferUse::StorageRead;       ///< Consuming buffer use.
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
/// `frameId` is the RHI device's number for the frame being recorded (rojoRHI::Device::frameNumber()),
/// which is the same numbering rojoRHI::Device::passTimingsFrame() reports -- so an observer holding
/// records for the frames in flight joins a retired frame's timings to the record that describes it
/// by comparing the two numbers rather than by guessing at a lag.
struct CompiledFrameRecord {
    uint64_t frameId = 0;     ///< The device frame number this frame was compiled for.
    CompiledFrameDebug debug; ///< What compilation decided.
};

} // namespace lmx::render
