//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphInternal.h
/// @brief Shares private graph range and declaration helpers across compilation units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Containers/Interval.h"
#include "Render/Graph/RenderGraph.h"

namespace lmx::render::graph_detail {

// Inclusive, resolved mip/layer bounds shared by validation and transition derivation.
struct ResolvedRange {
    Interval mips{0, 1};
    Interval layers{0, 1};
};

struct TransitionState {
    // What each resource was last written as, and which of its subresources a barrier has since
    // made visible to a reader. Writing a resource again puts it back in a producing state and
    // clears what was covered, so the transition is owed again.
    //
    // Coverage is per emitted range *and* per consuming stage class, because those are the two axes
    // a barrier is scoped on (rojoRHI::CommandList::textureBarrier states the model). A barrier
    // orders the passes it sits between, so a reader of mip 1 is not ordered by a barrier that
    // named mip 0 for an earlier reader; and a barrier consumed by a compute pass is scoped to that
    // pass's stages, so it orders nothing for a later raster reader of the same subresources. A
    // pass's kind is its stage class here, including opaque external operations. Passes of one kind
    // are ordered among themselves, so one barrier serves every later reader of that kind. Two
    // kinds whose stages happen to overlap in a backend are still treated as distinct, which costs
    // a redundant barrier rather than a missed one. Whole-resource declarations -- what every
    // raster pass here makes -- produce one whole-resource range that encloses every later
    // whole-resource reader of the same kind, so one transition still serves them all.
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
    std::vector<WriteState> pending;
    std::vector<ReadState> pendingReads;
    std::vector<DebugTransition> transitions;
};

bool isWriteRole(UseRole role);
ResolvedRange resolveRange(const rojoRHI::TextureSubresourceRange& range, uint32_t mipLevels,
                           uint32_t arrayLayers);
bool rangesOverlap(const ResolvedRange& a, const ResolvedRange& b);
std::string_view sinkVerb(SinkKind kind);
std::unexpected<GraphError> fail(std::string message);

} // namespace lmx::render::graph_detail
