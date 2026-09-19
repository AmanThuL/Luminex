//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphTransitions.cpp
/// @brief Derives resource transitions and alias handoffs for a compiled schedule.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"
#include "Render/RenderGraphInternal.h"

#include "Core/Assert.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace lmx::render {
using graph_detail::isWriteRole;
using graph_detail::rangesOverlap;
using graph_detail::ResolvedRange;
using graph_detail::resolveRange;

namespace {

//======================================================================================================================
// The RHI use a declaration stands for on either side of a derived barrier. The role decides it
// almost alone; only a plain read has to ask the pass kind, because a compute pass reads through a
// storage binding where a raster pass reads through a sampled one.
rojoRHI::TextureUse textureUseOf(PassKind kind, UseRole role) {
    if (kind == PassKind::External) {
        return isWriteRole(role) ? rojoRHI::TextureUse::ExternalWrite : rojoRHI::TextureUse::ExternalRead;
    }
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? rojoRHI::TextureUse::StorageRead
                                         : rojoRHI::TextureUse::ShaderRead;
    case UseRole::ShaderRead:
        return rojoRHI::TextureUse::ShaderRead;
    case UseRole::IndirectArgument:
        return rojoRHI::TextureUse::ShaderRead;
    case UseRole::Write:
        return rojoRHI::TextureUse::StorageWrite;
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
        return rojoRHI::TextureUse::RenderTarget;
    case UseRole::CopySource:
        return rojoRHI::TextureUse::CopySource;
    case UseRole::CopyDestination:
        return rojoRHI::TextureUse::CopyDestination;
    }
    return rojoRHI::TextureUse::ShaderRead;
}

//======================================================================================================================
rojoRHI::BufferUse bufferUseOf(PassKind kind, UseRole role) {
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? rojoRHI::BufferUse::StorageRead : rojoRHI::BufferUse::ShaderRead;
    case UseRole::ShaderRead:
        return rojoRHI::BufferUse::ShaderRead;
    case UseRole::IndirectArgument:
        return rojoRHI::BufferUse::IndirectArgument;
    case UseRole::Write:
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
        return rojoRHI::BufferUse::StorageWrite;
    case UseRole::CopySource:
        return rojoRHI::BufferUse::CopySource;
    case UseRole::CopyDestination:
        return rojoRHI::BufferUse::CopyDestination;
    }
    return rojoRHI::BufferUse::ShaderRead;
}

//======================================================================================================================
// Whether every subresource of `inner` is one of `outer`'s. Ranges are rectangles, so this is exact
// on a single range and deliberately not extended to a union of several: two barriers whose ranges
// together cover a reader do not each order it, and treating them as if they did is the mistake
// this check exists to avoid.
bool enclosesRange(const ResolvedRange& outer, const ResolvedRange& inner) {
    return outer.firstMip <= inner.firstMip && inner.lastMip <= outer.lastMip &&
           outer.firstLayer <= inner.firstLayer && inner.lastLayer <= outer.lastLayer;
}

//======================================================================================================================
ResolvedRange intersectRange(const ResolvedRange& a, const ResolvedRange& b) {
    LMX_ASSERT(rangesOverlap(a, b), "only overlapping subresource ranges have an intersection");
    return {.firstMip = std::max(a.firstMip, b.firstMip),
            .lastMip = std::min(a.lastMip, b.lastMip),
            .firstLayer = std::max(a.firstLayer, b.firstLayer),
            .lastLayer = std::min(a.lastLayer, b.lastLayer)};
}

