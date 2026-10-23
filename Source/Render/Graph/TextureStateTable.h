//----------------------------------------------------------------------------------------------------------------------
/// @file TextureStateTable.h
/// @brief Declares the table of uses persistent textures were left in by earlier frames.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/ResourceUse.h"

#include <rojoRHI/Texture.h>

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace lmx::render {

/// The use each persistent texture was left in when the last frame that touched it completed.
///
/// A render graph is declared fresh every frame and cannot see the passes of an earlier one, so a
/// texture that outlives a frame carries its terminal use here. A graph bound to the table reads
/// the entry when it imports the texture through RenderGraph::importPersistentTexture and writes
/// the frame's terminal use back once it has recorded the frame. An absent entry means no earlier
/// access, which is what a plain import states.
///
/// The owner of the persistent textures, the renderer, owns the table and binds it to each frame's
/// graph. Entries are keyed by the texture's address and are whole-resource uses.
/// An address can be reused once a texture is destroyed, so an owner that creates or recreates a
/// persistent texture seeds it with set() before its next import; otherwise the new texture would
/// inherit the destroyed one's use. The table holds no GPU object and dereferences no key, and it
/// is not thread-safe: it is read and written on the thread that records the frame.
class TextureStateTable {
public:
    /// The use `texture` was left in, or nullopt when nothing earlier accessed it.
    std::optional<TextureUse> find(const rojoRHI::Texture& texture) const;

    /// States the use `texture` is in now, replacing any earlier entry.
    void set(const rojoRHI::Texture& texture, TextureUse use);

    /// Removes `texture`'s entry, so its next import has no earlier access.
    void forget(const rojoRHI::Texture& texture);

    /// The number of textures with an entry.
    std::size_t size() const { return m_uses.size(); }

private:
    std::unordered_map<const rojoRHI::Texture*, TextureUse> m_uses;
};

} // namespace lmx::render
