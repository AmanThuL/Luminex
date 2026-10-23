//----------------------------------------------------------------------------------------------------------------------
/// @file TextureStateTable.cpp
/// @brief Implements the table of uses persistent textures were left in by earlier frames.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/TextureStateTable.h"

namespace lmx::render {

//======================================================================================================================
std::optional<TextureUse> TextureStateTable::find(const rojoRHI::Texture& texture) const {
    const auto entry = m_uses.find(&texture);
    if (entry == m_uses.end()) {
        return std::nullopt;
    }
    return entry->second;
}

//======================================================================================================================
void TextureStateTable::set(const rojoRHI::Texture& texture, TextureUse use) {
    m_uses.insert_or_assign(&texture, use);
}

//======================================================================================================================
void TextureStateTable::forget(const rojoRHI::Texture& texture) {
    m_uses.erase(&texture);
}

} // namespace lmx::render
