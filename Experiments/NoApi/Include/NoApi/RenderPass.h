//----------------------------------------------------------------------------------------------------------------------
/// @file RenderPass.h
/// @brief Declares the attachment set a render pass declares at its boundary.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::experimental::noapi {

/// Names what happens to an attachment's existing contents when a pass begins.
enum class LoadAction : uint8_t {
    DontCare, ///< Contents are undefined on entry; the cheapest option on tiled hardware.
    Load,     ///< Existing contents are preserved.
    Clear,    ///< Contents are replaced with the attachment's clear value.
};

/// Names what happens to an attachment's contents when a pass ends.
enum class StoreAction : uint8_t {
    DontCare, ///< Contents may be discarded.
    Store,    ///< Contents are written back and readable by later passes.
};

/// Describes one color attachment of a render pass.
struct ColorAttachment {
    /// Attachment texture; must declare `TextureUsage::ColorAttachment`.
    const Texture* texture = nullptr;
    uint32_t mipLevel = 0;                  ///< Mip level rendered into.
    uint32_t arrayLayer = 0;                ///< Array layer or cube face rendered into.
    LoadAction load = LoadAction::DontCare; ///< Entry action.
    StoreAction store = StoreAction::Store; ///< Exit action.
    /// Clear value in the attachment's own color space.
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Describes the depth attachment of a render pass.
struct DepthAttachment {
    /// Attachment texture; must declare `TextureUsage::DepthStencilAttachment`.
    const Texture* texture = nullptr;
    uint32_t mipLevel = 0;                  ///< Mip level rendered into.
    uint32_t arrayLayer = 0;                ///< Array layer rendered into.
    LoadAction load = LoadAction::DontCare; ///< Entry action.
    StoreAction store = StoreAction::Store; ///< Exit action.
    float clearDepth = 1.0f; ///< Clear value in the depth convention the caller renders with.
};

/// Describes the complete attachment set of one render pass.
///
/// Attachments are declared here and nowhere else: there is no attachment binding command and no
/// persistent render pass object. Beginning a pass emits no barrier, so two passes writing disjoint
/// attachments may overlap and a depth prepass does not pay for a flush it does not need.
struct RenderPassDesc {
    /// Color attachments, in attachment order; may be empty.
    std::span<const ColorAttachment> colorTargets;
    const DepthAttachment* depth = nullptr; ///< Depth attachment, or null for a color-only pass.
    std::string_view label; ///< Debug label; must be non-empty, and names the pass in captures.
};

} // namespace lmx::experimental::noapi
