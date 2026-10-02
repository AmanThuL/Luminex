//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentDiff.cpp
/// @brief Compares writer-canonical scene properties and binary animation channels.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocumentDiff.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/JsonWriter.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <algorithm>
#include <bit>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace lmx::asset {
namespace {

using Node = std::optional<JsonNode>;

//======================================================================================================================
Node member(const Node& node, std::string_view key) {
    return node && node->isObject() ? node->find(key) : std::nullopt;
}

//======================================================================================================================
Node element(const Node& node, size_t index) {
    return node && node->isArray() && index < node->size() ? Node(node->at(index)) : std::nullopt;
}

//======================================================================================================================
std::string value(const Node& node) {
    return node ? std::string(node->sourceJson()) : std::string{};
}

//======================================================================================================================
std::string ownerName(const Node& before, const Node& after) {
    const auto current = member(after, "name");
    const auto previous = member(before, "name");
    const auto name = current ? current : previous;
    return name && name->isString() ? *name->asString() : std::string{};
}

//======================================================================================================================
void add(std::vector<DocumentChange>& rows, DocumentChangeOwner owner, uint32_t index,
         std::string_view name, std::string property, const Node& before, const Node& after) {
    const auto oldText = value(before);
    const auto newText = value(after);
    if (oldText == newText)
        return;
    rows.push_back({owner, index, std::string(name), std::move(property), oldText, newText});
}

//======================================================================================================================
std::set<std::string> keys(const Node& before, const Node& after) {
    std::set<std::string> names;
    const auto collect = [&](const Node& node) {
        if (node && node->isObject())
            for (size_t i = 0; i < node->size(); ++i)
                names.insert(node->memberName(i));
    };
    collect(before);
    collect(after);
    return names;
}

//======================================================================================================================
void compareObject(std::vector<DocumentChange>& rows, DocumentChangeOwner owner, uint32_t index,
                   std::string_view name, const Node& before, const Node& after,
                   std::string_view prefix = {}) {
    for (const auto& key : keys(before, after))
        add(rows, owner, index, name, std::string(prefix) + key, member(before, key),
            member(after, key));
}

//======================================================================================================================
void compareNodes(std::vector<DocumentChange>& rows, const Node& before, const Node& after) {
    const size_t count = std::max(before ? before->size() : 0, after ? after->size() : 0);
    for (size_t i = 0; i < count; ++i) {
        const auto oldNode = element(before, i);
        const auto newNode = element(after, i);
        const auto name = ownerName(oldNode, newNode);
        if (!oldNode || !newNode) {
            add(rows, DocumentChangeOwner::Node, static_cast<uint32_t>(i), name, "", oldNode,
                newNode);
            continue;
        }
        for (const auto& key : keys(oldNode, newNode)) {
            if (key != "extensions")
                add(rows, DocumentChangeOwner::Node, static_cast<uint32_t>(i), name, key,
                    member(oldNode, key), member(newNode, key));
        }
        const auto oldExtensions = member(oldNode, "extensions");
        const auto newExtensions = member(newNode, "extensions");
        for (const auto& key : keys(oldExtensions, newExtensions)) {
            if (key == "LMX_scene") {
                compareObject(rows, DocumentChangeOwner::Node, static_cast<uint32_t>(i), name,
                              member(oldExtensions, key), member(newExtensions, key));
            } else {
                add(rows, DocumentChangeOwner::Node, static_cast<uint32_t>(i), name,
                    "extensions/" + key, member(oldExtensions, key), member(newExtensions, key));
            }
        }
    }
}

//======================================================================================================================
void compareIndexed(std::vector<DocumentChange>& rows, DocumentChangeOwner owner,
                    const Node& before, const Node& after) {
    const size_t count = std::max(before ? before->size() : 0, after ? after->size() : 0);
    for (size_t i = 0; i < count; ++i) {
        const auto oldItem = element(before, i);
        const auto newItem = element(after, i);
        const auto name = ownerName(oldItem, newItem);
        if (!oldItem || !newItem)
            add(rows, owner, static_cast<uint32_t>(i), name, "", oldItem, newItem);
        else
            compareObject(rows, owner, static_cast<uint32_t>(i), name, oldItem, newItem);
    }
}

//======================================================================================================================
void compareExtensions(std::vector<DocumentChange>& rows, const Node& before, const Node& after) {
    for (const auto& key : keys(before, after)) {
        if (key == "KHR_lights_punctual") {
            const auto oldLights = member(before, key);
            const auto newLights = member(after, key);
            compareIndexed(rows, DocumentChangeOwner::Light, member(oldLights, "lights"),
                           member(newLights, "lights"));
            for (const auto& subkey : keys(oldLights, newLights)) {
                if (subkey != "lights")
                    add(rows, DocumentChangeOwner::Document, 0, {},
                        "extensions/KHR_lights_punctual/" + subkey, member(oldLights, subkey),
                        member(newLights, subkey));
            }
        } else if (key == "LMX_scene") {
            const auto oldScene = member(before, key);
            const auto newScene = member(after, key);
            const auto oldLook = member(oldScene, "look");
            const auto newLook = member(newScene, "look");
            compareObject(rows, DocumentChangeOwner::Look, 0, "Look", oldLook, newLook);
            for (const auto& subkey : keys(oldScene, newScene)) {
                if (subkey != "look")
                    add(rows, DocumentChangeOwner::Document, 0, {},
                        "extensions/LMX_scene/" + subkey, member(oldScene, subkey),
                        member(newScene, subkey));
            }
        } else {
            add(rows, DocumentChangeOwner::Document, 0, {}, "extensions/" + key,
                member(before, key), member(after, key));
        }
    }
}

//======================================================================================================================
std::vector<std::byte> channelBytes(const DocChannel& channel) {
    std::vector<std::byte> bytes;
    const int width = channel.path == DocChannelPath::Rotation ? 4 : 3;
    bytes.reserve(channel.values.size() * static_cast<size_t>(width) * sizeof(float));
    for (const auto& sample : channel.values) {
        for (int component = 0; component < width; ++component) {
            const uint32_t bits = std::bit_cast<uint32_t>(sample[component]);
            for (uint32_t shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::byte>((bits >> shift) & 255));
        }
    }
    return bytes;
}

//======================================================================================================================
std::string samplesJson(const DocChannel* channel) {
    if (!channel)
        return {};
    JsonWriter writer;
    writer.beginArray(true);
    for (const auto& sample : channel->values) {
        writer.beginArray(true);
        for (int i = 0; i < (channel->path == DocChannelPath::Rotation ? 4 : 3); ++i)
            writer.number(sample[i]);
        writer.endArray();
    }
    writer.endArray();
    auto text = writer.take();
    text.pop_back();
    return text;
}

//======================================================================================================================
void compareSamples(std::vector<DocumentChange>& rows, const SceneDocument& before,
                    const SceneDocument& after) {
    const size_t count = std::max(before.animations.size(), after.animations.size());
    for (size_t i = 0; i < count; ++i) {
        const DocAnimation* oldAnimation =
            i < before.animations.size() ? &before.animations[i] : nullptr;
        const DocAnimation* newAnimation =
            i < after.animations.size() ? &after.animations[i] : nullptr;
        const size_t channels = std::max(oldAnimation ? oldAnimation->channels.size() : 0,
                                         newAnimation ? newAnimation->channels.size() : 0);
        for (size_t channelIndex = 0; channelIndex < channels; ++channelIndex) {
            const DocChannel* oldChannel =
                oldAnimation && channelIndex < oldAnimation->channels.size()
                    ? &oldAnimation->channels[channelIndex]
                    : nullptr;
            const DocChannel* newChannel =
                newAnimation && channelIndex < newAnimation->channels.size()
                    ? &newAnimation->channels[channelIndex]
                    : nullptr;
            if (oldChannel && newChannel && channelBytes(*oldChannel) == channelBytes(*newChannel))
                continue;
            rows.push_back({DocumentChangeOwner::Animation, static_cast<uint32_t>(i),
                            newAnimation ? newAnimation->name : oldAnimation->name,
                            "channels/" + std::to_string(channelIndex) + "/samples",
                            samplesJson(oldChannel), samplesJson(newChannel)});
        }
    }
}

} // namespace

