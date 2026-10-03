//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentWrite.cpp
/// @brief Writes canonical glTF scene documents and deterministic animation data.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/IO/JsonWriter.h"
#include "Engine/Asset/Document/DocumentUri.h"
#include "Engine/Asset/Document/SceneDocumentWriteInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace lmx::asset {
namespace {

//======================================================================================================================
template <typename T>
bool sameBits(const T& value, const T& defaultValue, int count) {
    for (int i = 0; i < count; ++i)
        if (std::bit_cast<uint32_t>(value[i]) != std::bit_cast<uint32_t>(defaultValue[i]))
            return false;
    return true;
}

//======================================================================================================================
template <typename T>
void scalar(JsonWriter& writer, std::string_view key, const T& value) {
    writer.key(key);
    if constexpr (std::is_same_v<T, bool>)
        writer.boolean(value);
    else if constexpr (std::is_integral_v<T>)
        writer.integer(value);
    else if constexpr (std::is_floating_point_v<T>)
        writer.number(value);
    else
        writer.string(value);
}

//======================================================================================================================
template <typename Values>
void vector(JsonWriter& writer, std::string_view key, const Values& values, size_t count) {
    writer.key(key);
    writer.beginArray(true);
    for (size_t i = 0; i < count; ++i) {
        if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(values[i])>>)
            writer.integer(values[i]);
        else
            writer.number(values[i]);
    }
    writer.endArray();
}

//======================================================================================================================
void lookJson(JsonWriter& w, const SceneLook& look) {
    w.key("look");
    w.beginObject();
    w.key("exposure");
    w.beginObject();
    const auto& e = look.exposure;
    scalar(w, "ev", e.ev);
    scalar(w, "autoEnabled", e.autoEnabled);
    scalar(w, "lowPercentile", e.lowPercentile);
    scalar(w, "highPercentile", e.highPercentile);
    scalar(w, "targetGrey", e.targetGrey);
    scalar(w, "evMin", e.evMin);
    scalar(w, "evMax", e.evMax);
    scalar(w, "compensationEv", e.compensationEv);
    scalar(w, "adaptUpStopsPerSecond", e.adaptUpStopsPerSecond);
    scalar(w, "adaptDownStopsPerSecond", e.adaptDownStopsPerSecond);
    w.endObject();
    w.key("bloom");
    w.beginObject();
    scalar(w, "enabled", look.bloom.enabled);
    scalar(w, "threshold", look.bloom.threshold);
    scalar(w, "intensity", look.bloom.intensity);
    w.endObject();
    w.key("shadow");
    w.beginObject();
    scalar(w, "filter", look.shadowFilter == ShadowFilter::PCF ? "pcf" : "pcss");
    w.endObject();
    w.key("environment");
    w.beginObject();
    vector(w, "skySrgb8", look.environment.skySrgb8, 3);
    if (look.environment.hdri) {
        const auto& h = *look.environment.hdri;
        w.key("hdri");
        w.beginObject();
        scalar(w, "uri", detail::encodeDocumentUri(h.uri));
        scalar(w, "sha256", h.sha256);
        scalar(w, "yaw", h.yaw);
        scalar(w, "scale", h.scale);
        scalar(w, "faceSize", h.faceSize);
        scalar(w, "diffuseFaceSize", h.diffuseFaceSize);
        w.endObject();
    }
    w.endObject();
    w.endObject();
}