//======================================================================================================================
// `range` with `cut`'s overlap removed, as up to four axis-aligned mip x layer rectangles -- a
// write-after-read discharge must shrink a pending read to what the write did *not* touch rather
// than drop the whole entry, or a read of mips 0-3 discharged by a write of mip 0 alone would stop
// protecting mips 1-3 against a later write of just one of them. The four pieces are the two mip
// strips outside `cut`'s mip span (full layer range) plus the two layer strips inside it (only
// `cut`'s mip span, so the two families never overlap each other): a rectangle with a rectangular
// hole cut from it, split the same way any such shape decomposes into disjoint rectangles. Returns
// `range` unchanged when the two do not overlap at all, and nothing when `cut` encloses `range`.
std::vector<ResolvedRange> subtractRange(const ResolvedRange& range, const ResolvedRange& cut) {
    if (!rangesOverlap(range, cut)) {
        return {range};
    }
    const uint32_t cutFirstMip = std::max(range.firstMip, cut.firstMip);
    const uint32_t cutLastMip = std::min(range.lastMip, cut.lastMip);
    const uint32_t cutFirstLayer = std::max(range.firstLayer, cut.firstLayer);
    const uint32_t cutLastLayer = std::min(range.lastLayer, cut.lastLayer);

    std::vector<ResolvedRange> remainder;
    if (range.firstMip < cutFirstMip) {
        remainder.push_back({.firstMip = range.firstMip,
                             .lastMip = cutFirstMip - 1,
                             .firstLayer = range.firstLayer,
                             .lastLayer = range.lastLayer});
    }
    if (cutLastMip < range.lastMip) {
        remainder.push_back({.firstMip = cutLastMip + 1,
                             .lastMip = range.lastMip,
                             .firstLayer = range.firstLayer,
                             .lastLayer = range.lastLayer});
    }
    if (range.firstLayer < cutFirstLayer) {
        remainder.push_back({.firstMip = cutFirstMip,
                             .lastMip = cutLastMip,
                             .firstLayer = range.firstLayer,
                             .lastLayer = cutFirstLayer - 1});
    }
    if (cutLastLayer < range.lastLayer) {
        remainder.push_back({.firstMip = cutFirstMip,
                             .lastMip = cutLastMip,
                             .firstLayer = cutLastLayer + 1,
                             .lastLayer = range.lastLayer});
    }
    return remainder;
}

//======================================================================================================================
// The canonical range covering both, expressed the way a whole-resource declaration is: a union
// that reaches the end of the chain keeps the sentinel rather than a resolved count, so a barrier
// derived from whole-resource reads is indistinguishable from the declaration it came from.
rojoRHI::TextureSubresourceRange unionRange(const ResolvedRange& a, const ResolvedRange& b,
                                        uint32_t mipLevels, uint32_t arrayLayers) {
    const uint32_t firstMip = std::min(a.firstMip, b.firstMip);
    const uint32_t lastMip = std::max(a.lastMip, b.lastMip);
    const uint32_t firstLayer = std::min(a.firstLayer, b.firstLayer);
    const uint32_t lastLayer = std::max(a.lastLayer, b.lastLayer);
    return {.baseMipLevel = firstMip,
            .mipLevelCount = lastMip + 1 >= mipLevels ? rojoRHI::kAllMipLevels : lastMip - firstMip + 1,
            .baseArrayLayer = firstLayer,
            .arrayLayerCount =
                lastLayer + 1 >= arrayLayers ? rojoRHI::kAllArrayLayers : lastLayer - firstLayer + 1};
}

} // namespace

