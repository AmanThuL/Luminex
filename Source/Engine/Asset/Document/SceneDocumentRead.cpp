//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentRead.cpp
/// @brief Reads validated CPU scene documents, uniform glTF animation buffers and disk hashes.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/Diagnostics/Log.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/DocumentUri.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <span>
#include <utility>

namespace lmx::asset {
namespace {

//======================================================================================================================
AssetError malformed(std::string_view path, std::string_view reason) {
    return {AssetErrorCode::Malformed,
            "JSON pointer '" + std::string(path) + "': " + std::string(reason)};
}

//======================================================================================================================
AssetResult<std::vector<std::byte>> fileBytes(const std::filesystem::path& path,
                                              std::string_view pointer) {
    const auto result = readWholeFile(path);
    if (!result)
        return std::unexpected(
            AssetError{AssetErrorCode::Io, "JSON pointer '" + std::string(pointer) +
                                               "': cannot read '" + path.string() + "'"});
    return *result;
}

class Reader {
public:
    //==================================================================================================================
    explicit Reader(JsonNode root, std::filesystem::path path);

    AssetResult<SceneDocument> document();

    AssetResult<std::vector<std::byte>> buffer();

private:
    //==================================================================================================================
    void fail(std::string_view path, std::string_view reason);

    //==================================================================================================================
    void object(const JsonNode& node);

    //==================================================================================================================
    size_t array(const JsonNode& node);

    //==================================================================================================================
    JsonNode required(const JsonNode& node, std::string_view key);

    //==================================================================================================================
    std::optional<JsonNode> optional(const JsonNode& node, std::string_view key);

    //==================================================================================================================
    JsonNode extension(const JsonNode& node, std::string_view name);

    //==================================================================================================================
    template <typename T>
    T number(const JsonNode& node);

    //==================================================================================================================
    uint32_t integer(const JsonNode& node);

    //==================================================================================================================
    uint64_t wideInteger(const JsonNode& node);

    //==================================================================================================================
    std::string string(const JsonNode& node);

    //==================================================================================================================
    bool boolean(const JsonNode& node);

    //==================================================================================================================
    template <typename T, glm::length_t N>
    glm::vec<N, T> vector(const JsonNode& node);

    //==================================================================================================================
    std::vector<uint32_t> indices(const JsonNode& node);

    //==================================================================================================================
    void uniqueKeys(const JsonNode& node, size_t depth = 0);

    //==================================================================================================================
    void reference(const JsonNode& node, std::string& uri, std::string& hash);

    //==================================================================================================================
    SceneLook look(const JsonNode& node);

    //==================================================================================================================
    DocCamera camera(const JsonNode& node);

    //==================================================================================================================
    DocLight light(const JsonNode& node);

    //==================================================================================================================
    DocNode node(const JsonNode& value);

    //==================================================================================================================
    void hierarchy(SceneDocument& doc);

    //==================================================================================================================
    std::vector<glm::vec4> accessor(const JsonNode& node, uint32_t components,
                                    const std::vector<std::byte>& bytes);

    //==================================================================================================================
    void animations(SceneDocument& doc, const std::vector<std::byte>& bytes);