//======================================================================================================================
void nodeJson(JsonWriter& w, const DocNode& node, uint32_t schemaVersion) {
    w.beginObject();
    scalar(w, "name", node.name);
    if (!node.children.empty())
        vector(w, "children", node.children, node.children.size());
    if (!sameBits(node.translation, glm::vec3(0.0f), 3))
        vector(w, "translation", node.translation, 3);
    if (!sameBits(node.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 4))
        vector(w, "rotation", node.rotation, 4);
    if (!sameBits(node.scale, glm::vec3(1.0f), 3))
        vector(w, "scale", node.scale, 3);
    if (node.camera)
        scalar(w, "camera", *node.camera);
    if (node.mesh)
        scalar(w, "mesh", *node.mesh);
    w.key("extensions");
    w.beginObject();
    if (node.light) {
        w.key("KHR_lights_punctual");
        w.beginObject();
        scalar(w, "light", *node.light);
        w.endObject();
    }
    w.key("LMX_scene");
    w.beginObject();
    scalar(w, "enabled", node.enabled);
    if (schemaVersion == 2 && node.mobility == DocMobility::Movable)
        scalar(w, "mobility", "movable");
    if (node.motion == DocMotion::Invalid)
        scalar(w, "motion", "invalid");
    if (node.asset) {
        w.key("asset");
        w.beginObject();
        scalar(w, "uri", detail::encodeDocumentUri(node.asset->uri));
        scalar(w, "sha256", node.asset->sha256);
        w.endObject();
    }
    if (!node.overrides.empty()) {
        w.key("overrides");
        w.beginArray();
        for (const auto& override : node.overrides) {
            w.beginObject();
            scalar(w, "node", override.node);
            scalar(w, "name", override.name);
            if (override.enabled)
                scalar(w, "enabled", *override.enabled);
            if (schemaVersion == 2 && override.mobility)
                scalar(w, "mobility",
                       *override.mobility == DocMobility::Movable ? "movable" : "static");
            if (override.pose) {
                w.key("pose");
                w.beginObject();
                vector(w, "translation", override.pose->translation, 3);
                vector(w, "eulerDegrees", override.pose->eulerDegrees, 3);
                vector(w, "scale", override.pose->scale, 3);
                w.endObject();
            }
            w.endObject();
        }
        w.endArray();
    }
    if (node.generator) {
        w.key("generator");
        w.beginObject();
        scalar(w, "name", node.generator->name);
        w.key("params");
        w.beginObject();
        for (const auto& [name, value] : node.generator->params)
            scalar(w, name, value);
        w.endObject();
        w.endObject();
    }
    if (node.role) {
        scalar(w, "role", *node.role);
        scalar(w, "castsShadow", node.castsShadow);
    }
    w.endObject();
    w.endObject();
    w.endObject();
}

//======================================================================================================================
const char* channelName(DocChannelPath path) {
    switch (path) {
    case DocChannelPath::Translation:
        return "translation";
    case DocChannelPath::Rotation:
        return "rotation";
    case DocChannelPath::Scale:
        return "scale";
    case DocChannelPath::EmissiveStrength:
        return "pointer";
    }
    return "";
}

struct AccessorLayout {
    uint64_t offset;
    uint32_t count;
    uint32_t components;
    float lastTime;
    bool time = false;
};

//======================================================================================================================
std::vector<AccessorLayout> accessorLayout(const SceneDocument& doc) {
    std::vector<AccessorLayout> result;
    uint64_t offset = 0;
    for (const auto& animation : doc.animations) {
        result.push_back({offset, animation.keyCount, 1,
                          float(double(animation.keyCount - 1) / animation.sampleRate), true});
        offset += uint64_t(animation.keyCount) * sizeof(float);
        for (const auto& channel : animation.channels) {
            const uint32_t width = channel.path == DocChannelPath::EmissiveStrength ? 1
                                   : channel.path == DocChannelPath::Rotation       ? 4
                                                                                    : 3;
            result.push_back({offset, animation.keyCount, width, 0.0f});
            offset += uint64_t(animation.keyCount) * width * sizeof(float);
        }
    }
    return result;
}

//======================================================================================================================
void animationsJson(JsonWriter& w, const SceneDocument& doc) {
    w.key("animations");
    w.beginArray();
    uint32_t accessor = 0;
    for (const auto& a : doc.animations) {
        w.beginObject();
        scalar(w, "name", a.name);
        w.key("channels");
        w.beginArray();
        for (size_t i = 0; i < a.channels.size(); ++i) {
            const auto& c = a.channels[i];
            w.beginObject();
            scalar(w, "sampler", i);
            w.key("target");
            w.beginObject();
            if (c.path != DocChannelPath::EmissiveStrength)
                scalar(w, "node", c.node);
            scalar(w, "path", channelName(c.path));
            if (c.path == DocChannelPath::EmissiveStrength) {
                w.key("extensions");
                w.beginObject();
                w.key("KHR_animation_pointer");
                w.beginObject();
                scalar(w, "pointer",
                       "/materials/" + std::to_string(*c.material) +
                           "/extensions/KHR_materials_emissive_strength/emissiveStrength");
                w.endObject();
                w.endObject();
            }
            w.endObject();
            w.endObject();
        }
        w.endArray();
        w.key("samplers");
        w.beginArray();
        for (size_t i = 0; i < a.channels.size(); ++i) {
            w.beginObject();
            scalar(w, "input", accessor);
            scalar(w, "output", accessor + i + 1);
            scalar(w, "interpolation", a.channels[i].step ? "STEP" : "LINEAR");
            w.endObject();
        }
        w.endArray();
        w.key("extensions");
        w.beginObject();
        w.key("LMX_scene");
        w.beginObject();
        scalar(w, "sampleRate", a.sampleRate);
        w.endObject();
        w.endObject();
        w.endObject();
        accessor += uint32_t(a.channels.size()) + 1;
    }
    w.endArray();
}

