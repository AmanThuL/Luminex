//----------------------------------------------------------------------------------------------------------------------
/// @file AttachmentOps.h
/// @brief Declares what a render-pass attachment does with its contents on entry and on exit.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::render {

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

} // namespace lmx::render
