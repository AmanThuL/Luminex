//----------------------------------------------------------------------------------------------------------------------
/// @file LoweredFrame.h
/// @brief Declares a compiled frame lowered to the ordered operations an emitter records.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/AttachmentOps.h"
#include "Render/Graph/CompiledFrameRecord.h"
#include "Render/Graph/RenderGraphLowering.h"
#include "Render/Graph/ResourceUse.h"
#include "Render/Graph/TextureStateTable.h"
#include "Render/Graph/TransientPool.h"

#include <rojoRHI/RHI.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lmx::render {

/// One exact physical texture dependency, including layout transitions and alias discards.
struct LoweredTextureBarrier {
    uint32_t resource = 0;           ///< Index into the emitted resource snapshot.
    rojoRHI::SubresourceRange range; ///< Exact cells sharing one source state.
    TextureState from;               ///< Prior valid-layout frontier.
    TextureState to;                 ///< Next access/layout.
    bool alias = false;              ///< Discards an earlier transient occupant.
};
/// A whole-buffer physical dependency.
struct LoweredBufferBarrier {
    uint32_t resource = 0; ///< Resource index.
    SyncPoint from;        ///< Outstanding earlier accesses.
    SyncPoint to;          ///< Next accesses.
    bool alias = false;    ///< Alias dependencies are global rather than buffer scoped.
};
/// A texture or buffer dependency; alias buffer entries emit a global barrier.
using LoweredBarrier = std::variant<LoweredTextureBarrier, LoweredBufferBarrier>;
/// One nonempty physical boundary batch, independent of diagnostic transitions.
struct BarriersOp {
    std::vector<LoweredBarrier> barriers; ///< All dependencies before the following pass/export.
};

/// One colour attachment of a render pass.
struct LoweredColorAttachment {
    uint32_t resource = 0;                                  ///< Index into LoweredFrame::resources.
    LoadOp load = LoadOp::Clear;                            ///< What happens to prior contents.
    StoreOp store = StoreOp::Store;                         ///< Whether contents outlive the pass.
    std::array<float, 4> clearColor = {0.f, 0.f, 0.f, 1.f}; ///< Applied when `load` clears.
};

/// The depth attachment of a render pass.
struct LoweredDepthAttachment {
    uint32_t resource = 0;            ///< Index into LoweredFrame::resources.
    LoadOp load = LoadOp::Clear;      ///< What happens to prior contents.
    StoreOp store = StoreOp::Discard; ///< Whether contents outlive the pass.
    float clearDepth = 0.0f;          ///< Applied when `load` clears; 0 is the far plane.
};

/// Opens a raster pass's render pass over its attachments.
struct BeginRenderPassOp {
    std::string label;                              ///< The pass's label.
    std::optional<LoweredColorAttachment> color;    ///< Attachment 0, when the pass has one.
    std::vector<LoweredColorAttachment> extraColor; ///< Attachments 1 and up, in order.
    std::optional<LoweredDepthAttachment> depth;    ///< The depth attachment, when present.
    uint32_t renderAreaWidth = 0;  ///< Origin-anchored render area; 0/0 is the whole attachment.
    uint32_t renderAreaHeight = 0; ///< Height of the render area; see the width.
};

/// Closes the render pass the last BeginRenderPassOp opened.
struct EndRenderPassOp {};

/// Opens a named scope: the frame's `lmx.frame`, or a compute or copy pass's own label.
struct BeginScopeOp {
    std::string label; ///< The scope's name.
};

/// Closes the innermost open scope.
struct EndScopeOp {};

/// Runs one scheduled pass's body.
struct RunOp {
    uint32_t pass = 0;                ///< Declaration index of the pass.
    PassKind kind = PassKind::Raster; ///< How the pass is encoded.
    std::string label;                ///< The pass's label.
};

/// One step of a lowered frame.
using LoweredOp =
    std::variant<BarriersOp, BeginRenderPassOp, EndRenderPassOp, BeginScopeOp, EndScopeOp, RunOp>;

/// Borrowed buffer identity and immutable resolver metadata; owns no GPU object.
struct BufferRef {
    rojoRHI::Buffer buffer;               ///< Native identity.
    uint64_t size = 0;                    ///< Buffer size, in bytes.
    std::optional<rojoRHI::Addr> address; ///< Cached address; absent for unaddressable memory.
};
/// One emitted resource snapshot; transients are populated after placement.
struct LoweredResource {
    engine::TextureRef texture{}; ///< Texture identity, sampled view and creation metadata.
    BufferRef buffer{};           ///< Buffer identity, bounds and address.
    std::vector<rojoRHI::ViewIndex> storageViews; ///< Actual views, one per available mip.
};
/// Proposed physical writeback, committed only after emission.
struct TexturePhysicalFinalState {
    uint32_t resource = 0;   ///< Persistent resource index.
    TextureRangeState state; ///< Exact terminal physical cell/range.
};

/// The use a persistent texture is left in once the frame's work completes.
struct TextureFinalState {
    uint32_t resource = 0;                   ///< Index into LoweredFrame::resources.
    TextureUse use = TextureUse::ShaderRead; ///< The use after the frame.
};

/// A compiled frame as an ordered list of operations, built without a device.
///
/// `ops` opens with BeginScopeOp{"lmx.frame"} and closes with its EndScopeOp, an empty schedule
/// included. Between them each scheduled pass contributes, in schedule order, its BarriersOp when
/// it needs transitions, then a raster pass's BeginRenderPassOp, RunOp and EndRenderPassOp, a
/// compute or copy pass's BeginScopeOp, RunOp and EndScopeOp under its own label, or an external
/// pass's RunOp alone. `resources` shares the record's resource index space.
struct LoweredFrame {
    CompiledFrameRecord record;                            ///< What compilation decided.
    std::vector<LoweredOp> ops;                            ///< The frame's operations, in order.
    std::vector<LoweredResource> resources;                ///< Device objects, by resource index.
    std::vector<TextureFinalState> finalStates;            ///< Diagnostic final uses.
    std::vector<TexturePhysicalFinalState> physicalStates; ///< Proposed physical writebacks.
    TransientLayout transientLayout; ///< Exact effective placement/cache descriptors.
};

} // namespace lmx::render