//======================================================================================================================
void buffersJson(JsonWriter& w, const SceneDocument& doc, std::string_view bufferUri) {
    const auto layout = accessorLayout(doc);
    w.key("buffers");
    w.beginArray();
    if (!layout.empty()) {
        w.beginObject();
        scalar(w, "uri", detail::encodeDocumentUri(bufferUri));
        const auto& last = layout.back();
        scalar(w, "byteLength",
               last.offset + uint64_t(last.count) * last.components * sizeof(float));
        w.endObject();
    }
    if (doc.content) {
        w.beginObject();
        scalar(w, "uri", detail::encodeDocumentUri(detail::geometryUri(bufferUri)));
        scalar(w, "byteLength", sceneDocumentGeometry(doc).size());
        w.endObject();
    }
    w.endArray();
    w.key("bufferViews");
    w.beginArray();
    for (const auto& a : layout) {
        w.beginObject();
        scalar(w, "buffer", 0);
        scalar(w, "byteOffset", a.offset);
        scalar(w, "byteLength", uint64_t(a.count) * a.components * sizeof(float));
        w.endObject();
    }
    detail::geometryViews(w, doc);
    w.endArray();
    w.key("accessors");
    w.beginArray();
    for (size_t i = 0; i < layout.size(); ++i) {
        const auto& a = layout[i];
        w.beginObject();
        scalar(w, "bufferView", i);
        scalar(w, "componentType", 5126);
        scalar(w, "count", a.count);
        scalar(w, "type", a.components == 1 ? "SCALAR" : a.components == 3 ? "VEC3" : "VEC4");
        if (a.time) {
            vector(w, "min", std::array<float, 1>{0.0f}, 1);
            vector(w, "max", std::array<float, 1>{a.lastTime}, 1);
        }
        w.endObject();
    }
    detail::geometryAccessors(w, doc, layout.size());
    w.endArray();
}

