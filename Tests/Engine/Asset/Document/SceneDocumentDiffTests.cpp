#include "Engine/Asset/Document/SceneDocumentDiff.h"

#include "Engine/Asset/Model/JsonTokens.h"
#include "Support/SceneDocumentFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace lmx::asset;
using lmx::test::animatedDocument;
using lmx::test::completeDocument;

namespace {

using OwnerIndex = std::pair<DocumentChangeOwner, uint32_t>;

//======================================================================================================================
OwnerIndex ownerForPointer(std::string_view pointer) {
    const auto indexAfter = [&](std::string_view prefix,
                                DocumentChangeOwner owner) -> std::optional<OwnerIndex> {
        if (!pointer.starts_with(prefix))
            return std::nullopt;
        auto tail = pointer.substr(prefix.size());
        const auto end = tail.find('/');
        tail = tail.substr(0, end);
        if (tail.empty() ||
            !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        return OwnerIndex{owner, static_cast<uint32_t>(std::stoul(std::string(tail)))};
    };
    if (auto owner = indexAfter("/nodes/", DocumentChangeOwner::Node))
        return *owner;
    if (auto owner = indexAfter("/cameras/", DocumentChangeOwner::Camera))
        return *owner;
    if (auto owner =
            indexAfter("/extensions/KHR_lights_punctual/lights/", DocumentChangeOwner::Light))
        return *owner;
    if (auto owner = indexAfter("/animations/", DocumentChangeOwner::Animation))
        return *owner;
    if (pointer.starts_with("/extensions/LMX_scene/look/"))
        return {DocumentChangeOwner::Look, 0};
    return {DocumentChangeOwner::Document, 0};
}

//======================================================================================================================
bool comparePointers(const std::optional<JsonNode>& before, const std::optional<JsonNode>& after,
                     std::string pointer, std::set<OwnerIndex>& changed) {
    if (!before || !after) {
        const auto& present = before ? before : after;
        if (present && present->isObject() && present->size() > 0) {
            bool found = false;
            for (size_t i = 0; i < present->size(); ++i) {
                const auto key = present->memberName(i);
                found = comparePointers(before ? before->find(key) : std::nullopt,
                                        after ? after->find(key) : std::nullopt,
                                        pointer + "/" + key, changed) ||
                        found;
            }
            return found;
        }
        if (present && present->isArray() && present->size() > 0) {
            bool found = false;
            for (size_t i = 0; i < present->size(); ++i)
                found = comparePointers(before ? std::optional(before->at(i)) : std::nullopt,
                                        after ? std::optional(after->at(i)) : std::nullopt,
                                        pointer + "/" + std::to_string(i), changed) ||
                        found;
            return found;
        }
        changed.insert(ownerForPointer(pointer));
        return true;
    }
    if (before->isObject() && after->isObject()) {
        std::set<std::string> keys;
        for (size_t i = 0; i < before->size(); ++i)
            keys.insert(before->memberName(i));
        for (size_t i = 0; i < after->size(); ++i)
            keys.insert(after->memberName(i));
        bool found = false;
        for (const auto& key : keys)
            found = comparePointers(before->find(key), after->find(key), pointer + "/" + key,
                                    changed) ||
                    found;
        if (!found && before->sourceJson() != after->sourceJson()) {
            changed.insert(ownerForPointer(pointer));
            return true;
        }
        return found;
    }
    if (before->isArray() && after->isArray()) {
        bool found = false;
        for (size_t i = 0; i < std::max(before->size(), after->size()); ++i)
            found =
                comparePointers(i < before->size() ? std::optional(before->at(i)) : std::nullopt,
                                i < after->size() ? std::optional(after->at(i)) : std::nullopt,
                                pointer + "/" + std::to_string(i), changed) ||
                found;
        if (!found && before->sourceJson() != after->sourceJson()) {
            changed.insert(ownerForPointer(pointer));
            return true;
        }
        return found;
    }
    if (before->sourceJson() != after->sourceJson()) {
        changed.insert(ownerForPointer(pointer));
        return true;
    }
    return false;
}

//======================================================================================================================
std::optional<JsonNode> member(const std::optional<JsonNode>& node, std::string_view key) {
    return node && node->isObject() ? node->find(key) : std::nullopt;
}

//======================================================================================================================
std::optional<JsonNode> element(const std::optional<JsonNode>& node, size_t index) {
    return node && node->isArray() && index < node->size() ? std::optional(node->at(index))
                                                           : std::nullopt;
}

//======================================================================================================================
uint64_t unsignedValue(const std::optional<JsonNode>& node) {
    REQUIRE(node);
    const auto value = node->asUInt();
    REQUIRE(value);
    return *value;
}

//======================================================================================================================
std::vector<std::byte> accessorBytes(const JsonNode& root, const std::vector<std::byte>& buffer,
                                     const JsonNode& sampler, std::string_view key) {
    const std::optional<JsonNode> document = root;
    const auto index = unsignedValue(member(sampler, key));
    const auto accessor = element(member(document, "accessors"), index);
    const auto viewIndex = unsignedValue(member(accessor, "bufferView"));
    const auto view = element(member(document, "bufferViews"), viewIndex);
    const auto offset = unsignedValue(member(view, "byteOffset"));
    const auto length = unsignedValue(member(view, "byteLength"));
    REQUIRE(offset <= buffer.size());
    REQUIRE(length <= buffer.size() - offset);
    return {buffer.begin() + offset, buffer.begin() + offset + length};
}

//======================================================================================================================
void compareBufferRanges(const JsonNode& a, const JsonNode& b,
                         const std::vector<std::byte>& oldBuffer,
                         const std::vector<std::byte>& newBuffer, std::set<OwnerIndex>& changed) {
    const auto oldAnimations = member(a, "animations");
    const auto newAnimations = member(b, "animations");
    const size_t count = std::max(oldAnimations ? oldAnimations->size() : 0,
                                  newAnimations ? newAnimations->size() : 0);
    for (size_t i = 0; i < count; ++i) {
        const auto oldSamplers = member(element(oldAnimations, i), "samplers");
        const auto newSamplers = member(element(newAnimations, i), "samplers");
        const size_t channels =
            std::max(oldSamplers ? oldSamplers->size() : 0, newSamplers ? newSamplers->size() : 0);
        for (size_t j = 0; j < channels; ++j) {
            const auto oldSampler = element(oldSamplers, j);
            const auto newSampler = element(newSamplers, j);
            if (!oldSampler || !newSampler) {
                changed.insert({DocumentChangeOwner::Animation, static_cast<uint32_t>(i)});
                continue;
            }
            for (const auto key : {"input", "output"}) {
                if (accessorBytes(a, oldBuffer, *oldSampler, key) !=
                    accessorBytes(b, newBuffer, *newSampler, key))
                    changed.insert({DocumentChangeOwner::Animation, static_cast<uint32_t>(i)});
            }
        }
    }
}

//======================================================================================================================
std::set<OwnerIndex> oracle(const SceneDocument& before, const SceneDocument& after) {
    const auto a = JsonTokens::parse(sceneDocumentJson(before, "scene.bin"));
    const auto b = JsonTokens::parse(sceneDocumentJson(after, "scene.bin"));
    REQUIRE(a);
    REQUIRE(b);
    std::set<OwnerIndex> changed;
    comparePointers(a->root(), b->root(), "", changed);
    compareBufferRanges(a->root(), b->root(), sceneDocumentBuffer(before),
                        sceneDocumentBuffer(after), changed);
    return changed;
}

//======================================================================================================================
std::set<OwnerIndex> jsonOwners(const SceneDocument& before, const SceneDocument& after) {
    const auto a = JsonTokens::parse(sceneDocumentJson(before, "scene.bin"));
    const auto b = JsonTokens::parse(sceneDocumentJson(after, "scene.bin"));
    REQUIRE(a);
    REQUIRE(b);
    std::set<OwnerIndex> changed;
    comparePointers(a->root(), b->root(), "", changed);
    return changed;
}

//======================================================================================================================
void checkSingle(const SceneDocument& before, const SceneDocument& after, DocumentChangeOwner owner,
                 uint32_t index, std::string_view property, std::string_view oldText,
                 std::string_view newText) {
    const auto changes = diffSceneDocuments(before, after);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].owner == owner);
    CHECK(changes[0].index == index);
    CHECK(changes[0].property == property);
    CHECK(changes[0].before == oldText);
    CHECK(changes[0].after == newText);
}