    JsonNode m_root;
    std::filesystem::path m_path;
    std::optional<AssetError> m_error;
    std::optional<std::string> m_bufferUri;
    std::vector<std::optional<uint32_t>> m_lightMap;
    std::set<uint32_t> m_placementAncestors;
};

//======================================================================================================================
Reader::Reader(JsonNode root, std::filesystem::path path)
    : m_root(std::move(root)), m_path(std::move(path)) {}

//======================================================================================================================
void Reader::fail(std::string_view path, std::string_view reason) {
    if (!m_error)
        m_error = malformed(path, reason);
}

//======================================================================================================================
void Reader::object(const JsonNode& node) {
    if (!node.isObject())
        fail(node.path(), "expected an object");
}

//======================================================================================================================
size_t Reader::array(const JsonNode& node) {
    if (!node.isArray()) {
        fail(node.path(), "expected an array");
        return 0;
    }
    return node.size();
}

//======================================================================================================================
JsonNode Reader::required(const JsonNode& node, std::string_view key) {
    object(node);
    if (node.isObject()) {
        if (auto value = node.find(key))
            return *value;
        fail(node.path() + '/' + std::string(key), "required field is missing");
    }
    return node;
}

//======================================================================================================================
std::optional<JsonNode> Reader::optional(const JsonNode& node, std::string_view key) {
    object(node);
    return node.isObject() ? node.find(key) : std::nullopt;
}

//======================================================================================================================
JsonNode Reader::extension(const JsonNode& node, std::string_view name) {
    return required(required(node, "extensions"), name);
}

//======================================================================================================================
template <typename T>
T Reader::number(const JsonNode& node) {
    const auto value = [&] {
        if constexpr (std::is_same_v<T, float>)
            return node.asFloat();
        else
            return node.asDouble();
    }();
    if (!value) {
        fail(node.path(), "expected a finite representable number");
        return T{};
    }
    return *value;
}

//======================================================================================================================
uint64_t Reader::wideInteger(const JsonNode& node) {
    const auto value = node.asUInt();
    if (!value) {
        fail(node.path(), "expected an unsigned integer");
        return 0;
    }
    return *value;
}

//======================================================================================================================
uint32_t Reader::integer(const JsonNode& node) {
    const auto value = wideInteger(node);
    if (value > std::numeric_limits<uint32_t>::max()) {
        fail(node.path(), "integer exceeds uint32 range");
        return 0;
    }
    return uint32_t(value);
}

//======================================================================================================================
std::string Reader::string(const JsonNode& node) {
    auto value = node.asString();
    if (!value) {
        fail(node.path(), "expected a string");
        return {};
    }
    return std::move(*value);
}

//======================================================================================================================
bool Reader::boolean(const JsonNode& node) {
    const auto value = node.asBool();
    if (!value) {
        fail(node.path(), "expected a boolean");
        return false;
    }
    return *value;
}

//======================================================================================================================
template <typename T, glm::length_t N>
glm::vec<N, T> Reader::vector(const JsonNode& node) {
    glm::vec<N, T> result{};
    if (array(node) != N) {
        fail(node.path(), "expected " + std::to_string(N) + " components");
        return result;
    }
    for (glm::length_t i = 0; i < N; ++i)
        result[i] = number<T>(node.at(i));
    return result;
}

//======================================================================================================================
std::vector<uint32_t> Reader::indices(const JsonNode& node) {
    std::vector<uint32_t> result;
    for (size_t i = 0, count = array(node); i < count; ++i)
        result.push_back(integer(node.at(i)));
    return result;
}

//======================================================================================================================
void Reader::uniqueKeys(const JsonNode& node, size_t depth) {
    if (depth > 128) {
        fail(node.path(), "document nesting exceeds 128 levels");
        return;
    }
    if (node.isObject()) {
        std::set<std::string> names;
        for (size_t i = 0; i < node.size() && !m_error; ++i) {
            const auto value = node.memberValue(i);
            if (!names.insert(node.memberName(i)).second)
                fail(value.path(), "duplicate object member");
            uniqueKeys(value, depth + 1);
        }
    } else if (node.isArray()) {
        for (size_t i = 0; i < node.size() && !m_error; ++i)
            uniqueKeys(node.at(i), depth + 1);
    }
}

//======================================================================================================================
void Reader::reference(const JsonNode& node, std::string& uri, std::string& hash) {
    const auto path = required(node, "uri");
    const auto decoded = detail::decodeDocumentUri(string(path), path.path());
    if (!decoded) {
        if (!m_error)
            m_error = decoded.error();
    } else {
        uri = *decoded;
        if (std::filesystem::path(uri).begin()->string() == "Assets")
            fail(path.path(), "expected a file URI relative to Assets/");
    }
    const auto sha = required(node, "sha256");
    hash = string(sha);
    if (hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        fail(sha.path(), "expected 64 lowercase hexadecimal SHA-256 digits");
}

//======================================================================================================================
SceneLook Reader::look(const JsonNode& node) {
    SceneLook result;
    const auto e = required(node, "exposure");
    auto& exposure = result.exposure;
    exposure.ev = number<float>(required(e, "ev"));
    exposure.autoEnabled = boolean(required(e, "autoEnabled"));
    exposure.lowPercentile = number<float>(required(e, "lowPercentile"));
    exposure.highPercentile = number<float>(required(e, "highPercentile"));
    exposure.targetGrey = number<float>(required(e, "targetGrey"));
    exposure.evMin = number<float>(required(e, "evMin"));
    exposure.evMax = number<float>(required(e, "evMax"));
    exposure.compensationEv = number<float>(required(e, "compensationEv"));
    exposure.adaptUpStopsPerSecond = number<float>(required(e, "adaptUpStopsPerSecond"));
    exposure.adaptDownStopsPerSecond = number<float>(required(e, "adaptDownStopsPerSecond"));
    const auto b = required(node, "bloom");
    result.bloom.enabled = boolean(required(b, "enabled"));
    result.bloom.threshold = number<float>(required(b, "threshold"));
    result.bloom.intensity = number<float>(required(b, "intensity"));
    if (const auto range = sceneLookRangeError(result))
        fail(node.path() + range->path, range->message);
    const auto filter = required(required(node, "shadow"), "filter");
    const auto filterName = string(filter);
    if (filterName == "pcf")
        result.shadowFilter = ShadowFilter::PCF;
    else if (filterName == "pcss")
        result.shadowFilter = ShadowFilter::PCSS;
    else
        fail(filter.path(), "expected pcf or pcss");
    const auto env = required(node, "environment");
    const auto sky = required(env, "skySrgb8");
    if (array(sky) != 3)
        fail(sky.path(), "expected three sRGB bytes");
    else
        for (size_t i = 0; i < 3; ++i) {
            const auto value = integer(sky.at(i));
            if (value > 255)
                fail(sky.at(i).path(), "sRGB byte exceeds 255");
            result.environment.skySrgb8[i] = uint8_t(value);
        }
    if (const auto h = optional(env, "hdri")) {
        auto& hdri = result.environment.hdri.emplace();
        reference(*h, hdri.uri, hdri.sha256);
        hdri.yaw = number<float>(required(*h, "yaw"));
        hdri.scale = number<float>(required(*h, "scale"));
        hdri.faceSize = integer(required(*h, "faceSize"));
        hdri.diffuseFaceSize = integer(required(*h, "diffuseFaceSize"));
        if (hdri.scale < 0.0f)
            fail(h->path() + "/scale", "must be nonnegative");
        if (!std::has_single_bit(hdri.faceSize))
            fail(h->path() + "/faceSize", "must be a positive power of two");
        if (!std::has_single_bit(hdri.diffuseFaceSize))
            fail(h->path() + "/diffuseFaceSize", "must be a positive power of two");
    }
    return result;
}

//======================================================================================================================
DocCamera Reader::camera(const JsonNode& node) {
    DocCamera result;
    if (auto n = optional(node, "name"))
        result.name = string(*n);
    const auto type = required(node, "type");
    if (string(type) != "perspective")
        fail(type.path(), "only perspective cameras are supported");
    const auto p = required(node, "perspective");
    result.fovY = number<float>(required(p, "yfov"));
    result.nearZ = number<float>(required(p, "znear"));
    result.farZ.reset();
    if (auto far = optional(p, "zfar"))
        result.farZ = number<float>(*far);
    if (auto aspect = optional(p, "aspectRatio"))
        result.aspectRatio = number<float>(*aspect);
    if (!(result.fovY > 0.0f && result.fovY < glm::pi<float>()))
        fail(p.path() + "/yfov", "must lie between zero and pi");
    if (!(result.nearZ > 0.0f))
        fail(p.path() + "/znear", "must be positive");
    if (result.farZ && *result.farZ <= result.nearZ)
        fail(p.path() + "/zfar", "must be greater than znear");
    if (result.aspectRatio && *result.aspectRatio <= 0.0f)
        fail(p.path() + "/aspectRatio", "must be positive");
    return result;
}

//======================================================================================================================
DocLight Reader::light(const JsonNode& node) {
    DocLight result;
    if (auto name = optional(node, "name"))
        result.name = string(*name);
    const auto t = required(node, "type");
    const auto type = string(t);
    if (type == "directional")
        result.type = DocLightType::Directional;
    else if (type == "point")
        result.type = DocLightType::Point;
    else if (type == "spot")
        result.type = DocLightType::Spot;
    else
        fail(t.path(), "expected directional, point or spot");
    if (auto colour = optional(node, "color"))
        result.colour = vector<double, 3>(*colour);
    if (auto intensity = optional(node, "intensity"))
        result.intensity = number<double>(*intensity);
    if (auto range = optional(node, "range"))
        result.range = number<float>(*range);
    for (int i = 0; i < 3; ++i)
        if (result.colour[i] < 0.0 || result.colour[i] > 1.0)
            fail(node.path() + "/color/" + std::to_string(i), "linear color must be in [0,1]");
    if (result.intensity < 0.0)
        fail(node.path() + "/intensity", "must be nonnegative");
    if (result.range && *result.range <= 0.0f)
        fail(node.path() + "/range", "must be positive");
    if (result.type == DocLightType::Spot) {
        if (auto spot = optional(node, "spot")) {
            if (auto inner = optional(*spot, "innerConeAngle"))
                result.innerCone = number<float>(*inner);
            if (auto outer = optional(*spot, "outerConeAngle"))
                result.outerCone = number<float>(*outer);
        }
        if (result.innerCone < 0.0f || result.innerCone >= result.outerCone)
            fail(node.path() + "/spot/innerConeAngle",
                 "must be nonnegative and smaller than outerConeAngle");
        if (result.outerCone > glm::half_pi<float>())
            fail(node.path() + "/spot/outerConeAngle", "must not exceed pi/2");
    }
    return result;
}

//======================================================================================================================
DocNode Reader::node(const JsonNode& value) {
    DocNode result;
    result.name = string(required(value, "name"));
    if (optional(value, "matrix"))
        fail(value.path() + "/matrix", "document nodes use TRS only");
    if (optional(value, "mesh"))
        fail(value.path() + "/mesh", "scene documents contain no meshes");
    if (auto children = optional(value, "children"))
        result.children = indices(*children);
    if (auto translation = optional(value, "translation"))
        result.translation = vector<float, 3>(*translation);
    if (auto scale = optional(value, "scale"))
        result.scale = vector<float, 3>(*scale);
    if (auto rotation = optional(value, "rotation")) {
        const auto q = vector<float, 4>(*rotation);
        result.rotation = {q.w, q.x, q.y, q.z};
        if (std::abs(glm::dot(q, q) - 1.0f) > 1e-4f)
            fail(rotation->path(), "expected a unit quaternion");
    }
    if (auto camera = optional(value, "camera"))
        result.camera = integer(*camera);
    const auto extensions = required(value, "extensions");
    if (auto light = optional(extensions, "KHR_lights_punctual")) {
        const auto index = required(*light, "light");
        const auto raw = integer(index);
        if (raw >= m_lightMap.size())
            fail(index.path(), "light index is out of range");
        else
            result.light = m_lightMap[raw];
    }
    const auto lmx = required(extensions, "LMX_scene");
    result.enabled = boolean(required(lmx, "enabled"));
    if (auto asset = optional(lmx, "asset")) {
        auto& ref = result.asset.emplace();
        reference(*asset, ref.uri, ref.sha256);
    }
    if (auto overrides = optional(lmx, "overrides")) {
        std::set<uint32_t> seen;
        for (size_t i = 0, count = array(*overrides); i < count; ++i) {
            const auto value = overrides->at(i);
            DocOverride o;
            o.node = integer(required(value, "node"));
            o.name = string(required(value, "name"));
            if (!seen.insert(o.node).second)
                fail(value.path() + "/node", "duplicate node override");
            if (auto enabled = optional(value, "enabled"))
                o.enabled = boolean(*enabled);
            if (auto pose = optional(value, "pose")) {
                o.pose = ObjectPose{vector<float, 3>(required(*pose, "translation")),
                                    vector<float, 3>(required(*pose, "eulerDegrees")),
                                    vector<float, 3>(required(*pose, "scale"))};
            }
            if (!o.enabled && !o.pose)
                fail(value.path(), "override needs enabled or pose");
            result.overrides.push_back(std::move(o));
        }
        if (!result.asset)
            fail(overrides->path(), "overrides require an asset node");
    }
    if (auto generator = optional(lmx, "generator")) {
        auto& g = result.generator.emplace();
        g.name = string(required(*generator, "name"));
        const auto params = required(*generator, "params");
        object(params);
        if (params.isObject())
            for (size_t i = 0; i < params.size(); ++i)
                g.params.emplace_back(params.memberName(i), number<double>(params.memberValue(i)));
        if (g.name.empty())
            fail(generator->path() + "/name", "generator name is empty");
        if (result.asset)
            fail(generator->path(), "asset and generator are mutually exclusive");
    }
    if (auto role = optional(lmx, "role"))
        result.role = string(*role);
    if (auto shadow = optional(lmx, "castsShadow"))
        result.castsShadow = boolean(*shadow);
    return result;
}

//======================================================================================================================
void Reader::hierarchy(SceneDocument& doc) {
    const auto count = doc.nodes.size();
    std::vector<int64_t> parent(count, -1);
    for (size_t i = 0; i < count; ++i) {
        const auto& node = doc.nodes[i];
        const auto path = "/nodes/" + std::to_string(i);
        if (node.camera && *node.camera >= doc.cameras.size())
            fail(path + "/camera", "camera index is out of range");
        for (size_t j = 0; j < node.children.size(); ++j) {
            const auto child = node.children[j];
            const auto childPath = path + "/children/" + std::to_string(j);
            if (child >= count) {
                fail(childPath, "node index is out of range");
                continue;
            }
            if (parent[child] != -1)
                fail(childPath, "node has multiple parents or a repeated child");
            parent[child] = int64_t(i);
        }
        if (node.light && doc.lights[*node.light].type == DocLightType::Directional) {
            const auto lmx = extension(required(m_root, "nodes").at(i), "LMX_scene");
            const auto role = string(required(lmx, "role"));
            boolean(required(lmx, "castsShadow"));
            if (role != "key" && role != "fill" && role != "rim")
                fail(lmx.path() + "/role", "expected key, fill or rim");
        } else if (node.role || node.castsShadow) {
            fail(path + "/extensions/LMX_scene",
                 "role and castsShadow require a directional light");
        }
        if ((node.asset || node.generator) && (node.camera || node.light))
            fail(path, "asset and generator roots cannot also be cameras or lights");
    }
    if (m_error)
        return;
    std::vector<uint8_t> state(count, 0);
    for (size_t i = 0; i < count && !m_error; ++i) {
        std::vector<uint32_t> chain;
        int64_t current = int64_t(i);
        while (current >= 0 && state[current] == 0) {
            state[current] = 1;
            chain.push_back(uint32_t(current));
            current = parent[current];
        }
        if (current >= 0 && state[current] == 1)
            fail("/nodes/" + std::to_string(current) + "/children", "hierarchy contains a cycle");
        for (const auto node : chain)
            state[node] = 2;
    }
    if (m_error)
        return;
    std::set<uint32_t> roots;
    for (size_t i = 0; i < doc.rootNodes.size(); ++i) {
        const auto root = doc.rootNodes[i];
        const auto path = "/scenes/0/nodes/" + std::to_string(i);
        if (root >= count) {
            fail(path, "node index is out of range");
            continue;
        }
        if (parent[root] != -1 || !roots.insert(root).second)
            fail(path, "scene root is repeated or has a parent");
    }
    std::set<std::string> roles;
    uint32_t shadowCount = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& node = doc.nodes[i];
        if (node.role && !roles.insert(*node.role).second)
            fail("/nodes/" + std::to_string(i) + "/extensions/LMX_scene/role",
                 "directional role is duplicated");
        shadowCount += node.castsShadow;
        if (node.camera || node.light) {
            for (int64_t ancestor = parent[i]; ancestor >= 0; ancestor = parent[ancestor]) {
                m_placementAncestors.insert(uint32_t(ancestor));
                const auto& p = doc.nodes[ancestor];
                if (p.translation != glm::vec3(0.0f) ||
                    p.rotation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f) || p.scale != glm::vec3(1.0f))
                    fail("/nodes/" + std::to_string(ancestor),
                         "ancestors of lights and cameras must have identity transforms");
            }
        }
        if (parent[i] == -1 && !roots.contains(uint32_t(i)))
            fail("/nodes/" + std::to_string(i), "node is not reachable from the scene roots");
    }
    if (shadowCount > 1)
        fail("/nodes", "only one directional light may cast shadows");
    if (doc.camera >= count || !doc.nodes[doc.camera].camera)
        fail("/extensions/LMX_scene/camera", "must reference a camera node");
}

//======================================================================================================================
AssetResult<std::vector<std::byte>> Reader::buffer() {
    const auto buffers = optional(m_root, "buffers");
    if (!buffers) {
        if (m_error)
            return std::unexpected(*m_error);
        return std::vector<std::byte>{};
    }
    if (array(*buffers) != 1)
        fail(buffers->path(), "expected exactly one external animation buffer");
    if (m_error)
        return std::unexpected(*m_error);
    const auto b = buffers->at(0);
    const auto uriNode = required(b, "uri");
    const auto uri = string(uriNode);
    const auto decoded = detail::decodeDocumentUri(uri, uriNode.path());
    if (!decoded) {
        if (!m_error)
            m_error = decoded.error();
    } else if (std::filesystem::path(*decoded).extension() != ".bin") {
        fail(uriNode.path(), "expected a relative external .bin URI");
    }
    const auto expected = wideInteger(required(b, "byteLength"));
    if (expected == 0)
        fail(b.path() + "/byteLength", "buffer must not be empty");
    if (m_error)
        return std::unexpected(*m_error);
    auto bytes = fileBytes(m_path.parent_path() / *decoded, b.path());
    if (!bytes)
        return bytes;
    if (bytes->size() != expected)
        return std::unexpected(malformed(b.path(), "buffer '" + uri + "' byte length is " +
                                                       std::to_string(bytes->size()) +
                                                       ", expected " + std::to_string(expected)));
    m_bufferUri = *decoded;
    return bytes;
}

//======================================================================================================================
std::vector<glm::vec4> Reader::accessor(const JsonNode& index, uint32_t components,
                                        const std::vector<std::byte>& bytes) {
    const auto accessors = required(m_root, "accessors");
    const auto indexValue = integer(index);
    if (indexValue >= array(accessors)) {
        fail(index.path(), "accessor index is out of range");
        return {};
    }
    const auto a = accessors.at(indexValue);
    if (optional(a, "sparse"))
        fail(a.path() + "/sparse", "sparse document accessors are unsupported");
    if (integer(required(a, "componentType")) != 5126)
        fail(a.path() + "/componentType", "expected FLOAT (5126)");
    const auto type = string(required(a, "type"));
    if (type != (components == 1 ? "SCALAR" : components == 3 ? "VEC3" : "VEC4"))
        fail(a.path() + "/type", "accessor shape does not match the animation target");
    if (auto n = optional(a, "normalized"); n && boolean(*n))
        fail(n->path(), "FLOAT accessors must not be normalized");
    const uint32_t count = integer(required(a, "count"));
    if (!count)
        fail(a.path() + "/count", "key count must be positive");
    const auto views = required(m_root, "bufferViews");
    const auto viewIndex = required(a, "bufferView");
    const auto viewValue = integer(viewIndex);
    if (viewValue >= array(views)) {
        fail(viewIndex.path(), "buffer view index is out of range");
        return {};
    }
    const auto v = views.at(viewValue);
    if (integer(required(v, "buffer")) != 0)
        fail(v.path() + "/buffer", "buffer index is out of range");
    uint64_t viewOffset = 0;
    if (auto o = optional(v, "byteOffset"))
        viewOffset = wideInteger(*o);
    const auto viewLength = wideInteger(required(v, "byteLength"));
    uint64_t accessorOffset = 0;
    if (auto o = optional(a, "byteOffset"))
        accessorOffset = wideInteger(*o);
    if (optional(v, "byteStride"))
        fail(v.path() + "/byteStride", "animation buffers must be tightly packed");
    if (optional(v, "target"))
        fail(v.path() + "/target", "animation buffers must not have a vertex/index target");
    const uint64_t needed = uint64_t(count) * components * sizeof(float);
    if (viewOffset > bytes.size() ||
        viewLength > bytes.size() - std::min<uint64_t>(viewOffset, bytes.size()))
        fail(v.path(), "buffer view exceeds the buffer");
    if (accessorOffset > viewLength || needed > viewLength - std::min(accessorOffset, viewLength))
        fail(a.path(), "accessor exceeds its buffer view");
    if (viewOffset % 4 != 0 || accessorOffset % 4 != 0)
        fail(a.path(), "FLOAT animation data must be four-byte aligned");
    if (m_error)
        return {};
    std::vector<glm::vec4> values(count, glm::vec4(0.0f));
    size_t cursor = size_t(viewOffset + accessorOffset);
    for (uint32_t k = 0; k < count; ++k) {
        for (uint32_t component = 0; component < components; ++component) {
            uint32_t bits = 0;
            for (uint32_t shift = 0; shift < 32; shift += 8)
                bits |= uint32_t(bytes[cursor++]) << shift;
            const float value = std::bit_cast<float>(bits);
            if (!std::isfinite(value))
                fail(a.path(), "key " + std::to_string(k) + " has a non-finite component");
            values[k][component] = value;
        }
        if (components == 4 && std::abs(glm::dot(values[k], values[k]) - 1.0f) > 1e-4f)
            fail(a.path(), "key " + std::to_string(k) + " is not a unit quaternion");
    }
    if (components == 1 && !m_error) {
        vector<float, 1>(required(a, "min"));
        vector<float, 1>(required(a, "max"));
    }
    return values;
}

//======================================================================================================================
void Reader::animations(SceneDocument& doc, const std::vector<std::byte>& bytes) {
    const auto animations = optional(m_root, "animations");
    if (!animations)
        return;
    for (size_t i = 0, count = array(*animations); i < count && !m_error; ++i) {
        const auto a = animations->at(i);
        DocAnimation result;
        result.name = string(required(a, "name"));
        const auto rate = required(extension(a, "LMX_scene"), "sampleRate");
        result.sampleRate = number<double>(rate);
        if (result.sampleRate <= 0.0)
            fail(rate.path(), "sample rate must be positive");
        const auto samplers = required(a, "samplers");
        const size_t samplerCount = array(samplers);
        const auto channels = required(a, "channels");
        const size_t channelCount = array(channels);
        if (!channelCount)
            fail(channels.path(), "animation must contain a channel");
        std::set<std::pair<uint32_t, DocChannelPath>> targets;
        for (size_t j = 0; j < channelCount && !m_error; ++j) {
            const auto c = channels.at(j);
            DocChannel channel;
            const auto target = required(c, "target");
            channel.node = integer(required(target, "node"));
            if (channel.node >= doc.nodes.size())
                fail(target.path() + "/node", "node index is out of range");
            const auto path = string(required(target, "path"));
            if (path == "translation")
                channel.path = DocChannelPath::Translation;
            else if (path == "rotation")
                channel.path = DocChannelPath::Rotation;
            else if (path == "scale")
                channel.path = DocChannelPath::Scale;
            else
                fail(target.path() + "/path", "expected translation, rotation or scale");
            if (!targets.emplace(channel.node, channel.path).second)
                fail(target.path(), "duplicate node/path target within one animation");
            if (m_error)
                break;
            const auto samplerIndex = required(c, "sampler");
            const auto s = integer(samplerIndex);
            if (s >= samplerCount) {
                fail(samplerIndex.path(), "sampler index is out of range");
                break;
            }
            const auto sampler = samplers.at(s);
            if (auto interpolation = optional(sampler, "interpolation")) {
                const auto method = string(*interpolation);
                channel.step = method == "STEP";
                if (method != "STEP" && method != "LINEAR")
                    fail(interpolation->path(), "document animations support LINEAR or STEP");
            }
            const auto times = accessor(required(sampler, "input"), 1, bytes);
            if (m_error)
                break;
            if (result.keyCount && result.keyCount != times.size())
                fail(sampler.path() + "/input", "channel key counts disagree");
            result.keyCount = uint32_t(times.size());
            for (size_t k = 0; k < times.size(); ++k) {
                const float expected = float(double(k) / result.sampleRate);
                if (!std::isfinite(expected) || times[k].x != expected)
                    fail(a.path(), "animation '" + result.name + "' key " + std::to_string(k) +
                                       " time must equal float(k / sampleRate)");
                if (k && times[k].x <= times[k - 1].x)
                    fail(a.path(), "animation '" + result.name + "' key " + std::to_string(k) +
                                       " time is not strictly increasing");
            }
            const auto input =
                required(m_root, "accessors").at(integer(required(sampler, "input")));
            if (vector<float, 1>(required(input, "min"))[0] != times.front().x)
                fail(input.path() + "/min", "does not match the first time");
            if (vector<float, 1>(required(input, "max"))[0] != times.back().x)
                fail(input.path() + "/max", "does not match the last time");
            channel.values = accessor(required(sampler, "output"),
                                      channel.path == DocChannelPath::Rotation ? 4 : 3, bytes);
            if (channel.values.size() != result.keyCount)
                fail(sampler.path() + "/output",
                     "channel values and times have different key counts");
            if (m_placementAncestors.contains(channel.node)) {
                for (size_t k = 0; k < channel.values.size(); ++k) {
                    const auto value = channel.values[k];
                    const bool identity =
                        channel.path == DocChannelPath::Rotation
                            ? glm::vec3(value) == glm::vec3(0.0f) && std::abs(value.w) == 1.0f
                            : glm::vec3(value) ==
                                  glm::vec3(channel.path == DocChannelPath::Scale ? 1.0f : 0.0f);
                    if (!identity)
                        fail(target.path(), "camera/light ancestor must remain identity; key " +
                                                std::to_string(k) + " changes its transform");
                }
            }
            result.channels.push_back(std::move(channel));
        }
        doc.animations.push_back(std::move(result));
    }
}

//======================================================================================================================
AssetResult<SceneDocument> Reader::document() {
    uniqueKeys(m_root);
    object(m_root);
    const auto version = required(required(m_root, "asset"), "version");
    if (string(version) != "2.0")
        fail(version.path(), "expected glTF 2.0");
    if (optional(m_root, "meshes"))
        fail("/meshes", "scene documents contain no meshes");
    if (const auto requiredExtensions = optional(m_root, "extensionsRequired")) {
        if (array(*requiredExtensions))
            fail(requiredExtensions->path(), "scene documents use no required extensions");
    }
    const auto used = required(m_root, "extensionsUsed");
    bool hasLmx = false;
    bool hasLights = false;
    for (size_t i = 0, count = array(used); i < count; ++i) {
        const auto name = string(used.at(i));
        hasLmx |= name == "LMX_scene";
        hasLights |= name == "KHR_lights_punctual";
    }
    if (!hasLmx)
        fail(used.path(), "LMX_scene must be listed");
    SceneDocument doc;
    const auto lmx = extension(m_root, "LMX_scene");
    doc.schemaVersion = integer(required(lmx, "schemaVersion"));
    if (doc.schemaVersion != 1)
        fail(lmx.path() + "/schemaVersion", "unsupported schema version");
    doc.camera = integer(required(lmx, "camera"));
    doc.look = look(required(lmx, "look"));
    doc.loop = boolean(required(lmx, "loop"));
    const auto scenes = required(m_root, "scenes");
    if (array(scenes) != 1)
        fail(scenes.path(), "expected one glTF scene");
    if (integer(required(m_root, "scene")) != 0)
        fail("/scene", "scene index must be zero");
    if (m_error)
        return std::unexpected(*m_error);
    const auto scene = scenes.at(0);
    doc.name = string(required(scene, "name"));
    doc.rootNodes = indices(required(scene, "nodes"));
    const auto cameras = required(m_root, "cameras");
    for (size_t i = 0, count = array(cameras); i < count; ++i)
        doc.cameras.push_back(camera(cameras.at(i)));
    if (auto punctual = optional(required(m_root, "extensions"), "KHR_lights_punctual")) {
        if (!hasLights)
            fail("/extensionsUsed", "KHR_lights_punctual must be listed");
        const auto lights = required(*punctual, "lights");
        for (size_t i = 0, count = array(lights); i < count; ++i) {
            auto value = light(lights.at(i));
            if (value.type != DocLightType::Directional && !value.range) {
                doc.warnings.push_back(lights.at(i).path() + "/range: skipped local light '" +
                                       value.name + "' without a finite range");
                m_lightMap.push_back(std::nullopt);
            } else {
                m_lightMap.push_back(uint32_t(doc.lights.size()));
                doc.lights.push_back(std::move(value));
            }
        }
    }
    const auto nodes = required(m_root, "nodes");
    for (size_t i = 0, count = array(nodes); i < count; ++i)
        doc.nodes.push_back(node(nodes.at(i)));
    if (m_error)
        return std::unexpected(*m_error);
    hierarchy(doc);
    if (m_error)
        return std::unexpected(*m_error);
    const auto bytes = buffer();
    if (!bytes)
        return std::unexpected(bytes.error());
    doc.sourceBufferUri = m_bufferUri;
    animations(doc, *bytes);
    if (m_error)
        return std::unexpected(*m_error);
    for (const auto& warning : doc.warnings)
        LMX_LOG_WARN("{}: {}", m_path.string(), warning);
    return doc;
}

//======================================================================================================================
AssetResult<JsonTokens> readJson(const std::filesystem::path& path) {
    const auto bytes = fileBytes(path, "");
    if (!bytes)
        return std::unexpected(bytes.error());
    auto parsed =
        JsonTokens::parse(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!parsed) {
        auto error = parsed.error();
        error.message = path.string() + ": " + error.message;
        return std::unexpected(std::move(error));
    }
    return parsed;
}

} // namespace