//======================================================================================================================
AssetResult<void> finiteModel(const SceneDocument& doc) {
    const auto bad = [](std::string path) -> AssetResult<void> {
        return std::unexpected(AssetError{AssetErrorCode::Malformed,
                                          "JSON pointer '" + path + "': non-finite number"});
    };
    const auto& e = doc.look.exposure;
    if (doc.look.shadowFilter != ShadowFilter::PCF && doc.look.shadowFilter != ShadowFilter::PCSS)
        return std::unexpected(AssetError{
            AssetErrorCode::Malformed, "/extensions/LMX_scene/look/shadow/filter: invalid enum"});
    const float look[] = {e.ev,
                          e.lowPercentile,
                          e.highPercentile,
                          e.targetGrey,
                          e.evMin,
                          e.evMax,
                          e.compensationEv,
                          e.adaptUpStopsPerSecond,
                          e.adaptDownStopsPerSecond,
                          doc.look.bloom.threshold,
                          doc.look.bloom.intensity};
    for (float value : look)
        if (!std::isfinite(value))
            return bad("/extensions/LMX_scene/look");
    if (doc.look.environment.hdri && (!std::isfinite(doc.look.environment.hdri->yaw) ||
                                      !std::isfinite(doc.look.environment.hdri->scale)))
        return bad("/extensions/LMX_scene/look/environment/hdri");
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
        const auto& n = doc.nodes[i];
        const auto path = "/nodes/" + std::to_string(i);
        for (int k = 0; k < 4; ++k) {
            if (!std::isfinite(n.rotation[k]))
                return bad(path + "/rotation");
            if (k < 3 && (!std::isfinite(n.translation[k]) || !std::isfinite(n.scale[k])))
                return bad(path);
        }
        if (n.generator)
            for (const auto& [name, value] : n.generator->params)
                if (!std::isfinite(value))
                    return bad(path + "/extensions/LMX_scene/generator/params/" + name);
        if (n.mobility != DocMobility::Static && n.mobility != DocMobility::Movable)
            return bad(path + "/extensions/LMX_scene/mobility");
        if (n.mobility == DocMobility::Movable &&
            (n.camera || n.generator || (!n.mesh && !n.asset && !n.light)))
            return std::unexpected(AssetError{AssetErrorCode::Malformed,
                                              path + "/extensions/LMX_scene/mobility: mobility "
                                                     "requires an object, asset or light node"});
        for (size_t o = 0; o < n.overrides.size(); ++o)
            if (n.overrides[o].mobility && *n.overrides[o].mobility != DocMobility::Static &&
                *n.overrides[o].mobility != DocMobility::Movable)
                return bad(path + "/extensions/LMX_scene/overrides/" + std::to_string(o) +
                           "/mobility");
        for (const auto& o : n.overrides)
            if (o.pose)
                for (int k = 0; k < 3; ++k)
                    if (!std::isfinite(o.pose->translation[k]) ||
                        !std::isfinite(o.pose->eulerDegrees[k]) || !std::isfinite(o.pose->scale[k]))
                        return bad(path + "/extensions/LMX_scene/overrides");
    }
    for (size_t i = 0; i < doc.cameras.size(); ++i) {
        const auto& c = doc.cameras[i];
        if (!std::isfinite(c.fovY) || !std::isfinite(c.nearZ) ||
            (c.farZ && !std::isfinite(*c.farZ)) ||
            (c.aspectRatio && !std::isfinite(*c.aspectRatio)))
            return bad("/cameras/" + std::to_string(i));
    }
    for (size_t i = 0; i < doc.lights.size(); ++i) {
        const auto& l = doc.lights[i];
        const auto path = "/extensions/KHR_lights_punctual/lights/" + std::to_string(i);
        if (l.type != DocLightType::Directional && l.type != DocLightType::Point &&
            l.type != DocLightType::Spot)
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed, path + "/type: invalid enum"});
        if (l.type != DocLightType::Directional && !l.range)
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed,
                           path + "/range: model contains an unbounded local light"});
        if (!std::isfinite(l.intensity) || !std::isfinite(l.innerCone) ||
            !std::isfinite(l.outerCone) || (l.range && !std::isfinite(*l.range)))
            return bad("/extensions/KHR_lights_punctual/lights/" + std::to_string(i));
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(l.colour[k]))
                return bad("/extensions/KHR_lights_punctual/lights/" + std::to_string(i) +
                           "/color");
    }
    for (size_t i = 0; i < doc.animations.size(); ++i) {
        const auto& a = doc.animations[i];
        if (!std::isfinite(a.sampleRate) || a.sampleRate <= 0.0 || a.keyCount == 0 ||
            a.channels.empty() || !std::isfinite(float(double(a.keyCount - 1) / a.sampleRate)))
            return std::unexpected(
                AssetError{AssetErrorCode::Malformed, "/animations/" + std::to_string(i) +
                                                          ": invalid rate, key count or channels"});
        for (const auto& c : a.channels) {
            const auto target = "/animations/" + std::to_string(i) + "/channels/" +
                                std::to_string(&c - a.channels.data()) + "/target";
            if (c.path == DocChannelPath::EmissiveStrength) {
                if (!c.material || *c.material >= doc.materials.size())
                    return std::unexpected(
                        AssetError{AssetErrorCode::Malformed,
                                   target + "/extensions/KHR_animation_pointer/pointer: material "
                                            "index is out of range"});
                if (!c.step || a.sampleRate != 60.0)
                    return std::unexpected(
                        AssetError{AssetErrorCode::Malformed,
                                   target + ": emissive channels require STEP at 60 Hz"});
                for (const auto& value : c.values)
                    if (value.x < 0.0f)
                        return std::unexpected(
                            AssetError{AssetErrorCode::Malformed,
                                       target + ": emissive strength must be nonnegative"});
            } else if (c.material)
                return std::unexpected(
                    AssetError{AssetErrorCode::Malformed,
                               target + ": material is only valid for emissive strength"});
            if (c.values.size() != a.keyCount)
                return std::unexpected(
                    AssetError{AssetErrorCode::Malformed, "/animations/" + std::to_string(i) +
                                                              ": channel key count mismatch"});
            for (const auto& v : c.values)
                for (int k = 0; k < 4; ++k)
                    if (!std::isfinite(v[k]))
                        return bad("/animations/" + std::to_string(i));
        }
    }
    return {};
}

} // namespace