//======================================================================================================================
std::string atCanonicalPointer(const SceneDocument& doc, std::string_view pointer) {
    const auto json = JsonTokens::parse(sceneDocumentJson(doc, "scene.bin"));
    REQUIRE(json);
    std::optional<JsonNode> node = json->root();
    while (!pointer.empty() && node) {
        REQUIRE(pointer.front() == '/');
        pointer.remove_prefix(1);
        const auto slash = pointer.find('/');
        const auto component = pointer.substr(0, slash);
        if (node->isArray()) {
            const auto index = std::stoul(std::string(component));
            node = index < node->size() ? std::optional(node->at(index)) : std::nullopt;
        } else {
            node = node->find(component);
        }
        pointer = slash == std::string_view::npos ? std::string_view{} : pointer.substr(slash);
    }
    return node ? std::string(node->sourceJson()) : std::string{};
}

struct FieldCase {
    const char* label;
    std::function<void(SceneDocument&)> edit;
    DocumentChangeOwner owner;
    uint32_t index;
    const char* property;
    const char* pointer;
};

//======================================================================================================================
void checkFields(const SceneDocument& base, const std::vector<FieldCase>& cases) {
    for (const auto& test : cases) {
        INFO(test.label);
        auto edited = base;
        test.edit(edited);
        const auto rows = diffSceneDocuments(base, edited);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].owner == test.owner);
        CHECK(rows[0].index == test.index);
        CHECK(rows[0].property == test.property);
        CHECK(rows[0].before == atCanonicalPointer(base, test.pointer));
        CHECK(rows[0].after == atCanonicalPointer(edited, test.pointer));
        CHECK(rows[0].before != rows[0].after);
    }
}

} // namespace

