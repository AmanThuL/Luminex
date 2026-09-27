//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentWrite.cpp
/// @brief Writes canonical glTF animation documents and rolls back failed two-file saves.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/IO/JsonWriter.h"
#include "Engine/Asset/Document/DocumentUri.h"
#include "Engine/Asset/Document/SceneDocumentSaveInternal.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

#include <unistd.h>

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
void nodeJson(JsonWriter& w, const DocNode& node) {
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
    }
    return "";
}

struct AccessorLayout {
    uint64_t offset;
    uint32_t count;
    uint32_t components;
    float lastTime;
};

//======================================================================================================================
std::vector<AccessorLayout> accessorLayout(const SceneDocument& doc) {
    std::vector<AccessorLayout> result;
    uint64_t offset = 0;
    for (const auto& animation : doc.animations) {
        result.push_back({offset, animation.keyCount, 1,
                          float(double(animation.keyCount - 1) / animation.sampleRate)});
        offset += uint64_t(animation.keyCount) * sizeof(float);
        for (const auto& channel : animation.channels) {
            const uint32_t width = channel.path == DocChannelPath::Rotation ? 4 : 3;
            result.push_back({offset, animation.keyCount, width, 0.0f});
            offset += uint64_t(animation.keyCount) * width * sizeof(float);
        }
    }
    return result;
}

//======================================================================================================================
void animationsJson(JsonWriter& w, const SceneDocument& doc, std::string_view bufferUri) {
    const auto layout = accessorLayout(doc);
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
            scalar(w, "node", c.node);
            scalar(w, "path", channelName(c.path));
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
    w.key("buffers");
    w.beginArray();
    w.beginObject();
    scalar(w, "uri", detail::encodeDocumentUri(bufferUri));
    const auto& last = layout.back();
    scalar(w, "byteLength", last.offset + uint64_t(last.count) * last.components * sizeof(float));
    w.endObject();
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
        if (a.components == 1) {
            vector(w, "min", std::array<float, 1>{0.0f}, 1);
            vector(w, "max", std::array<float, 1>{a.lastTime}, 1);
        }
        w.endObject();
    }
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

//======================================================================================================================
AssetError ioError(const std::filesystem::path& path, std::string_view message) {
    return {AssetErrorCode::Io, path.string() + ": " + std::string(message)};
}

//======================================================================================================================
AssetResult<void> writableTarget(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory)
        return {};
    if (ec)
        return std::unexpected(ioError(path, ec.message()));
    if (status.type() == std::filesystem::file_type::not_found)
        return {};
    if (!std::filesystem::is_regular_file(status))
        return std::unexpected(ioError(path, "target is not a regular file"));
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    if ((status.permissions() & writeBits) == std::filesystem::perms::none ||
        access(path.c_str(), W_OK) != 0)
        return std::unexpected(ioError(path, "target is read-only"));
    return {};
}

//======================================================================================================================
AssetResult<void> writeBytes(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return std::unexpected(ioError(path, "cannot open staged output"));
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.flush();
    const bool good = file.good();
    file.close();
    if (!good || file.fail())
        return std::unexpected(ioError(path, "cannot write staged output"));
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
        nodeJson(w, node);
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
        animationsJson(w, doc, bufferUri);
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
    w.endObject();
    w.endObject();
    w.endObject();
    return w.take();
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
                     component < (channel.path == DocChannelPath::Rotation ? 4 : 3); ++component)
                    append(value[component]);
    }
    return bytes;
}

//======================================================================================================================
AssetResult<void> detail::saveSceneDocumentWithRename(const SceneDocument& doc,
                                                      const std::filesystem::path& path,
                                                      const DocumentRename& rename) {
    if (auto valid = finiteModel(doc); !valid)
        return valid;
    if (path.extension() != ".gltf")
        return std::unexpected(ioError(path, "scene document must use the .gltf extension"));
    auto binPath = path;
    binPath.replace_extension(".bin");
    for (const auto& target : {path, binPath})
        if (auto writable = writableTarget(target); !writable)
            return writable;
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::error_code ec;
    const auto permissions = std::filesystem::status(parent, ec).permissions();
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    if (ec || (permissions & writeBits) == std::filesystem::perms::none ||
        access(parent.c_str(), W_OK) != 0)
        return std::unexpected(ioError(parent, "output directory is not writable"));
    static std::atomic<uint64_t> sequence{0};
    const auto unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(sequence++);
    const auto staging = parent / (".lmx-save-" + unique + ".tmp");
    if (!std::filesystem::create_directory(staging, ec))
        return std::unexpected(ioError(staging, ec.message()));
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const auto json = sceneDocumentJson(doc, binPath.filename().string());
    const auto bin = sceneDocumentBuffer(doc);
    auto written = writeBytes(staging / path.filename(), json.data(), json.size());
    if (written)
        written = writeBytes(staging / binPath.filename(), bin.data(), bin.size());
    if (!written) {
        cleanup();
        return written;
    }
    const auto checked = readSceneDocument(staging / path.filename());
    if (!checked) {
        cleanup();
        return std::unexpected(checked.error());
    }
    const std::array<std::filesystem::path, 2> targets{path, binPath};
    std::array<bool, 2> backedUp{};
    std::array<bool, 2> installed{};
    const auto rollback = [&](AssetError error) -> AssetResult<void> {
        bool restored = true;
        for (size_t i = 0; i < targets.size(); ++i) {
            std::error_code recovery;
            if (installed[i]) {
                std::filesystem::remove(targets[i], recovery);
                if (recovery) {
                    restored = false;
                    error.message += "; rollback remove: " + recovery.message();
                }
            }
            if (backedUp[i]) {
                rename(staging / (std::to_string(i) + ".bak"), targets[i], recovery);
                if (recovery) {
                    restored = false;
                    error.message += "; rollback restore: " + recovery.message();
                }
            }
        }
        if (restored)
            cleanup();
        else
            error.message += "; preserved backups at " + staging.string();
        return std::unexpected(std::move(error));
    };
    for (size_t i = 0; i < targets.size(); ++i) {
        const bool exists = std::filesystem::exists(targets[i], ec);
        if (ec)
            return rollback(ioError(targets[i], ec.message()));
        if (exists) {
            rename(targets[i], staging / (std::to_string(i) + ".bak"), ec);
            if (ec)
                return rollback(ioError(targets[i], ec.message()));
            backedUp[i] = true;
        }
    }
    for (size_t i = 0; i < targets.size(); ++i) {
        rename(staging / targets[i].filename(), targets[i], ec);
        if (ec)
            return rollback(ioError(targets[i], ec.message()));
        installed[i] = true;
    }
    cleanup();
    return {};
}

//======================================================================================================================
AssetResult<void> saveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path) {
    return detail::saveSceneDocumentWithRename(
        doc, path,
        [](const std::filesystem::path& from, const std::filesystem::path& to,
           std::error_code& error) { std::filesystem::rename(from, to, error); });
}

} // namespace lmx::asset
