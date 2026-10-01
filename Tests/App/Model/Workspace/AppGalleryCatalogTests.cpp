#include "App/Model/Workspace/GalleryCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string_view>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("style gallery maps exactly 21 Figma names to their renderer identities",
          "[app][gallery]") {
    constexpr std::array expected{
        GalleryEntry{GalleryComponent::Button, "Button"},
        GalleryEntry{GalleryComponent::IconButton, "Icon button"},
        GalleryEntry{GalleryComponent::FieldText, "Field/Text"},
        GalleryEntry{GalleryComponent::FieldNumber, "Field/Number"},
        GalleryEntry{GalleryComponent::FieldSlider, "Field/Slider"},
        GalleryEntry{GalleryComponent::FieldSelect, "Field/Select"},
        GalleryEntry{GalleryComponent::Checkbox, "Checkbox"},
        GalleryEntry{GalleryComponent::DockTab, "Dock tab"},
        GalleryEntry{GalleryComponent::HierarchyRow, "Hierarchy row"},
        GalleryEntry{GalleryComponent::SubjectHeader, "Subject header"},
        GalleryEntry{GalleryComponent::TopicHeader, "Topic header"},
        GalleryEntry{GalleryComponent::PropertyRow, "Property row"},
        GalleryEntry{GalleryComponent::Chip, "Chip"},
        GalleryEntry{GalleryComponent::ActivityStrip, "Activity strip"},
        GalleryEntry{GalleryComponent::ProposalCard, "Proposal card"},
        GalleryEntry{GalleryComponent::Notice, "Notice"},
        GalleryEntry{GalleryComponent::LegendChip, "Legend chip"},
        GalleryEntry{GalleryComponent::ConsoleRow, "Console row"},
        GalleryEntry{GalleryComponent::GraphCard, "Graph card"},
        GalleryEntry{GalleryComponent::MenuItem, "Menu item"},
        GalleryEntry{GalleryComponent::AttentionRing, "Attention ring"},
    };
    const auto catalog = galleryCatalog();
    REQUIRE(catalog.size() == expected.size());
    std::set<std::string_view> uniqueNames;
    std::set<GalleryComponent> uniqueComponents;
    for (const auto& entry : catalog) {
        REQUIRE_FALSE(entry.figmaName.empty());
        REQUIRE(uniqueNames.insert(entry.figmaName).second);
        REQUIRE(uniqueComponents.insert(entry.component).second);
    }
    for (const auto& entry : expected) {
        const auto found = std::ranges::find(catalog, entry.figmaName, &GalleryEntry::figmaName);
        REQUIRE(found != catalog.end());
        REQUIRE(found->component == entry.component);
    }
}