//======================================================================================================================
TEST_CASE("independent oracle attributes only the changed animation buffer range",
          "[asset][scene-document-diff]") {
    auto base = completeDocument();
    base.animations.push_back(base.animations[0]);
    base.animations[1].name = "Second rail";
    auto edited = base;
    edited.animations[0].channels[0].values[0].x = 9.0f;
    const std::set<OwnerIndex> expected{{DocumentChangeOwner::Animation, 0}};
    CHECK(jsonOwners(base, edited).empty());
    CHECK(oracle(base, edited) == expected);
    const auto rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].owner == DocumentChangeOwner::Animation);
    CHECK(rows[0].index == 0);
}

//======================================================================================================================
TEST_CASE("independent pointer oracle descends first and last owner containers",
          "[asset][scene-document-diff]") {
    const auto empty = animatedDocument();
    auto lit = empty;
    lit.lights.push_back({.name = "First"});
    const std::set<OwnerIndex> lightOwners{{DocumentChangeOwner::Document, 0},
                                           {DocumentChangeOwner::Light, 0}};
    CHECK(jsonOwners(empty, lit) == lightOwners);
    CHECK(jsonOwners(lit, empty) == lightOwners);

    auto still = empty;
    still.animations.clear();
    const std::set<OwnerIndex> animationOwners{{DocumentChangeOwner::Document, 0},
                                               {DocumentChangeOwner::Animation, 0}};
    CHECK(jsonOwners(still, empty) == animationOwners);
    CHECK(jsonOwners(empty, still) == animationOwners);
}

//======================================================================================================================
TEST_CASE("independent pointer oracle distinguishes canonical positive and negative zero",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto edited = base;
    edited.look.exposure.ev = -0.0f;
    const std::set<OwnerIndex> expected{{DocumentChangeOwner::Look, 0}};
    CHECK(jsonOwners(base, edited) == expected);
    const auto rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].owner == DocumentChangeOwner::Look);
    CHECK(rows[0].before != rows[0].after);
}

//======================================================================================================================
TEST_CASE("independent pointer oracle sees canonical object member order",
          "[asset][scene-document-diff]") {
    auto base = completeDocument();
    base.nodes[2].generator->params.push_back({"second", 2.5});
    auto edited = base;
    std::reverse(edited.nodes[2].generator->params.begin(),
                 edited.nodes[2].generator->params.end());
    REQUIRE(sceneDocumentJson(base, "scene.bin") != sceneDocumentJson(edited, "scene.bin"));
    const std::set<OwnerIndex> expected{{DocumentChangeOwner::Node, 2}};
    CHECK(jsonOwners(base, edited) == expected);
    CHECK(oracle(base, edited) == expected);
    const auto rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].owner == DocumentChangeOwner::Node);
    CHECK(rows[0].index == 2);
    CHECK(rows[0].property == "generator");
    CHECK(rows[0].before != rows[0].after);
}

//======================================================================================================================
TEST_CASE("independent pointer oracle attributes an added empty container to its node",
          "[asset][scene-document-diff]") {
    const auto before = JsonTokens::parse(R"({"nodes":[{"name":"A"}]})");
    const auto after = JsonTokens::parse(
        R"({"nodes":[{"name":"A","extensions":{"LMX_scene":{"generator":{"params":{}}}}}]})");
    REQUIRE(before);
    REQUIRE(after);
    std::set<OwnerIndex> changed;
    comparePointers(before->root(), after->root(), "", changed);
    CHECK(changed == std::set<OwnerIndex>{{DocumentChangeOwner::Node, 0}});
}

