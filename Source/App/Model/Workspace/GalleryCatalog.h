//----------------------------------------------------------------------------------------------------------------------
/// @file GalleryCatalog.h
/// @brief Names the editor component specimens rendered by the Style Gallery.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::app {

/// Component identities shared by the catalog and its exhaustive specimen renderer.
enum class GalleryComponent : uint8_t {
    Button,        ///< button specimen.
    IconButton,    ///< icon-button specimen.
    Checkbox,      ///< checkbox specimen.
    Chip,          ///< chip specimen.
    FieldText,     ///< field-text specimen.
    FieldNumber,   ///< field-number specimen.
    FieldSelect,   ///< field-select specimen.
    FieldSlider,   ///< field-slider specimen.
    DockTab,       ///< dock-tab specimen.
    MenuItem,      ///< menu-item specimen.
    HierarchyRow,  ///< hierarchy-row specimen.
    PropertyRow,   ///< property-row specimen.
    SubjectHeader, ///< subject-header specimen.
    TopicHeader,   ///< topic-header specimen.
    Notice,        ///< notice specimen.
    LegendChip,    ///< legend-chip specimen.
    GraphCard,     ///< graph-card specimen.
    ConsoleRow,    ///< console-row specimen.
};

/// A renderer identity and its stable Figma component name.
struct GalleryEntry {
    GalleryComponent component; ///< Identifies the specimen renderer.
    std::string_view figmaName; ///< Exact Figma component name with static storage.
};

/// Borrows the immutable catalog; valid for the process lifetime, with no file access.
std::span<const GalleryEntry> galleryCatalog();

} // namespace lmx::app
