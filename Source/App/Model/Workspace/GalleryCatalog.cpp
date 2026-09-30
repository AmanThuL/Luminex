//----------------------------------------------------------------------------------------------------------------------
/// @file GalleryCatalog.cpp
/// @brief Provides the immutable Style Gallery component inventory.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Workspace/GalleryCatalog.h"

#include <array>

namespace lmx::app {
namespace {
constexpr std::array kCatalog{
    GalleryEntry{GalleryComponent::Button, "Button"},
    GalleryEntry{GalleryComponent::IconButton, "Icon button"},
    GalleryEntry{GalleryComponent::Checkbox, "Checkbox"},
    GalleryEntry{GalleryComponent::Chip, "Chip"},
    GalleryEntry{GalleryComponent::FieldText, "Field/Text"},
    GalleryEntry{GalleryComponent::FieldNumber, "Field/Number"},
    GalleryEntry{GalleryComponent::FieldSelect, "Field/Select"},
    GalleryEntry{GalleryComponent::FieldSlider, "Field/Slider"},
    GalleryEntry{GalleryComponent::DockTab, "Dock tab"},
    GalleryEntry{GalleryComponent::MenuItem, "Menu item"},
    GalleryEntry{GalleryComponent::HierarchyRow, "Hierarchy row"},
    GalleryEntry{GalleryComponent::PropertyRow, "Property row"},
    GalleryEntry{GalleryComponent::SubjectHeader, "Subject header"},
    GalleryEntry{GalleryComponent::TopicHeader, "Topic header"},
    GalleryEntry{GalleryComponent::Notice, "Notice"},
    GalleryEntry{GalleryComponent::LegendChip, "Legend chip"},
    GalleryEntry{GalleryComponent::GraphCard, "Graph card"},
    GalleryEntry{GalleryComponent::ConsoleRow, "Console row"},
};
} // namespace

//======================================================================================================================
std::span<const GalleryEntry> galleryCatalog() {
    return kCatalog;
}

} // namespace lmx::app