//======================================================================================================================
std::optional<SceneLookRangeError> sceneLookRangeError(const SceneLook& look) {
    const auto& e = look.exposure;
    if (e.lowPercentile < 0.0f || e.lowPercentile >= e.highPercentile)
        return SceneLookRangeError{"/exposure/lowPercentile",
                                   "must be nonnegative and smaller than highPercentile"};
    if (e.highPercentile > 100.0f)
        return SceneLookRangeError{"/exposure/highPercentile", "must not exceed 100"};
    if (e.targetGrey <= 0.0f)
        return SceneLookRangeError{"/exposure/targetGrey", "must be positive"};
    if (e.evMin > e.evMax)
        return SceneLookRangeError{"/exposure/evMin", "must not exceed evMax"};
    if (e.adaptUpStopsPerSecond < 0.0f)
        return SceneLookRangeError{"/exposure/adaptUpStopsPerSecond", "must be nonnegative"};
    if (e.adaptDownStopsPerSecond < 0.0f)
        return SceneLookRangeError{"/exposure/adaptDownStopsPerSecond", "must be nonnegative"};
    if (look.bloom.threshold < 0.0f)
        return SceneLookRangeError{"/bloom/threshold", "must be nonnegative"};
    if (look.bloom.intensity < 0.0f)
        return SceneLookRangeError{"/bloom/intensity", "must be nonnegative"};
    return std::nullopt;
}

