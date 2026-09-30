#include "App/Model/Workspace/GalleryCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <string_view>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("style gallery names and renderer identities are unique", "[app][gallery]") {
    constexpr std::array names{"Button",         "Icon button",  "Checkbox",      "Chip",
                               "Field/Text",     "Field/Number", "Field/Select",  "Field/Slider",
                               "Dock tab",       "Menu item",    "Hierarchy row", "Property row",
                               "Subject header", "Topic header", "Notice",        "Legend chip",
                               "Graph card",     "Console row"};
    const auto catalog = galleryCatalog();
    REQUIRE(catalog.size() == names.size());
    std::set<std::string_view> uniqueNames;
    std::set<GalleryComponent> uniqueComponents;
    for (const auto& entry : catalog) {
        REQUIRE_FALSE(entry.figmaName.empty());
        REQUIRE(uniqueNames.insert(entry.figmaName).second);
        REQUIRE(uniqueComponents.insert(entry.component).second);
    }
    for (const auto* name : names)
        REQUIRE(uniqueNames.contains(name));
}