//======================================================================================================================
std::vector<DocumentChange> diffSceneDocuments(const SceneDocument& before,
                                               const SceneDocument& after) {
    const auto oldJson = JsonTokens::parse(sceneDocumentJson(before, "scene.bin"));
    const auto newJson = JsonTokens::parse(sceneDocumentJson(after, "scene.bin"));
    LMX_ASSERT(oldJson && newJson, "canonical scene JSON must parse");
    const Node oldRoot = oldJson->root();
    const Node newRoot = newJson->root();
    std::vector<DocumentChange> rows;
    const auto oldScene = element(member(oldRoot, "scenes"), 0);
    const auto newScene = element(member(newRoot, "scenes"), 0);
    const auto name = ownerName(oldScene, newScene);
    for (const auto& key : keys(oldRoot, newRoot)) {
        const auto oldValue = member(oldRoot, key);
        const auto newValue = member(newRoot, key);
        if (key == "nodes")
            compareNodes(rows, oldValue, newValue);
        else if (key == "cameras")
            compareIndexed(rows, DocumentChangeOwner::Camera, oldValue, newValue);
        else if (key == "animations")
            compareIndexed(rows, DocumentChangeOwner::Animation, oldValue, newValue);
        else if (key == "extensions")
            compareExtensions(rows, oldValue, newValue);
        else
            add(rows, DocumentChangeOwner::Document, 0, name, key, oldValue, newValue);
    }
    compareSamples(rows, before, after);
    std::sort(rows.begin(), rows.end(), [](const DocumentChange& a, const DocumentChange& b) {
        return std::tie(a.owner, a.index, a.property) < std::tie(b.owner, b.index, b.property);
    });
    return rows;
}

} // namespace lmx::asset
