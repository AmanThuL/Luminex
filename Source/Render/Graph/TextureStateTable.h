//----------------------------------------------------------------------------------------------------------------------
/// @file TextureStateTable.h
/// @brief Declares independent logical uses and physical subresource frontiers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/ResourceUse.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rojoRHI/RHI.h>
#include <unordered_map>
#include <vector>

namespace lmx::render {
/// The outstanding dependency frontier, valid in exactly one layout.
struct TextureState {
    rojoRHI::Stage stage = rojoRHI::Stage::None;         ///< Outstanding stages.
    rojoRHI::Access access = rojoRHI::Access::None;      ///< Accesses valid in layout.
    rojoRHI::Layout layout = rojoRHI::Layout::Undefined; ///< Actual physical layout.
    /// Compares all frontier fields.
    bool operator==(const TextureState&) const = default;
};
/// An exact disjoint subresource range and its physical state.
struct TextureRangeState {
    rojoRHI::SubresourceRange range; ///< Resolved mip/layer cells.
    TextureState state;              ///< Frontier for every covered cell.
};
/// Renderer-owned states keyed by texture identity; owns no GPU object and is recording-thread
/// only. Logical uses preserve diagnostic records; physical cells describe actual synchronization.
class TextureStateTable {
public:
    /// Returns the whole-resource diagnostic use, if present.
    std::optional<TextureUse> logicalUse(rojoRHI::Texture texture) const;
    /// Replaces only the diagnostic use.
    void setLogicalUse(rojoRHI::Texture texture, TextureUse use);
    /// Initializes absent physical cells from immutable creation facts, preserving known cells.
    void seedPhysical(rojoRHI::Texture texture, uint32_t mips, uint32_t layers,
                      rojoRHI::Layout initialLayout);
    /// Returns exact disjoint known cells; absent resources return no ranges.
    std::vector<TextureRangeState> physicalRanges(rojoRHI::Texture texture,
                                                  const rojoRHI::SubresourceRange& range) const;
    /// Combines masks only when every requested cell is known and has the same layout.
    std::optional<TextureState> find(rojoRHI::Texture texture,
                                     const rojoRHI::SubresourceRange& range) const;
    /// Replaces requested cells only; requires dimensions established by seedPhysical.
    void set(rojoRHI::Texture texture, const rojoRHI::SubresourceRange& range,
             const TextureState& state);
    /// Removes both channels before a destroyed identity can be reused.
    void forget(rojoRHI::Texture texture);
    /// Counts distinct identities with either channel.
    std::size_t size() const { return m_entries.size(); }

private:
    struct Entry {
        std::optional<TextureUse> logical;
        uint32_t mips = 0;
        uint32_t layers = 0;
        std::vector<std::optional<TextureState>> cells;
    };
    std::unordered_map<uint64_t, Entry> m_entries;
};
} // namespace lmx::render