//======================================================================================================================
TEST_CASE("scene diff rows sort by owner then index then property",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto edited = base;
    edited.loop = false;
    edited.nodes[0].enabled = false;
    edited.nodes[1].asset->uri += "x";
    edited.nodes[1].enabled = false;
    edited.nodes[2].name = "Renamed";
    edited.cameras[0].name = "Wide";
    edited.lights[0].intensity = 2.0;
    edited.look.bloom.enabled = false;
    const auto rows = diffSceneDocuments(base, edited);
    std::vector<std::tuple<DocumentChangeOwner, uint32_t, std::string>> actual;
    for (const auto& row : rows)
        actual.emplace_back(row.owner, row.index, row.property);
    const std::vector<std::tuple<DocumentChangeOwner, uint32_t, std::string>> expected{
        {DocumentChangeOwner::Document, 0, "extensions/LMX_scene/loop"},
        {DocumentChangeOwner::Node, 0, "enabled"},
        {DocumentChangeOwner::Node, 1, "asset"},
        {DocumentChangeOwner::Node, 1, "enabled"},
        {DocumentChangeOwner::Node, 2, "name"},
        {DocumentChangeOwner::Camera, 0, "name"},
        {DocumentChangeOwner::Light, 0, "intensity"},
        {DocumentChangeOwner::Look, 0, "bloom"}};
    CHECK(actual == expected);
}

//======================================================================================================================
TEST_CASE("scene diff reports canonical owner properties and animation samples",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto edited = base;
    edited.nodes[0].translation = {1.0f, 2.0f, 3.0f};
    checkSingle(base, edited, DocumentChangeOwner::Node, 0, "translation", "", "[1, 2, 3]");
    edited = base;
    edited.nodes[1].enabled = false;
    checkSingle(base, edited, DocumentChangeOwner::Node, 1, "enabled", "true", "false");
    edited = base;
    edited.cameras[0].name = "Wide";
    checkSingle(base, edited, DocumentChangeOwner::Camera, 0, "name", "\"Perspective\"",
                "\"Wide\"");
    edited = base;
    edited.lights[0].intensity = 2.0;
    checkSingle(base, edited, DocumentChangeOwner::Light, 0, "intensity", "1", "2");
    edited = base;
    edited.look.shadowFilter = ShadowFilter::PCSS;
    const auto look = diffSceneDocuments(base, edited);
    REQUIRE(look.size() == 1);
    CHECK(look[0].owner == DocumentChangeOwner::Look);
    CHECK(look[0].property == "shadow");
    CHECK(look[0].before.find("\"pcf\"") != std::string::npos);
    CHECK(look[0].after.find("\"pcss\"") != std::string::npos);
    edited = base;
    edited.animations[0].channels[0].values[1].x = 7.0f;
    const auto samples = diffSceneDocuments(base, edited);
    REQUIRE(samples.size() == 1);
    CHECK(samples[0].owner == DocumentChangeOwner::Animation);
    CHECK(samples[0].index == 0);
    CHECK(samples[0].property == "channels/0/samples");
    CHECK(samples[0].before.find("[1, 2, 3]") != std::string::npos);
    CHECK(samples[0].after.find("[7, 2, 3]") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("scene diff covers inserted and removed node indices", "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto added = base;
    added.nodes.push_back({.name = "New"});
    const auto addRows = diffSceneDocuments(base, added);
    REQUIRE(addRows.size() == 1);
    CHECK(addRows[0].owner == DocumentChangeOwner::Node);
    CHECK(addRows[0].index == 4);
    CHECK(addRows[0].property.empty());
    CHECK(addRows[0].before.empty());
    CHECK(addRows[0].after == atCanonicalPointer(added, "/nodes/4"));
    const auto removeRows = diffSceneDocuments(added, base);
    REQUIRE(removeRows.size() == 1);
    CHECK(removeRows[0].owner == DocumentChangeOwner::Node);
    CHECK(removeRows[0].index == 4);
    CHECK(removeRows[0].property.empty());
    CHECK(removeRows[0].before == atCanonicalPointer(added, "/nodes/4"));
    CHECK(removeRows[0].after.empty());
    CHECK(diffSceneDocuments(base, base).empty());
}

//======================================================================================================================
TEST_CASE("scene diff emits one whole-element row per added or removed owner index",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto cameraAdded = base;
    cameraAdded.cameras.push_back({.name = "Second"});
    for (const auto& [before, after] :
         {std::pair{base, cameraAdded}, std::pair{cameraAdded, base}}) {
        const auto rows = diffSceneDocuments(before, after);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].owner == DocumentChangeOwner::Camera);
        CHECK(rows[0].index == 1);
        CHECK(rows[0].property.empty());
        CHECK(rows[0].before == atCanonicalPointer(before, "/cameras/1"));
        CHECK(rows[0].after == atCanonicalPointer(after, "/cameras/1"));
    }
    auto lightAdded = base;
    lightAdded.lights.push_back({.name = "Second"});
    for (const auto& [before, after] : {std::pair{base, lightAdded}, std::pair{lightAdded, base}}) {
        const auto rows = diffSceneDocuments(before, after);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].owner == DocumentChangeOwner::Light);
        CHECK(rows[0].index == 1);
        CHECK(rows[0].property.empty());
        CHECK(rows[0].before ==
              atCanonicalPointer(before, "/extensions/KHR_lights_punctual/lights/1"));
        CHECK(rows[0].after ==
              atCanonicalPointer(after, "/extensions/KHR_lights_punctual/lights/1"));
    }
    auto animationAdded = base;
    animationAdded.animations.push_back(base.animations[0]);
    animationAdded.animations[1].name = "Second";
    for (const auto& [before, after] :
         {std::pair{base, animationAdded}, std::pair{animationAdded, base}}) {
        const auto rows = diffSceneDocuments(before, after);
        const auto whole = std::ranges::count_if(rows, [](const DocumentChange& row) {
            return row.owner == DocumentChangeOwner::Animation && row.index == 1 &&
                   row.property.empty();
        });
        const auto samples = std::ranges::count_if(rows, [](const DocumentChange& row) {
            return row.owner == DocumentChangeOwner::Animation && row.index == 1 &&
                   row.property == "channels/0/samples";
        });
        CHECK(whole == 1);
        CHECK(samples == 1);
        const auto element = std::ranges::find_if(rows, [](const DocumentChange& row) {
            return row.owner == DocumentChangeOwner::Animation && row.index == 1 &&
                   row.property.empty();
        });
        REQUIRE(element != rows.end());
        CHECK(element->before == atCanonicalPointer(before, "/animations/1"));
        CHECK(element->after == atCanonicalPointer(after, "/animations/1"));
    }
}