//======================================================================================================================
std::vector<DebugTransition> RenderGraph::deriveTransitions(const Schedule& schedule,
                                                            const AliasPlan& plan) const {
    // What each resource was last written as, and which of its subresources a barrier has since
    // made visible to a reader. Writing a resource again puts it back in a producing state and
    // clears what was covered, so the transition is owed again.
    //
    // Coverage is per emitted range *and* per consuming stage class, because those are the two axes
    // a barrier is scoped on (rojoRHI::CommandList::textureBarrier states the model). A barrier orders
    // the passes it sits between, so a reader of mip 1 is not ordered by a barrier that named mip 0
    // for an earlier reader; and a barrier consumed by a compute pass is scoped to that pass's
    // stages, so it orders nothing for a later raster reader of the same subresources. A pass's
    // kind is its stage class here, including opaque external operations. Passes of one kind are
    // ordered among themselves, so one barrier serves every later reader of that kind. Two kinds
    // whose stages happen to overlap
    // in a backend are still treated as distinct, which costs a redundant barrier rather than a
    // missed one. Whole-resource declarations -- what every raster pass here makes -- produce one
    // whole-resource range that encloses every later whole-resource reader of the same kind, so one
    // transition still serves them all.
    struct Covered {
        ResolvedRange range;
        PassKind consumer = PassKind::Raster;
    };
    struct TextureWriter {
        ResolvedRange range;
        rojoRHI::TextureUse use = rojoRHI::TextureUse::RenderTarget;
        bool writes = true;
        std::vector<Covered> covered;
    };
    struct WriteState {
        bool bufferWritten = false;
        rojoRHI::BufferUse bufferUse = rojoRHI::BufferUse::StorageWrite;
        std::vector<Covered> bufferCovered;
        std::vector<TextureWriter> textureWriters;
    };
    std::vector<WriteState> pending(m_resources.size());

    const auto textureUseWrites = [](rojoRHI::TextureUse use) {
        return use == rojoRHI::TextureUse::RenderTarget || use == rojoRHI::TextureUse::StorageWrite ||
               use == rojoRHI::TextureUse::CopyDestination || use == rojoRHI::TextureUse::ExternalWrite;
    };
    const auto bufferUseWrites = [](rojoRHI::BufferUse use) {
        return use == rojoRHI::BufferUse::StorageWrite || use == rojoRHI::BufferUse::CopyDestination;
    };

    // A texture accessed in an earlier frame starts with that whole-resource state. Prior writes
    // participate in RAW and WAW; prior reads skip RAW but remain available to the WAW loop below,
    // which emits the cross-frame WAR before this frame overwrites them.
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        if (const std::optional<rojoRHI::TextureUse>& access = m_resources[index].priorTextureAccess) {
            const Resource& resource = m_resources[index];
            pending[index].textureWriters.push_back(
                {.range = {.firstMip = 0,
                           .lastMip = resource.mipLevels - 1,
                           .firstLayer = 0,
                           .lastLayer = resource.arrayLayers - 1},
                 .use = *access,
                 .writes = textureUseWrites(*access)});
        }
    }

    // Subresources read since the last write that touched them, not yet ordered against a future
    // write. A write-after-read hazard is the mirror of the read-after-write one above: a pass that
    // writes a version some earlier pass already read needs a barrier ordering it after that read,
    // or the write could retire before the read that depends on the prior contents does. A write
    // discharges only the ranges it actually overlaps -- a write of mip 3 says nothing about a read
    // of mip 0, so mip 0's read stays owed to whichever later write does overlap it. Tracking is
    // still per-resource rather than per-version, because whole-resource writes (every raster pass
    // here) discharge everything in one step and are the common case; ranged writes discharge in
    // parts instead of all at once, which is the property a full per-resource reset would lose.
    //
    // The use that made each read is carried per entry rather than once per resource, because the
    // producing side of a write-after-read barrier has to cover *every* reader the write is being
    // ordered after. Keeping only the last reader's use would let one reader's stage class stand in
    // for another's -- a scene pass's sampled read and a histogram dispatch's storage read of one
    // buffer, with the resolve dispatch that overwrites it waiting on the dispatch alone. A write
    // therefore emits one barrier per *distinct* use among the readers it overlaps, in first-read
    // order: separate barriers between the same two passes are how the RHI expresses a producing
    // side that spans several uses, and a backend accumulates them into the one dependency the
    // consuming pass emits.
    struct PendingRead {
        ResolvedRange range;
        rojoRHI::TextureUse use = rojoRHI::TextureUse::ShaderRead;
    };
    struct ReadState {
        std::vector<PendingRead> textureReads;
        // Buffers carry no subresource ranges, so a pending buffer read is always the whole
        // resource and only the distinct uses are worth keeping, in first-read order.
        std::vector<rojoRHI::BufferUse> bufferReads;
    };
    std::vector<ReadState> pendingReads(m_resources.size());
    std::vector<DebugTransition> transitions;

    // Buffers split their earlier-frame terminal access between the same two states used for
    // accesses declared in this frame. This preserves the real use in debug records and lets a
    // prior read followed by another read remain barrier-free.
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const std::optional<rojoRHI::BufferUse>& access = m_resources[index].priorBufferAccess;
        if (!access) {
            continue;
        }
        if (bufferUseWrites(*access)) {
            pending[index].bufferWritten = true;
            pending[index].bufferUse = *access;
        } else {
            pendingReads[index].bufferReads.push_back(*access);
        }
    }

    // Every distinct use a resource makes at one lifetime boundary. The closing pass may read and
    // write disjoint subresources through different stages, and alias reuse has to wait on all of
    // them before the next logical resource takes those bytes.
    const auto usesAt = [&](uint32_t resource, uint32_t position) {
        const Pass& pass = m_passes[schedule.passes[position]];
        std::vector<UseRole> roles;
        for (const Declaration& declaration : pass.declarations) {
            if (declaration.resource != resource ||
                std::ranges::find(roles, declaration.role) != roles.end()) {
                continue;
            }
            roles.push_back(declaration.role);
        }
        LMX_ASSERT(!roles.empty(),
                   "a transient's lifetime bound must name a pass that declares it");
        return std::pair{pass.kind, std::move(roles)};
    };

    for (uint32_t position = 0; position < schedule.passes.size(); ++position) {
        const uint32_t passIndex = schedule.passes[position];
        const Pass& pass = m_passes[passIndex];
        const auto loadsAttachment = [&](const Declaration& declaration) {
            return (declaration.role == UseRole::ColorAttachment &&
                    colorAttachmentOf(pass, declaration).load == LoadOp::Load) ||
                   (declaration.role == UseRole::DepthAttachment &&
                    pass.depth->load == LoadOp::Load);
        };

        // Reuse boundaries come first: they make the memory this pass's transients sit in available
        // before anything else about the pass is ordered. Whole-resource whatever either side
        // declared, because the hazard is over shared bytes rather than over subresources, and
        // listed in declaration order so the sequence is a function of the declarations.
        for (const TransientPlan& entry : plan.transients) {
            if (!entry.aliasedFrom || entry.firstPosition != position) {
                continue;
            }
            const TransientPlan& previous =
                *std::ranges::find(plan.transients, *entry.aliasedFrom, &TransientPlan::resource);
            const auto [fromKind, fromRoles] = usesAt(previous.resource, previous.lastPosition);
            const auto [toKind, toRoles] = usesAt(entry.resource, entry.firstPosition);
            const UseRole toRole = toRoles.front();
            if (m_resources[entry.resource].kind == ResourceKind::Buffer) {
                std::vector<rojoRHI::BufferUse> emitted;
                for (const UseRole fromRole : fromRoles) {
                    const rojoRHI::BufferUse from = bufferUseOf(fromKind, fromRole);
                    if (std::ranges::find(emitted, from) != emitted.end()) {
                        continue;
                    }
                    emitted.push_back(from);
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = entry.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = from,
                                           .bufferTo = bufferUseOf(toKind, toRole),
                                           .aliasedFrom = previous.resource});
                }
            } else {
                std::vector<rojoRHI::TextureUse> emitted;
                for (const UseRole fromRole : fromRoles) {
                    const rojoRHI::TextureUse from = textureUseOf(fromKind, fromRole);
                    if (std::ranges::find(emitted, from) != emitted.end()) {
                        continue;
                    }
                    emitted.push_back(from);
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = entry.resource,
                                           .kind = GraphResourceKind::Texture,
                                           .textureFrom = from,
                                           .textureTo = textureUseOf(toKind, toRole),
                                           .aliasedFrom = previous.resource});
                }
            }
        }

        // A read is ordered against every writer segment it overlaps. Several reads made through
        // the same use in one pass collapse into one range before that comparison. A loaded
        // attachment participates here too: it is a read and a write through RenderTarget.
        for (uint32_t index = 0; index < pass.declarations.size(); ++index) {
            const Declaration& read = pass.declarations[index];
            const bool reads = !read.isWrite || loadsAttachment(read);
            if (!reads) {
                continue;
            }
            const Resource& resource = m_resources[read.resource];
            bool declaredEarlier = false;
            for (uint32_t earlier = 0; earlier < index; ++earlier) {
                const Declaration& other = pass.declarations[earlier];
                const bool otherReads = !other.isWrite || loadsAttachment(other);
                declaredEarlier =
                    declaredEarlier || (otherReads && other.resource == read.resource &&
                                        (resource.kind == ResourceKind::Texture
                                             ? textureUseOf(pass.kind, other.role) ==
                                                   textureUseOf(pass.kind, read.role)
                                             : bufferUseOf(pass.kind, other.role) ==
                                                   bufferUseOf(pass.kind, read.role)));
            }
            if (declaredEarlier) {
                continue;
            }

            if (resource.kind == ResourceKind::Buffer) {
                WriteState& state = pending[read.resource];
                if (!state.bufferWritten) {
                    continue;
                }
                bool alreadyOrdered = false;
                for (const Covered& emitted : state.bufferCovered) {
                    alreadyOrdered = alreadyOrdered || emitted.consumer == pass.kind;
                }
                if (alreadyOrdered) {
                    continue;
                }
                state.bufferCovered.push_back({.consumer = pass.kind});
                transitions.push_back({.beforePass = passIndex,
                                       .resource = read.resource,
                                       .kind = GraphResourceKind::Buffer,
                                       .bufferFrom = state.bufferUse,
                                       .bufferTo = bufferUseOf(pass.kind, read.role)});
            } else {
                rojoRHI::TextureSubresourceRange covered = read.range;
                for (uint32_t later = index + 1; later < pass.declarations.size(); ++later) {
                    const Declaration& other = pass.declarations[later];
                    const bool otherReads = !other.isWrite || loadsAttachment(other);
                    if (!otherReads || other.resource != read.resource ||
                        textureUseOf(pass.kind, other.role) != textureUseOf(pass.kind, read.role)) {
                        continue;
                    }
                    covered = unionRange(
                        resolveRange(covered, resource.mipLevels, resource.arrayLayers),
                        resolveRange(other.range, resource.mipLevels, resource.arrayLayers),
                        resource.mipLevels, resource.arrayLayers);
                }
                const ResolvedRange resolved =
                    resolveRange(covered, resource.mipLevels, resource.arrayLayers);
                struct NeededBarrier {
                    ResolvedRange range;
                    rojoRHI::TextureUse from = rojoRHI::TextureUse::RenderTarget;
                };
                std::vector<NeededBarrier> needed;
                for (TextureWriter& writer : pending[read.resource].textureWriters) {
                    if (!writer.writes || !rangesOverlap(writer.range, resolved)) {
                        continue;
                    }
                    const ResolvedRange overlap = intersectRange(writer.range, resolved);
                    bool alreadyOrdered = false;
                    for (const Covered& emitted : writer.covered) {
                        alreadyOrdered = alreadyOrdered || (emitted.consumer == pass.kind &&
                                                            enclosesRange(emitted.range, overlap));
                    }
                    if (alreadyOrdered) {
                        continue;
                    }
                    writer.covered.push_back({.range = overlap, .consumer = pass.kind});
                    const auto sameUse =
                        std::ranges::find(needed, writer.use, &NeededBarrier::from);
                    if (sameUse == needed.end()) {
                        needed.push_back({.range = overlap, .from = writer.use});
                    } else {
                        sameUse->range =
                            resolveRange(unionRange(sameUse->range, overlap, resource.mipLevels,
                                                    resource.arrayLayers),
                                         resource.mipLevels, resource.arrayLayers);
                    }
                }
                for (const NeededBarrier& barrier : needed) {
                    transitions.push_back(
                        {.beforePass = passIndex,
                         .resource = read.resource,
                         .kind = GraphResourceKind::Texture,
                         .range = unionRange(barrier.range, barrier.range, resource.mipLevels,
                                             resource.arrayLayers),
                         .textureFrom = barrier.from,
                         .textureTo = textureUseOf(pass.kind, read.role)});
                }
            }
        }

        // Every overlapping write is an access conflict on Metal 4's untracked resources. A loaded
        // attachment was already ordered as a read above, so that one dependency also orders its
        // write and need not be duplicated here.
        for (const Declaration& write : pass.declarations) {
            if (!write.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[write.resource];
            WriteState& state = pending[write.resource];
            if (resource.kind == ResourceKind::Buffer) {
                if (state.bufferWritten) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = write.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = state.bufferUse,
                                           .bufferTo = bufferUseOf(pass.kind, write.role)});
                }
                continue;
            }
            const ResolvedRange writeRange =
                resolveRange(write.range, resource.mipLevels, resource.arrayLayers);
            for (const TextureWriter& writer : state.textureWriters) {
                // Loading an attachment already took the RAW path above for a prior write. A
                // prior read is different: the attachment's write still owes it a WAR barrier.
                if ((loadsAttachment(write) && writer.writes) ||
                    !rangesOverlap(writer.range, writeRange)) {
                    continue;
                }
                const ResolvedRange overlap = intersectRange(writer.range, writeRange);
                transitions.push_back({.beforePass = passIndex,
                                       .resource = write.resource,
                                       .kind = GraphResourceKind::Texture,
                                       .range = unionRange(overlap, overlap, resource.mipLevels,
                                                           resource.arrayLayers),
                                       .textureFrom = writer.use,
                                       .textureTo = textureUseOf(pass.kind, write.role)});
            }
        }

        // A write-after-read barrier per write declaration that overlaps a pending read -- not once
        // per prior reader, since those readers needed no ordering among themselves and the write
        // is what has to wait for the last of them. This checks reads recorded by *earlier* passes
        // only: this pass's own reads (if any) are not recorded into `pendingReads` until the loop
        // below runs, which is what lets a pass read and write one resource through disjoint ranges
        // (bloom's downsample step, one buffer accumulate dispatch) without owing a barrier against
        // itself. Discharge is at *subresource* granularity, not whole-entry: a write shrinks each
        // overlapping pending-read entry to the subresources it did not touch (`subtractRange`)
        // rather than dropping the entry outright, and two hazards motivate that precision. First,
        // one write must not discharge a disjoint pending read sharing only the resource, not any
        // subresources: A reads mip 0, B writes mip 3 -- disjoint, no barrier, but clearing mip 0's
        // whole entry anyway would leave C's later write of mip 0 wrongly finding nothing owed.
        // Second, a write covering *part* of one read entry must not discharge the rest of that
        // same entry: A reads mips 0-3 in one declaration, D writes mip 0 alone (the two overlap,
        // so a barrier is owed before D) -- but erasing the whole 0-3 entry on that overlap would
        // leave a later write of mip 2 by E wrongly finding nothing owed either, though A's read of
        // mip 2 was never ordered against it.
        for (const Declaration& declaration : pass.declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            ReadState& reads = pendingReads[declaration.resource];
            const Resource& resource = m_resources[declaration.resource];
            if (resource.kind == ResourceKind::Buffer) {
                // Buffers carry no subresource ranges, so any pending read is the whole resource
                // and every write discharges it completely -- there is no partial case to preserve.
                // One barrier per distinct reading use, so the producing side covers every reader.
                for (const rojoRHI::BufferUse readUse : reads.bufferReads) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = declaration.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = readUse,
                                           .bufferTo = bufferUseOf(pass.kind, declaration.role)});
                }
                reads.bufferReads.clear();
            } else if (!reads.textureReads.empty()) {
                const ResolvedRange writeRange =
                    resolveRange(declaration.range, resource.mipLevels, resource.arrayLayers);
                // Shrink each pending entry to what this write did not touch, rather than dropping
                // an entry outright the moment any part of it overlaps -- see the comment above.
                // Each surviving piece keeps the use that read it, so a later write over it names
                // that reader too.
                std::vector<PendingRead> remaining;
                remaining.reserve(reads.textureReads.size());
                std::vector<rojoRHI::TextureUse> overlappedUses;
                for (const PendingRead& pendingRead : reads.textureReads) {
                    if (!rangesOverlap(pendingRead.range, writeRange)) {
                        remaining.push_back(pendingRead);
                        continue;
                    }
                    if (std::ranges::find(overlappedUses, pendingRead.use) ==
                        overlappedUses.end()) {
                        overlappedUses.push_back(pendingRead.use);
                    }
                    for (const ResolvedRange& piece :
                         subtractRange(pendingRead.range, writeRange)) {
                        remaining.push_back({.range = piece, .use = pendingRead.use});
                    }
                }
                for (const rojoRHI::TextureUse readUse : overlappedUses) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = declaration.resource,
                                           .kind = GraphResourceKind::Texture,
                                           .range = declaration.range,
                                           .textureFrom = readUse,
                                           .textureTo = textureUseOf(pass.kind, declaration.role)});
                }
                reads.textureReads = std::move(remaining);
            }
        }

        // Record every read this pass makes, whatever wrote the version it names, so a later write
        // to the same resource knows what it must be ordered after. Appended rather than
        // deduplicated against what RAW already covered above: RAW orders a reader against its
        // producer, this orders a future writer against the reader, and the two barriers answer
        // different questions even when they happen to share a `from` use. Deliberately after the
        // write-after-read check above, not before: this pass's own reads must not count as a prior
        // reader of themselves.
        for (const Declaration& read : pass.declarations) {
            if (read.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[read.resource];
            ReadState& reads = pendingReads[read.resource];
            if (resource.kind == ResourceKind::Buffer) {
                const rojoRHI::BufferUse use = bufferUseOf(pass.kind, read.role);
                if (std::ranges::find(reads.bufferReads, use) == reads.bufferReads.end()) {
                    reads.bufferReads.push_back(use);
                }
            } else {
                reads.textureReads.push_back(
                    {.range = resolveRange(read.range, resource.mipLevels, resource.arrayLayers),
                     .use = textureUseOf(pass.kind, read.role)});
            }
        }

        for (const Declaration& declaration : pass.declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            WriteState& state = pending[declaration.resource];
            if (resource.kind == ResourceKind::Buffer) {
                state.bufferWritten = true;
                state.bufferUse = bufferUseOf(pass.kind, declaration.role);
                state.bufferCovered.clear();
                continue;
            }

            const ResolvedRange writeRange =
                resolveRange(declaration.range, resource.mipLevels, resource.arrayLayers);
            std::vector<TextureWriter> inherited;
            for (const TextureWriter& writer : state.textureWriters) {
                for (const ResolvedRange& piece : subtractRange(writer.range, writeRange)) {
                    inherited.push_back({.range = piece,
                                         .use = writer.use,
                                         .writes = writer.writes,
                                         .covered = writer.covered});
                }
            }
            inherited.push_back({.range = writeRange,
                                 .use = textureUseOf(pass.kind, declaration.role),
                                 .writes = true,
                                 .covered = {}});
            state.textureWriters = std::move(inherited);
        }
    }
    return transitions;
}

} // namespace lmx::render