//======================================================================================================================
std::string sceneDocumentJson(const SceneDocument& doc, std::string_view bufferUri) {
    JsonWriter w;
    w.beginObject();
    w.key("asset");
    w.beginObject();
    scalar(w, "version", "2.0");
    w.endObject();
    w.key("extensionsUsed");
    w.beginArray(true);
    w.string("LMX_scene");
    if (!doc.lights.empty())
        w.string("KHR_lights_punctual");
    if (!doc.materials.empty())
        w.string("KHR_materials_emissive_strength");
    if (std::ranges::any_of(doc.animations, [](const DocAnimation& animation) {
            return std::ranges::any_of(animation.channels, [](const DocChannel& channel) {
                return channel.path == DocChannelPath::EmissiveStrength;
            });
        }))
        w.string("KHR_animation_pointer");
    w.endArray();
    scalar(w, "scene", 0);
    w.key("scenes");
    w.beginArray();
    w.beginObject();
    scalar(w, "name", doc.name);
    vector(w, "nodes", doc.rootNodes, doc.rootNodes.size());
    w.endObject();
    w.endArray();
    w.key("nodes");
    w.beginArray();
    for (const auto& node : doc.nodes)
        nodeJson(w, node, doc.schemaVersion);
    w.endArray();
    w.key("cameras");
    w.beginArray();
    for (const auto& camera : doc.cameras) {
        w.beginObject();
        scalar(w, "name", camera.name);
        scalar(w, "type", "perspective");
        w.key("perspective");
        w.beginObject();
        scalar(w, "yfov", camera.fovY);
        scalar(w, "znear", camera.nearZ);
        if (camera.farZ)
            scalar(w, "zfar", *camera.farZ);
        if (camera.aspectRatio)
            scalar(w, "aspectRatio", *camera.aspectRatio);
        w.endObject();
        w.endObject();
    }
    w.endArray();
    if (!doc.animations.empty())
        animationsJson(w, doc);
    if (!doc.animations.empty() || doc.content)
        buffersJson(w, doc, bufferUri);
    detail::contentJson(w, doc, bufferUri, accessorLayout(doc).size());
    w.key("extensions");
    w.beginObject();
    if (!doc.lights.empty()) {
        w.key("KHR_lights_punctual");
        w.beginObject();
        w.key("lights");
        w.beginArray();
        for (const auto& light : doc.lights) {
            w.beginObject();
            scalar(w, "name", light.name);
            scalar(w, "type",
                   light.type == DocLightType::Directional ? "directional"
                   : light.type == DocLightType::Point     ? "point"
                                                           : "spot");
            vector(w, "color", light.colour, 3);
            scalar(w, "intensity", light.intensity);
            if (light.range)
                scalar(w, "range", *light.range);
            if (light.type == DocLightType::Spot) {
                w.key("spot");
                w.beginObject();
                scalar(w, "innerConeAngle", light.innerCone);
                scalar(w, "outerConeAngle", light.outerCone);
                w.endObject();
            }
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.key("LMX_scene");
    w.beginObject();
    scalar(w, "schemaVersion", doc.schemaVersion);
    scalar(w, "camera", doc.camera);
    lookJson(w, doc.look);
    scalar(w, "loop", doc.loop);
    detail::contentExtensionJson(w, doc, bufferUri);
    w.endObject();
    w.endObject();
    w.endObject();
    return w.take();
}

//======================================================================================================================
AssetResult<void> validateSceneDocumentModel(const SceneDocument& doc, ContentBytes bytes) {
    if (auto valid = finiteModel(doc); !valid)
        return valid;
    return detail::validateDocumentContent(doc, bytes);
}

//======================================================================================================================
std::vector<std::byte> sceneDocumentBuffer(const SceneDocument& doc) {
    std::vector<std::byte> bytes;
    const auto append = [&](float value) {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        for (uint32_t shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((bits >> shift) & 255));
    };
    for (const auto& animation : doc.animations) {
        for (uint32_t k = 0; k < animation.keyCount; ++k)
            append(float(double(k) / animation.sampleRate));
        for (const auto& channel : animation.channels)
            for (const auto& value : channel.values)
                for (int component = 0;
                     component < (channel.path == DocChannelPath::EmissiveStrength ? 1
                                  : channel.path == DocChannelPath::Rotation       ? 4
                                                                                   : 3);
                     ++component)
                    append(value[component]);
    }
    return bytes;
}

} // namespace lmx::asset