//======================================================================================================================
TEST_CASE("scene diff covers nested extension values and binary bit differences",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    auto edited = base;
    edited.nodes[1].asset->uri = "Fetched/replacement.gltf";
    auto rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].owner == DocumentChangeOwner::Node);
    CHECK(rows[0].property == "asset");
    CHECK(rows[0].before.find("Fetched/model.gltf") != std::string::npos);
    CHECK(rows[0].after.find("Fetched/replacement.gltf") != std::string::npos);
    edited = base;
    edited.nodes[1].overrides[0].pose->eulerDegrees.y = 45.0f;
    rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].property == "overrides");
    CHECK(rows[0].after.find("45") != std::string::npos);
    edited = base;
    edited.animations[0].channels[0].values[0].x = -0.0f;
    rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].property == "channels/0/samples");
    CHECK(rows[0].before != rows[0].after);
    edited = base;
    edited.animations[0].channels[0].values[0].w = 99.0f;
    edited.warnings.push_back("diagnostic only");
    edited.sourceBufferUri = "another.bin";
    CHECK(diffSceneDocuments(base, edited).empty());
}

//======================================================================================================================
TEST_CASE("scene diff reports every node and camera field at its canonical property",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    checkFields(
        base,
        {{"node name", [](auto& d) { d.nodes[0].name = "View"; }, DocumentChangeOwner::Node, 0,
          "name", "/nodes/0/name"},
         {"node translation", [](auto& d) { d.nodes[0].translation.x = 2.0f; },
          DocumentChangeOwner::Node, 0, "translation", "/nodes/0/translation"},
         {"node rotation", [](auto& d) { d.nodes[0].rotation.x = 0.25f; },
          DocumentChangeOwner::Node, 0, "rotation", "/nodes/0/rotation"},
         {"node scale", [](auto& d) { d.nodes[0].scale.x = 2.0f; }, DocumentChangeOwner::Node, 0,
          "scale", "/nodes/0/scale"},
         {"node camera", [](auto& d) { d.nodes[0].camera.reset(); }, DocumentChangeOwner::Node, 0,
          "camera", "/nodes/0/camera"},
         {"node light", [](auto& d) { d.nodes[3].light.reset(); }, DocumentChangeOwner::Node, 3,
          "extensions/KHR_lights_punctual", "/nodes/3/extensions/KHR_lights_punctual"},
         {"node enabled", [](auto& d) { d.nodes[1].enabled = false; }, DocumentChangeOwner::Node, 1,
          "enabled", "/nodes/1/extensions/LMX_scene/enabled"},
         {"node asset", [](auto& d) { d.nodes[1].asset->uri += "x"; }, DocumentChangeOwner::Node, 1,
          "asset", "/nodes/1/extensions/LMX_scene/asset"},
         {"node overrides", [](auto& d) { d.nodes[1].overrides[0].name += "x"; },
          DocumentChangeOwner::Node, 1, "overrides", "/nodes/1/extensions/LMX_scene/overrides"},
         {"node generator", [](auto& d) { d.nodes[2].generator->name += "x"; },
          DocumentChangeOwner::Node, 2, "generator", "/nodes/2/extensions/LMX_scene/generator"},
         {"node role", [](auto& d) { d.nodes[3].role = "fill"; }, DocumentChangeOwner::Node, 3,
          "role", "/nodes/3/extensions/LMX_scene/role"},
         {"node castsShadow", [](auto& d) { d.nodes[3].castsShadow = false; },
          DocumentChangeOwner::Node, 3, "castsShadow", "/nodes/3/extensions/LMX_scene/castsShadow"},
         {"camera name", [](auto& d) { d.cameras[0].name += "x"; }, DocumentChangeOwner::Camera, 0,
          "name", "/cameras/0/name"},
         {"camera fov", [](auto& d) { d.cameras[0].fovY += 0.1f; }, DocumentChangeOwner::Camera, 0,
          "perspective", "/cameras/0/perspective"},
         {"camera near", [](auto& d) { d.cameras[0].nearZ += 0.1f; }, DocumentChangeOwner::Camera,
          0, "perspective", "/cameras/0/perspective"},
         {"camera far", [](auto& d) { d.cameras[0].farZ.reset(); }, DocumentChangeOwner::Camera, 0,
          "perspective", "/cameras/0/perspective"},
         {"camera aspect", [](auto& d) { d.cameras[0].aspectRatio = 1.5f; },
          DocumentChangeOwner::Camera, 0, "perspective", "/cameras/0/perspective"}});
    auto nested = base;
    nested.nodes[0].children = {1, 2};
    nested.rootNodes = {0, 3};
    checkFields(nested, {{"node children", [](auto& d) { d.nodes[0].children = {2, 1}; },
                          DocumentChangeOwner::Node, 0, "children", "/nodes/0/children"}});
}