//======================================================================================================================
AssetResult<SceneDocument> readSceneDocument(const std::filesystem::path& path) {
    const auto json = readJson(path);
    if (!json)
        return std::unexpected(json.error());
    return Reader(json->root(), path).document();
}

//======================================================================================================================
AssetResult<std::optional<std::filesystem::path>>
sceneDocumentBufferPath(std::string_view gltfJson, const std::filesystem::path& document) {
    const auto parsed = JsonTokens::parse(std::string(gltfJson));
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto root = parsed->root();
    if (!root.isObject())
        return std::unexpected(malformed("", "expected a JSON object"));
    const auto buffers = root.find("buffers");
    if (!buffers)
        return std::optional<std::filesystem::path>{};
    if (!buffers->isArray() || buffers->size() != 1)
        return std::unexpected(
            malformed("/buffers", "expected exactly one external animation buffer"));
    const auto entry = buffers->at(0);
    if (!entry.isObject())
        return std::unexpected(malformed("/buffers/0", "expected an object"));
    const auto uriNode = entry.find("uri");
    if (!uriNode || !uriNode->isString())
        return std::unexpected(
            malformed("/buffers/0/uri", "expected a relative external .bin URI"));
    const auto uri = uriNode->asString();
    if (!uri)
        return std::unexpected(malformed("/buffers/0/uri", uri.error()));
    const auto decoded = detail::decodeDocumentUri(*uri, uriNode->path());
    if (!decoded)
        return std::unexpected(decoded.error());
    if (std::filesystem::path(*decoded).extension() != ".bin")
        return std::unexpected(
            malformed("/buffers/0/uri", "expected a relative external .bin URI"));
    return std::optional<std::filesystem::path>{document.parent_path() / *decoded};
}

//======================================================================================================================
AssetResult<std::string> sceneDocumentHash(const std::filesystem::path& path) {
    const auto bytes = fileBytes(path, "");
    if (!bytes)
        return std::unexpected(bytes.error());
    const auto json =
        JsonTokens::parse(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!json)
        return std::unexpected(json.error());
    auto buffer = Reader(json->root(), path).buffer();
    if (!buffer)
        return std::unexpected(buffer.error());
    std::vector<std::byte> content = *bytes;
    content.insert(content.end(), buffer->begin(), buffer->end());
    return sha256Hex(content);
}

} // namespace lmx::asset
