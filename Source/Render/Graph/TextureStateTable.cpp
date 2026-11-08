//----------------------------------------------------------------------------------------------------------------------
/// @file TextureStateTable.cpp
/// @brief Implements exact physical cells independently of logical texture uses.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/TextureStateTable.h"
#include "Core/Diagnostics/Assert.h"

namespace lmx::render {
namespace {

//======================================================================================================================
rojoRHI::SubresourceRange resolved(rojoRHI::SubresourceRange range, uint32_t mips,
                                   uint32_t layers) {
    LMX_ASSERT(range.baseMip < mips && range.baseLayer < layers,
               "texture state range starts outside its resource");
    if (range.mipCount == rojoRHI::kAllMips) {
        range.mipCount = mips - range.baseMip;
    }
    if (range.layerCount == rojoRHI::kAllLayers) {
        range.layerCount = layers - range.baseLayer;
    }
    LMX_ASSERT(range.mipCount > 0 && range.mipCount <= mips - range.baseMip &&
                   range.layerCount > 0 && range.layerCount <= layers - range.baseLayer,
               "texture state range runs outside its resource");
    return range;
}
} // namespace

//======================================================================================================================
std::optional<TextureUse> TextureStateTable::logicalUse(rojoRHI::Texture texture) const {
    const auto found = m_entries.find(texture.bits);
    return found == m_entries.end() ? std::nullopt : found->second.logical;
}

//======================================================================================================================
void TextureStateTable::setLogicalUse(rojoRHI::Texture texture, TextureUse use) {
    m_entries[texture.bits].logical = use;
}

//======================================================================================================================
void TextureStateTable::seedPhysical(rojoRHI::Texture texture, uint32_t mips, uint32_t layers,
                                     rojoRHI::Layout initialLayout) {
    LMX_ASSERT(mips > 0 && layers > 0, "texture state dimensions must be non-zero");
    Entry& entry = m_entries[texture.bits];
    if (entry.cells.empty()) {
        entry.mips = mips;
        entry.layers = layers;
        entry.cells.resize(static_cast<size_t>(mips) * layers);
    }
    LMX_ASSERT(entry.mips == mips && entry.layers == layers,
               "one texture identity cannot change its subresource dimensions");
    for (auto& cell : entry.cells) {
        if (!cell) {
            cell = TextureState{.layout = initialLayout};
        }
    }
}

//======================================================================================================================
std::vector<TextureRangeState>
TextureStateTable::physicalRanges(rojoRHI::Texture texture,
                                  const rojoRHI::SubresourceRange& requested) const {
    const auto found = m_entries.find(texture.bits);
    if (found == m_entries.end() || found->second.cells.empty()) {
        return {};
    }
    const Entry& entry = found->second;
    const auto range = resolved(requested, entry.mips, entry.layers);
    std::vector<TextureRangeState> result;
    for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
        for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
            const auto& state = entry.cells[static_cast<size_t>(layer) * entry.mips + mip];
            if (state) {
                result.push_back({{mip, 1, layer, 1}, *state});
            }
        }
    }
    return result;
}

//======================================================================================================================
std::optional<TextureState>
TextureStateTable::find(rojoRHI::Texture texture,
                        const rojoRHI::SubresourceRange& requested) const {
    const auto found = m_entries.find(texture.bits);
    if (found == m_entries.end() || found->second.cells.empty()) {
        return std::nullopt;
    }
    const auto range = resolved(requested, found->second.mips, found->second.layers);
    const auto states = physicalRanges(texture, range);
    if (states.size() != static_cast<size_t>(range.mipCount) * range.layerCount) {
        return std::nullopt;
    }
    TextureState combined = states.front().state;
    for (const auto& state : states) {
        if (state.state.layout != combined.layout) {
            return std::nullopt;
        }
        combined.stage = combined.stage | state.state.stage;
        combined.access = combined.access | state.state.access;
    }
    return combined;
}

//======================================================================================================================
void TextureStateTable::set(rojoRHI::Texture texture, const rojoRHI::SubresourceRange& requested,
                            const TextureState& state) {
    const auto found = m_entries.find(texture.bits);
    LMX_ASSERT(found != m_entries.end() && !found->second.cells.empty(),
               "seed texture dimensions before setting physical state");
    Entry& entry = found->second;
    const auto range = resolved(requested, entry.mips, entry.layers);
    for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
        for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
            entry.cells[static_cast<size_t>(layer) * entry.mips + mip] = state;
        }
    }
}

//======================================================================================================================
void TextureStateTable::forget(rojoRHI::Texture texture) {
    m_entries.erase(texture.bits);
}
} // namespace lmx::render