//======================================================================================================================
TEST_CASE("scene diff reports every light and look field at its canonical property",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    checkFields(
        base,
        {{"light name", [](auto& d) { d.lights[0].name += "x"; }, DocumentChangeOwner::Light, 0,
          "name", "/extensions/KHR_lights_punctual/lights/0/name"},
         {"light color", [](auto& d) { d.lights[0].colour.r = 0.5; }, DocumentChangeOwner::Light, 0,
          "color", "/extensions/KHR_lights_punctual/lights/0/color"},
         {"light intensity", [](auto& d) { d.lights[0].intensity = 2.0; },
          DocumentChangeOwner::Light, 0, "intensity",
          "/extensions/KHR_lights_punctual/lights/0/intensity"},
         {"light range", [](auto& d) { d.lights[0].range = 10.0f; }, DocumentChangeOwner::Light, 0,
          "range", "/extensions/KHR_lights_punctual/lights/0/range"},
         {"exposure ev", [](auto& d) { d.look.exposure.ev = 1.0f; }, DocumentChangeOwner::Look, 0,
          "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure auto", [](auto& d) { d.look.exposure.autoEnabled = true; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure low", [](auto& d) { d.look.exposure.lowPercentile = 45.0f; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure high", [](auto& d) { d.look.exposure.highPercentile = 90.0f; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure grey", [](auto& d) { d.look.exposure.targetGrey = 0.2f; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure min", [](auto& d) { d.look.exposure.evMin = -7.0f; }, DocumentChangeOwner::Look,
          0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure max", [](auto& d) { d.look.exposure.evMax = 7.0f; }, DocumentChangeOwner::Look,
          0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure compensation", [](auto& d) { d.look.exposure.compensationEv = 1.0f; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure adapt up", [](auto& d) { d.look.exposure.adaptUpStopsPerSecond = 4; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"exposure adapt down", [](auto& d) { d.look.exposure.adaptDownStopsPerSecond = 2; },
          DocumentChangeOwner::Look, 0, "exposure", "/extensions/LMX_scene/look/exposure"},
         {"bloom enabled", [](auto& d) { d.look.bloom.enabled = false; }, DocumentChangeOwner::Look,
          0, "bloom", "/extensions/LMX_scene/look/bloom"},
         {"bloom threshold", [](auto& d) { d.look.bloom.threshold = 2; }, DocumentChangeOwner::Look,
          0, "bloom", "/extensions/LMX_scene/look/bloom"},
         {"bloom intensity", [](auto& d) { d.look.bloom.intensity = 0.4f; },
          DocumentChangeOwner::Look, 0, "bloom", "/extensions/LMX_scene/look/bloom"},
         {"shadow filter", [](auto& d) { d.look.shadowFilter = ShadowFilter::PCSS; },
          DocumentChangeOwner::Look, 0, "shadow", "/extensions/LMX_scene/look/shadow"},
         {"sky color", [](auto& d) { d.look.environment.skySrgb8[0]++; }, DocumentChangeOwner::Look,
          0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri uri", [](auto& d) { d.look.environment.hdri->uri += "x"; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri hash", [](auto& d) { d.look.environment.hdri->sha256[0] = 'b'; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri yaw", [](auto& d) { d.look.environment.hdri->yaw = 0.5f; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri scale", [](auto& d) { d.look.environment.hdri->scale = 0.5f; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri face", [](auto& d) { d.look.environment.hdri->faceSize = 256; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"},
         {"hdri diffuse", [](auto& d) { d.look.environment.hdri->diffuseFaceSize = 64; },
          DocumentChangeOwner::Look, 0, "environment", "/extensions/LMX_scene/look/environment"}});
    auto spot = base;
    spot.lights[0].type = DocLightType::Spot;
    spot.lights[0].range = 10.0f;
    checkFields(
        spot,
        {{"spot inner", [](auto& d) { d.lights[0].innerCone = 0.1f; }, DocumentChangeOwner::Light,
          0, "spot", "/extensions/KHR_lights_punctual/lights/0/spot"},
         {"spot outer", [](auto& d) { d.lights[0].outerCone = 0.9f; }, DocumentChangeOwner::Light,
          0, "spot", "/extensions/KHR_lights_punctual/lights/0/spot"}});
    auto typed = base;
    typed.lights[0].range = 10.0f;
    checkFields(typed, {{"light type", [](auto& d) { d.lights[0].type = DocLightType::Point; },
                         DocumentChangeOwner::Light, 0, "type",
                         "/extensions/KHR_lights_punctual/lights/0/type"}});
}

//======================================================================================================================
TEST_CASE("scene diff reports animation fields and canonical layout consequences",
          "[asset][scene-document-diff]") {
    const auto base = completeDocument();
    checkFields(base,
                {{"animation name", [](auto& d) { d.animations[0].name += "x"; },
                  DocumentChangeOwner::Animation, 0, "name", "/animations/0/name"},
                 {"channel target node", [](auto& d) { d.animations[0].channels[0].node = 1; },
                  DocumentChangeOwner::Animation, 0, "channels", "/animations/0/channels"},
                 {"channel target path",
                  [](auto& d) { d.animations[0].channels[0].path = DocChannelPath::Scale; },
                  DocumentChangeOwner::Animation, 0, "channels", "/animations/0/channels"},
                 {"channel interpolation", [](auto& d) { d.animations[0].channels[0].step = true; },
                  DocumentChangeOwner::Animation, 0, "samplers", "/animations/0/samplers"}});
    auto singleKey = base;
    singleKey.animations[0].keyCount = 1;
    singleKey.animations[0].channels[0].values.resize(1);
    checkFields(singleKey,
                {{"sample rate", [](auto& d) { d.animations[0].sampleRate = 30.0; },
                  DocumentChangeOwner::Animation, 0, "extensions", "/animations/0/extensions"}});
    auto edited = base;
    edited.animations[0].channels[0].path = DocChannelPath::Rotation;
    const auto rows = diffSceneDocuments(base, edited);
    CHECK(std::ranges::any_of(rows, [](const DocumentChange& row) {
        return row.owner == DocumentChangeOwner::Animation && row.index == 0 &&
               row.property == "channels";
    }));
    CHECK(std::ranges::any_of(rows, [](const DocumentChange& row) {
        return row.owner == DocumentChangeOwner::Document && row.property == "accessors";
    }));
    CHECK(std::ranges::any_of(rows, [](const DocumentChange& row) {
        return row.owner == DocumentChangeOwner::Animation && row.property == "channels/0/samples";
    }));
}

//======================================================================================================================
TEST_CASE("scene diff owner set matches independent recursive canonical comparison for 40 edits",
          "[asset][scene-document-diff]") {
    std::mt19937 rng(42017);
    for (int seed = 0; seed < 40; ++seed) {
        auto base = completeDocument();
        if (seed % 2 != 0) {
            base.animations.push_back(base.animations[0]);
            base.animations[1].name = "Second rail";
        }
        auto edited = base;
        const size_t animationIndex = static_cast<size_t>(seed % 2);
        const int count = 2 + seed % 5;
        for (int n = 0; n < count; ++n) {
            switch (rng() % 34) {
            case 0:
                edited.name += "x";
                break;
            case 1:
                edited.nodes[0].name += "x";
                break;
            case 2:
                edited.nodes[1].enabled = !edited.nodes[1].enabled;
                break;
            case 3:
                edited.nodes[2].generator->params[0].second += 1.0;
                break;
            case 4:
                edited.nodes[3].castsShadow = !edited.nodes[3].castsShadow;
                break;
            case 5:
                edited.cameras[0].fovY += 0.01f;
                break;
            case 6:
                edited.lights[0].intensity += 1.0;
                break;
            case 7:
                edited.look.exposure.ev += 1.0f;
                break;
            case 8:
                edited.look.environment.hdri->yaw += 0.1f;
                break;
            case 9:
                edited.animations[animationIndex].name += "x";
                break;
            case 10:
                edited.animations[animationIndex].channels[0].values[0].x += 1.0f;
                break;
            case 11:
                edited.nodes.push_back({.name = "extra"});
                break;
            case 12:
                edited.nodes[0].translation.x += 1.0f;
                break;
            case 13:
                edited.nodes[0].rotation.x += 0.1f;
                break;
            case 14:
                edited.nodes[0].scale.x += 0.1f;
                break;
            case 15:
                edited.nodes[1].asset->sha256[0] = 'c';
                break;
            case 16:
                edited.nodes[1].overrides[0].enabled = true;
                break;
            case 17:
                edited.nodes[1].overrides[0].pose->translation.z += 1.0f;
                break;
            case 18:
                edited.nodes[2].generator->name += "x";
                break;
            case 19:
                edited.nodes[3].role = "fill";
                break;
            case 20:
                edited.cameras[0].nearZ += 0.01f;
                break;
            case 21:
                edited.cameras[0].farZ.reset();
                break;
            case 22:
                edited.cameras[0].aspectRatio = 1.5f;
                break;
            case 23:
                edited.lights[0].colour.r += 0.1;
                break;
            case 24:
                edited.lights[0].name += "x";
                break;
            case 25:
                edited.lights[0].type = DocLightType::Spot;
                edited.lights[0].range = 10.0f;
                break;
            case 26:
                edited.look.exposure.autoEnabled = !edited.look.exposure.autoEnabled;
                break;
            case 27:
                edited.look.bloom.threshold += 0.1f;
                break;
            case 28:
                edited.look.environment.skySrgb8[0] += 1;
                break;
            case 29:
                edited.look.shadowFilter = ShadowFilter::PCSS;
                break;
            case 30:
                edited.animations[animationIndex].sampleRate += 1.0;
                break;
            case 31:
                edited.animations[animationIndex].channels[0].step = true;
                break;
            case 32:
                edited.animations[animationIndex].channels[0].node = 1;
                break;
            case 33:
                edited.loop = !edited.loop;
                break;
            }
        }
        const auto changes = diffSceneDocuments(base, edited);
        std::set<OwnerIndex> actual;
        for (const auto& row : changes)
            actual.insert({row.owner, row.index});
        CHECK(actual == oracle(base, edited));
        CHECK(changes.empty() ==
              (sceneDocumentJson(base, "scene.bin") == sceneDocumentJson(edited, "scene.bin") &&
               sceneDocumentBuffer(base) == sceneDocumentBuffer(edited)));
    }
}

//======================================================================================================================
TEST_CASE("scene diff reports emissive samples and material target as animation rows",
          "[asset][scene-document-diff][ux6-emissive]") {
    auto base = lmx::test::contentDocument();
    base.animations = {{.name = "Sign",
                        .keyCount = 2,
                        .channels = {{.path = DocChannelPath::EmissiveStrength,
                                      .material = 0,
                                      .step = true,
                                      .values = {{1, 0, 0, 0}, {3, 0, 0, 0}}}}}};
    auto edited = base;
    edited.animations[0].channels[0].values[1].x = 5;
    const auto rows = diffSceneDocuments(base, edited);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].owner == DocumentChangeOwner::Animation);
    CHECK(rows[0].property == "channels/0/samples");
    CHECK(rows[0].before == "[[1], [3]]");
    CHECK(rows[0].after == "[[1], [5]]");
    edited = base;
    edited.animations[0].channels[0].material = 1;
    const auto targetRows = diffSceneDocuments(base, edited);
    REQUIRE(targetRows.size() == 1);
    CHECK(targetRows[0].owner == DocumentChangeOwner::Animation);
    CHECK(targetRows[0].property == "channels");
}
