//----------------------------------------------------------------------------------------------------------------------
/// @file LoweredFrame.h
/// @brief Declares a compiled frame lowered to the ordered operations an emitter records.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/AttachmentOps.h"
#include "Render/Graph/CompiledFrameRecord.h"
#include "Render/Graph/ResourceUse.h"

#include <rojoRHI/Buffer.h>
#include <rojoRHI/Texture.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lmx::render {

/// One texture transition at a transition point.
struct LoweredTextureBarrier {
    uint32_t resource = 0;                      ///< Index into LoweredFrame::resources.
    rojoRHI::TextureSubresourceRange range;     ///< Subresources the transition covers.
    TextureUse from = TextureUse::RenderTarget; ///< Use that last accessed the subresources.
    TextureUse to = TextureUse::ShaderRead;     ///< Use that accesses them next.
    /// Set when the resource takes over bytes an earlier transient held, so the transition orders
    /// that transient's last use against this resource's first.
    bool alias = false;
};

/// One buffer transition at a transition point; it always covers the whole buffer.
struct LoweredBufferBarrier {
    uint32_t resource = 0;                    ///< Index into LoweredFrame::resources.
    BufferUse from = BufferUse::StorageWrite; ///< Use that last accessed the buffer.
    BufferUse to = BufferUse::StorageRead;    ///< Use that accesses it next.
    bool alias = false; ///< Set on a transient reuse boundary, as for a texture.
};

/// A texture or a buffer transition, kept in one list so a transition point keeps record order.
using LoweredBarrier = std::variant<LoweredTextureBarrier, LoweredBufferBarrier>;

/// Every transition one scheduled pass needs before it starts, in record order.
struct BarriersOp {
    std::vector<LoweredBarrier> barriers; ///< Never empty: a pass with none gets no op.
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

/// The device objects behind one declared resource. Lowering fills imports; a transient stays null
/// until its frame is placed.
struct LoweredResource {
    rojoRHI::Texture* texture = nullptr; ///< The texture, for a texture resource.
    rojoRHI::Buffer* buffer = nullptr;   ///< The buffer, for a buffer resource.
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
    CompiledFrameRecord record;                 ///< What compilation decided.
    std::vector<LoweredOp> ops;                 ///< The frame's operations, in order.
    std::vector<LoweredResource> resources;     ///< Device objects, by resource index.
    std::vector<TextureFinalState> finalStates; ///< Uses persistent textures are left in.
};

} // namespace lmx::render
