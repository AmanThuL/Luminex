//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphLowering.h
/// @brief Declares the mapping from graph uses to explicit synchronization.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/CompiledFrameRecord.h"
#include <rojoRHI/RHI.h>

namespace lmx::render {
/// One synchronization endpoint; layout is unused for buffers.
struct SyncPoint {
    rojoRHI::Stage stage;   ///< Pipeline stages making the access.
    rojoRHI::Access access; ///< Accesses made by those stages.
    rojoRHI::Layout layout; ///< Required texture layout.
};
/// Maps a graph texture use to its physical synchronization endpoint.
SyncPoint textureSync(TextureUse use, PassKind kind, bool depthAspect);
/// Maps a graph buffer use; the returned layout is unused.
SyncPoint bufferSync(BufferUse use, PassKind kind);
} // namespace lmx::render
