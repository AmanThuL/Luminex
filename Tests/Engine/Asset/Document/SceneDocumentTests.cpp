#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Document/SceneDocumentSaveInternal.h"

#include "Core/IO/File.h"
#include "Core/IO/JsonWriter.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Model/JsonTokens.h"
#include "Support/GoldenFile.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using namespace lmx::asset;
namespace fs = std::filesystem;

namespace {

//======================================================================================================================
fs::path outputPath(std::string_view name) {
    fs::create_directories("SceneDocuments");
    return fs::path("SceneDocuments") / name;
}

//======================================================================================================================
void writeText(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file << text;
    REQUIRE(file.good());
}

//======================================================================================================================
std::string readText(const fs::path& path) {
    const auto bytes = lmx::readWholeFile(path);
    REQUIRE(bytes);
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

//======================================================================================================================
SceneDocument animatedDocument() {
    SceneDocument doc;
    doc.name = "Document test";
    doc.nodes = {{.name = "Camera", .camera = 0}};
    doc.rootNodes = {0};
    doc.cameras = {{.name = "Perspective"}};
    DocAnimation animation;
    animation.name = "Camera rail";
    animation.keyCount = 2;
    animation.channels = {{.node = 0,
                           .path = DocChannelPath::Translation,
                           .values = {{0.0f, 1.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 3.0f, 0.0f}}}};
    doc.animations.push_back(animation);
    return doc;
}

//======================================================================================================================
void copyJson(lmx::JsonWriter& writer, const JsonNode& node, std::string_view target, bool omit) {
    if (node.path() == target && !omit) {
        if (node.isString())
            writer.boolean(false);
        else
            writer.string("wrong type");
    } else if (node.isObject()) {
        writer.beginObject();
        for (size_t i = 0; i < node.size(); ++i) {
            const auto value = node.memberValue(i);
            if (omit && value.path() == target)
                continue;
            writer.key(node.memberName(i));
            copyJson(writer, value, target, omit);
        }
        writer.endObject();
    } else if (node.isArray()) {
        writer.beginArray();
        for (size_t i = 0; i < node.size(); ++i)
            copyJson(writer, node.at(i), target, omit);
        writer.endArray();
    } else if (node.isString())
        writer.string(*node.asString());
    else if (node.isBool())
        writer.boolean(*node.asBool());
    else if (node.isNumber())
        writer.number(*node.asDouble());
    else
        FAIL("unexpected null in fixture");
}

//======================================================================================================================
SceneDocument completeDocument() {
    auto doc = animatedDocument();
    doc.look.environment.hdri =
        SceneLook::Hdri{.uri = "Fetched/studio.hdr", .sha256 = std::string(64, 'a')};
    DocNode source;
    source.name = "Asset";
    source.asset = DocAsset{"Fetched/model.gltf", std::string(64, 'b')};
    source.overrides = {{.node = 7, .name = "Mesh", .enabled = false, .pose = ObjectPose{}}};
    doc.nodes.push_back(source);
    doc.nodes.push_back(
        {.name = "Lab", .generator = DocGenerator{"MaterialLab", {{"first", 1.25}, {"a/b", 5.0}}}});
    doc.lights = {{.name = "Key"}};
    doc.nodes.push_back({.name = "Key node", .light = 0, .role = "key", .castsShadow = true});
    doc.rootNodes = {0, 1, 2, 3};
    return doc;
}

//======================================================================================================================
std::string jsonWithBufferUri(const SceneDocument& doc, std::string_view uri) {
    auto json = sceneDocumentJson(doc, "placeholder.scene.bin");
    lmx::JsonWriter writer;
    writer.string(uri);
    auto quoted = writer.take();
    quoted.pop_back();
    const std::string placeholder = "\"placeholder.scene.bin\"";
    const auto start = json.find(placeholder);
    REQUIRE(start != std::string::npos);
    json.replace(start, placeholder.size(), quoted);
    return json;
}

//======================================================================================================================
void writeBuffer(const SceneDocument& doc, const fs::path& path) {
    const auto bytes = sceneDocumentBuffer(doc);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    REQUIRE(file.good());
}

} // namespace

//======================================================================================================================
TEST_CASE("minimal golden scene document reads and rewrites byte-identically",
          "[asset][scene-document]") {
    const auto path = lmx::test::goldenPath("scene-document-min.scene.gltf");
    const auto doc = readSceneDocument(path);
    REQUIRE(doc);
    REQUIRE(sceneDocumentJson(*doc, "scene-document-min.scene.bin") == readText(path));
    REQUIRE(saveSceneDocument(*doc, outputPath("minimal.scene.gltf")));
    const auto saved = readSceneDocument(outputPath("minimal.scene.gltf"));
    REQUIRE(saved);
    REQUIRE(sceneDocumentJson(*saved, "minimal.scene.bin") ==
            readText(outputPath("minimal.scene.gltf")));
}

//======================================================================================================================
TEST_CASE("scene look preserves every existing exposure bloom and environment default",
          "[asset][scene-document]") {
    const SceneLook look;
    REQUIRE(look.exposure.ev == 0.0f);
    REQUIRE_FALSE(look.exposure.autoEnabled);
    REQUIRE(look.exposure.lowPercentile == 50.0f);
    REQUIRE(look.exposure.highPercentile == 95.0f);
    REQUIRE(look.exposure.targetGrey == 0.18f);
    REQUIRE(look.exposure.evMin == -8.0f);
    REQUIRE(look.exposure.evMax == 8.0f);
    REQUIRE(look.exposure.compensationEv == 0.0f);
    REQUIRE(look.exposure.adaptUpStopsPerSecond == 3.0f);
    REQUIRE(look.exposure.adaptDownStopsPerSecond == 1.5f);
    REQUIRE(look.bloom.enabled);
    REQUIRE(look.bloom.threshold == 1.0f);
    REQUIRE(look.bloom.intensity == 0.2f);
    REQUIRE(look.shadowFilter == ShadowFilter::PCF);
    REQUIRE(look.environment.skySrgb8 == std::array<uint8_t, 3>{149, 170, 196});
    REQUIRE_FALSE(look.environment.hdri);
}

//======================================================================================================================
TEST_CASE("animation times and external buffer lengths have named failures",
          "[asset][scene-document]") {
    auto doc = animatedDocument();
    const auto path = outputPath("animated.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(read->animations.front().keyCount == 2);
    REQUIRE(read->animations.front().channels.front().values ==
            doc.animations.front().channels.front().values);
    const auto binPath = outputPath("animated.scene.bin");
    const auto original = lmx::readWholeFile(binPath);
    REQUIRE(original);
    {
        std::fstream file(binPath, std::ios::binary | std::ios::in | std::ios::out);
        // One ulp from the exact stored float(k / sampleRate) must still be rejected.
        const float wrongTime =
            std::nextafter(static_cast<float>(1.0 / doc.animations.front().sampleRate), 1.0f);
        file.seekp(sizeof(float));
        file.write(reinterpret_cast<const char*>(&wrongTime), sizeof(wrongTime));
    }
    read = readSceneDocument(path);
    REQUIRE_FALSE(read);
    REQUIRE(read.error().message.contains("/animations/0"));
    REQUIRE(read.error().message.contains("key 1"));
    fs::resize_file(binPath, original->size() - 1);
    read = readSceneDocument(path);
    REQUIRE_FALSE(read);
    REQUIRE(read.error().message.contains("/buffers/0"));
    REQUIRE(read.error().message.contains("animated.scene.bin"));
    REQUIRE(saveSceneDocument(doc, path));
}

//======================================================================================================================
TEST_CASE("read-only save targets preserve both old files and leave no temporary files",
          "[asset][scene-document]") {
    const auto path = outputPath("readonly.scene.gltf");
    const auto binPath = outputPath("readonly.scene.bin");
    auto doc = animatedDocument();
    REQUIRE(saveSceneDocument(doc, path));
    const auto oldJson = readText(path);
    const auto oldBin = readText(binPath);
    doc.name = "Changed";
    for (const auto& target : {path, binPath}) {
        const auto originalPermissions = fs::status(target).permissions();
        fs::permissions(target,
                        fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
        const auto saved = saveSceneDocument(doc, path);
        fs::permissions(target, originalPermissions);
        REQUIRE_FALSE(saved);
        REQUIRE(saved.error().message.contains(target.filename().string()));
        REQUIRE(readText(path) == oldJson);
        REQUIRE(readText(binPath) == oldBin);
    }
    for (const auto& entry : fs::directory_iterator(path.parent_path())) {
        REQUIRE_FALSE(entry.path().filename().string().contains(".tmp"));
        REQUIRE_FALSE(entry.path().filename().string().contains(".bak"));
    }
}

//======================================================================================================================
TEST_CASE("scene document hash covers JSON then referenced buffer bytes",
          "[asset][scene-document]") {
    const auto path = outputPath("hashed.scene.gltf");
    REQUIRE(saveSceneDocument(animatedDocument(), path));
    auto combined = *lmx::readWholeFile(path);
    const auto buffer = *lmx::readWholeFile(outputPath("hashed.scene.bin"));
    combined.insert(combined.end(), buffer.begin(), buffer.end());
    REQUIRE(sceneDocumentHash(path) == lmx::sha256Hex(combined));
}

//======================================================================================================================
TEST_CASE("candidate buffer path decodes the glTF URI before its file exists",
          "[asset][scene-document][session]") {
    const fs::path directory = "SceneDocumentNegativeFixtures";
    fs::create_directories(directory);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{directory};
    const fs::path document = directory / "candidate.scene.gltf";
    for (const auto& [uri, expected] : std::vector<std::pair<std::string, fs::path>>{
             {"new%20pair.bin", "SceneDocumentNegativeFixtures/new pair.bin"},
             {"percent%25.bin", "SceneDocumentNegativeFixtures/percent%.bin"},
             {"nested/%E4%B8%AD.bin",
              fs::path("SceneDocumentNegativeFixtures/nested/\xE4\xB8\xAD.bin")}}) {
        INFO(uri);
        const auto candidate = sceneDocumentBufferPath(
            R"({"buffers":[{"uri":")" + uri + R"(","byteLength":2}]})", document);
        REQUIRE(candidate);
        REQUIRE(*candidate);
        CHECK(**candidate == expected);
    }
    const auto none = sceneDocumentBufferPath(R"({"asset":{"uri":"image.png"}})", document);
    REQUIRE(none);
    CHECK_FALSE(*none);

    for (const std::string json :
         {"[]", R"({"buffers":[42]})", R"({"buffers":42})", R"({"buffers":[{"uri":42}]})",
          R"({"buffers":[{"uri":"../bad.bin"}]})", "{bad json"}) {
        INFO(json);
        CHECK_FALSE(sceneDocumentBufferPath(json, document));
        writeText(document, json);
        CHECK_FALSE(sceneDocumentHash(document));
    }
    fs::remove_all(directory);
    CHECK_FALSE(fs::exists(directory));
    CHECK_FALSE(fs::exists("SceneDocuments/candidate.scene.gltf"));
}

//======================================================================================================================
TEST_CASE("every required LMX field rejects missing and mistyped values at its exact pointer",
          "[asset][scene-document]") {
    const auto doc = completeDocument();
    const auto path = outputPath("complete.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto text = sceneDocumentJson(doc, "complete.scene.bin");
    const auto parsed = JsonTokens::parse(text);
    REQUIRE(parsed);
    const std::string root = "/extensions/LMX_scene";
    std::vector<std::string> fields = {
        root,
        root + "/schemaVersion",
        root + "/camera",
        root + "/look",
        root + "/loop",
        root + "/look/exposure",
        root + "/look/bloom",
        root + "/look/shadow",
        root + "/look/environment",
        root + "/look/shadow/filter",
        root + "/look/environment/skySrgb8",
        "/nodes/0/extensions/LMX_scene",
        "/nodes/0/extensions/LMX_scene/enabled",
        "/nodes/1/extensions/LMX_scene/asset/uri",
        "/nodes/1/extensions/LMX_scene/asset/sha256",
        "/nodes/1/extensions/LMX_scene/overrides/0/node",
        "/nodes/1/extensions/LMX_scene/overrides/0/name",
        "/nodes/1/extensions/LMX_scene/overrides/0/pose/translation",
        "/nodes/1/extensions/LMX_scene/overrides/0/pose/eulerDegrees",
        "/nodes/1/extensions/LMX_scene/overrides/0/pose/scale",
        "/nodes/2/extensions/LMX_scene/generator/name",
        "/nodes/2/extensions/LMX_scene/generator/params",
        "/nodes/3/extensions/LMX_scene/role",
        "/nodes/3/extensions/LMX_scene/castsShadow",
        "/animations/0/extensions/LMX_scene/sampleRate"};
    for (const auto* key :
         {"ev", "autoEnabled", "lowPercentile", "highPercentile", "targetGrey", "evMin", "evMax",
          "compensationEv", "adaptUpStopsPerSecond", "adaptDownStopsPerSecond"})
        fields.push_back(root + "/look/exposure/" + key);
    for (const auto* key : {"enabled", "threshold", "intensity"})
        fields.push_back(root + "/look/bloom/" + key);
    for (const auto* key : {"uri", "sha256", "yaw", "scale", "faceSize", "diffuseFaceSize"})
        fields.push_back(root + "/look/environment/hdri/" + key);
    const auto invalid = outputPath("invalid.input.json");
    for (const auto& field : fields) {
        for (bool omit : {true, false}) {
            INFO(field);
            INFO(omit);
            lmx::JsonWriter writer;
            copyJson(writer, parsed->root(), field, omit);
            writeText(invalid, writer.take());
            const auto result = readSceneDocument(invalid);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().message.contains("'" + field + "'"));
        }
    }
    fs::remove(invalid);
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(read->nodes[2].generator->params == doc.nodes[2].generator->params);
    REQUIRE(sceneDocumentJson(*read, "complete.scene.bin") == text);
}

//======================================================================================================================
TEST_CASE("a local light without range is skipped once and directional indices survive",
          "[asset][scene-document]") {
    auto doc = animatedDocument();
    doc.lights = {{.name = "Infinite point", .type = DocLightType::Point},
                  {.name = "Sun", .type = DocLightType::Directional}};
    doc.nodes.push_back({.name = "Point node", .light = 0});
    doc.nodes.push_back({.name = "Sun node", .light = 1, .role = "key", .castsShadow = true});
    doc.rootNodes = {0, 1, 2};
    const auto input = outputPath("unbounded.input.json");
    writeText(input, sceneDocumentJson(doc, "unbounded.scene.bin"));
    const auto bin = sceneDocumentBuffer(doc);
    const auto binPath = outputPath("unbounded.scene.bin");
    {
        std::ofstream file(binPath, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size()));
    }
    const auto read = readSceneDocument(input);
    REQUIRE(read);
    REQUIRE(read->warnings.size() == 1);
    REQUIRE(read->warnings[0].contains("/extensions/KHR_lights_punctual/lights/0/range"));
    REQUIRE(read->lights.size() == 1);
    REQUIRE(read->lights[0].type == DocLightType::Directional);
    REQUIRE_FALSE(read->nodes[1].light);
    REQUIRE(read->nodes[2].light == 0);
    REQUIRE(saveSceneDocument(*read, outputPath("bounded.scene.gltf")));
    fs::remove(input);
}

//======================================================================================================================
TEST_CASE("invalid hierarchy and model saves fail without replacing existing documents",
          "[asset][scene-document]") {
    auto doc = animatedDocument();
    const auto path = outputPath("unchanged.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto original = readText(path);
    const auto originalBin = readText(outputPath("unchanged.scene.bin"));
    doc.nodes[0].children = {0};
    auto result = saveSceneDocument(doc, path);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.contains("/nodes/0/children"));
    REQUIRE(readText(path) == original);
    REQUIRE(readText(outputPath("unchanged.scene.bin")) == originalBin);
    doc.nodes[0].children.clear();
    doc.look.bloom.threshold = std::numeric_limits<float>::infinity();
    result = saveSceneDocument(doc, path);
    REQUIRE_FALSE(result);
    REQUIRE(readText(path) == original);
    REQUIRE(readText(outputPath("unchanged.scene.bin")) == originalBin);
}

//======================================================================================================================
TEST_CASE("node transform serialization retains signed zero and exact quaternion components",
          "[asset][scene-document]") {
    auto doc = animatedDocument();
    doc.nodes[0].translation.x = -0.0f;
    doc.nodes[0].rotation.y = -0.0f;
    const auto path = outputPath("signed-zero.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(std::bit_cast<uint32_t>(read->nodes[0].translation.x) == 0x80000000u);
    REQUIRE(std::bit_cast<uint32_t>(read->nodes[0].rotation.y) == 0x80000000u);
    REQUIRE(sceneDocumentJson(*read, "signed-zero.scene.bin") ==
            sceneDocumentJson(doc, "signed-zero.scene.bin"));
}

//======================================================================================================================
TEST_CASE("optional extension fields reject wrong types and arrays reject bad indices",
          "[asset][scene-document]") {
    auto doc = completeDocument();
    const auto path = outputPath("optional.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto parsed = JsonTokens::parse(sceneDocumentJson(doc, "optional.scene.bin"));
    REQUIRE(parsed);
    const auto input = outputPath("optional.input.json");
    for (const std::string field :
         {"/extensions/LMX_scene/look/environment/hdri", "/nodes/1/extensions/LMX_scene/asset",
          "/nodes/1/extensions/LMX_scene/overrides",
          "/nodes/1/extensions/LMX_scene/overrides/0/enabled",
          "/nodes/1/extensions/LMX_scene/overrides/0/pose",
          "/nodes/2/extensions/LMX_scene/generator",
          "/nodes/2/extensions/LMX_scene/generator/params/a~1b"}) {
        INFO(field);
        lmx::JsonWriter writer;
        copyJson(writer, parsed->root(), field, false);
        writeText(input, writer.take());
        const auto read = readSceneDocument(input);
        REQUIRE_FALSE(read);
        REQUIRE(read.error().message.contains("'" + field + "'"));
    }
    doc.nodes[1].children = {900};
    writeText(input, sceneDocumentJson(doc, "optional.scene.bin"));
    const auto badChild = readSceneDocument(input);
    REQUIRE_FALSE(badChild);
    REQUIRE(badChild.error().message.contains("/nodes/1/children/0"));
    fs::remove(input);
}

//======================================================================================================================
TEST_CASE("a failed staged save removes temporaries and preserves both existing files",
          "[asset][scene-document]") {
    const auto path = outputPath("staged-failure.scene.gltf");
    auto doc = animatedDocument();
    REQUIRE(saveSceneDocument(doc, path));
    const auto json = readText(path);
    const auto bin = readText(outputPath("staged-failure.scene.bin"));
    doc.nodes[0].camera = 42;
    const auto saved = saveSceneDocument(doc, path);
    REQUIRE_FALSE(saved);
    REQUIRE(saved.error().message.contains("/nodes/0/camera"));
    REQUIRE(readText(path) == json);
    REQUIRE(readText(outputPath("staged-failure.scene.bin")) == bin);
    for (const auto& entry : fs::directory_iterator(path.parent_path()))
        REQUIRE_FALSE(entry.path().filename().string().starts_with(".lmx-save-"));
}

//======================================================================================================================
TEST_CASE("Save As encodes companion URI punctuation and hashes decoded files",
          "[asset][scene-document][scene-document-review]") {
    const auto doc = animatedDocument();
    for (const auto& [name, encoded] :
         std::vector<std::pair<std::string, std::string>>{{"My Scene", "My%20Scene"},
                                                          {"percent%", "percent%25"},
                                                          {"hash#", "hash%23"},
                                                          {"query?", "query%3F"},
                                                          {"all %#?", "all%20%25%23%3F"}}) {
        INFO(name);
        const auto path = outputPath(name + ".scene.gltf");
        REQUIRE(saveSceneDocument(doc, path));
        const auto json = readText(path);
        REQUIRE(json.contains("\"uri\": \"" + encoded + ".scene.bin\""));
        auto bytes = *lmx::readWholeFile(path);
        const auto buffer = *lmx::readWholeFile(outputPath(name + ".scene.bin"));
        bytes.insert(bytes.end(), buffer.begin(), buffer.end());
        REQUIRE(sceneDocumentHash(path) == lmx::sha256Hex(bytes));
        const auto read = readSceneDocument(path);
        REQUIRE(read);
        REQUIRE(read->animations[0].channels[0].values == doc.animations[0].channels[0].values);
    }
}

//======================================================================================================================
TEST_CASE("independently encoded buffers load once and unsafe URI decoding fails by pointer",
          "[asset][scene-document][scene-document-review]") {
    const auto doc = animatedDocument();
    const auto input = outputPath("uri.input.json");
    const auto bin = outputPath("independent %#?.scene.bin");
    writeBuffer(doc, bin);
    writeText(input, jsonWithBufferUri(doc, "independent%20%25%23%3f.scene.bin"));
    const auto read = readSceneDocument(input);
    REQUIRE(read);
    REQUIRE(read->animations[0].channels[0].values == doc.animations[0].channels[0].values);
    REQUIRE(sceneDocumentHash(input));
    for (const std::string uri :
         {"bad%.bin", "bad%2.bin", "bad%GG.bin", "../outside.bin", "%2e%2e/outside.bin",
          "sub/%2E%2e/outside.bin", "%2Fabsolute.bin", "/absolute.bin", "a%00.bin", "C%3a/path.bin",
          "a%5cb.bin", "raw space.bin", "raw#.bin", "raw?.bin"}) {
        INFO(uri);
        writeText(input, jsonWithBufferUri(doc, uri));
        const auto invalid = readSceneDocument(input);
        REQUIRE_FALSE(invalid);
        REQUIRE(invalid.error().message.contains("/buffers/0/uri"));
        REQUIRE_FALSE(sceneDocumentHash(input));
    }
    const auto literal = outputPath("literal%2e%2e.scene.bin");
    writeBuffer(doc, literal);
    writeText(input, jsonWithBufferUri(doc, "literal%252e%252e.scene.bin"));
    REQUIRE(readSceneDocument(input));
    fs::remove(input);
    fs::remove(bin);
    fs::remove(literal);
}

//======================================================================================================================
TEST_CASE("asset and HDRI model paths are decoded once and serialized as URI paths",
          "[asset][scene-document][scene-document-review]") {
    auto doc = completeDocument();
    doc.nodes[1].asset->uri = "Fetched/Model % #?.gltf";
    doc.look.environment.hdri->uri = "Fetched/Studio % #?.hdr";
    const auto path = outputPath("reference-uri.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto json = readText(path);
    REQUIRE(json.contains("Fetched/Model%20%25%20%23%3F.gltf"));
    REQUIRE(json.contains("Fetched/Studio%20%25%20%23%3F.hdr"));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(read->nodes[1].asset->uri == doc.nodes[1].asset->uri);
    REQUIRE(read->look.environment.hdri->uri == doc.look.environment.hdri->uri);
    const auto input = outputPath("reference-uri.input.json");
    for (const auto& [encoded, pointer] : std::vector<std::pair<std::string, std::string>>{
             {"Fetched/Model%20%25%20%23%3F.gltf", "/nodes/1/extensions/LMX_scene/asset/uri"},
             {"Fetched/Studio%20%25%20%23%3F.hdr",
              "/extensions/LMX_scene/look/environment/hdri/uri"}}) {
        auto malformed = json;
        const auto start = malformed.find(encoded);
        REQUIRE(start != std::string::npos);
        malformed.replace(start, encoded.size(), "bad%00.file");
        writeText(input, malformed);
        const auto rejected = readSceneDocument(input);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error().message.contains(pointer));
    }
    fs::remove(input);
}

//======================================================================================================================
TEST_CASE("camera and light ancestors stay identity across all animation samples",
          "[asset][scene-document][scene-document-review]") {
    for (const bool cameraChild : {true, false}) {
        auto doc = animatedDocument();
        doc.nodes.push_back({.name = "Group"});
        if (cameraChild)
            doc.nodes[1].children = {0};
        else {
            doc.lights.push_back({.name = "Key"});
            doc.nodes.push_back({.name = "Light", .light = 0, .role = "key"});
            doc.nodes[1].children = {2};
        }
        doc.rootNodes = cameraChild ? std::vector<uint32_t>{1} : std::vector<uint32_t>{0, 1};
        const auto path =
            outputPath(cameraChild ? "camera-ancestor.scene.gltf" : "light-ancestor.scene.gltf");
        REQUIRE(saveSceneDocument(doc, path));
        const auto originalJson = readText(path);
        auto binPath = path;
        binPath.replace_extension(".bin");
        const auto originalBin = readText(binPath);
        const auto input = outputPath("ancestor.input.json");
        for (const auto channelPath :
             {DocChannelPath::Translation, DocChannelPath::Rotation, DocChannelPath::Scale}) {
            INFO(cameraChild);
            INFO(static_cast<int>(channelPath));
            auto changed = doc;
            auto& c = changed.animations[0].channels[0];
            c.node = 1;
            c.path = channelPath;
            const auto identity = channelPath == DocChannelPath::Rotation ? glm::vec4(0, 0, 0, 1)
                                  : channelPath == DocChannelPath::Scale  ? glm::vec4(1, 1, 1, 0)
                                                                          : glm::vec4(0);
            c.values = {identity, identity};
            c.values.back() = channelPath == DocChannelPath::Rotation ? glm::vec4(0, 1, 0, 0)
                              : channelPath == DocChannelPath::Scale  ? glm::vec4(2, 1, 1, 0)
                                                                      : glm::vec4(1, 0, 0, 0);
            writeText(input, sceneDocumentJson(changed, "ancestor.input.bin"));
            writeBuffer(changed, outputPath("ancestor.input.bin"));
            const auto invalid = readSceneDocument(input);
            REQUIRE_FALSE(invalid);
            REQUIRE(invalid.error().message.contains("/animations/0/channels/0/target"));
            REQUIRE_FALSE(saveSceneDocument(changed, path));
            REQUIRE(readText(path) == originalJson);
            REQUIRE(readText(binPath) == originalBin);
            c.values = {identity, identity};
            const auto identityPath =
                outputPath(std::string(cameraChild ? "camera" : "light") + "-identity-" +
                           std::to_string(static_cast<int>(channelPath)) + ".scene.gltf");
            REQUIRE(saveSceneDocument(changed, identityPath));
            c.node = cameraChild ? 0 : 2;
            c.values.back() = channelPath == DocChannelPath::Rotation ? glm::vec4(0, 1, 0, 0)
                              : channelPath == DocChannelPath::Scale  ? glm::vec4(2, 1, 1, 0)
                                                                      : glm::vec4(1, 0, 0, 0);
            const auto subjectPath =
                outputPath(std::string(cameraChild ? "camera" : "light") + "-subject-" +
                           std::to_string(static_cast<int>(channelPath)) + ".scene.gltf");
            REQUIRE(saveSceneDocument(changed, subjectPath));
        }
        fs::remove(input);
        fs::remove(outputPath("ancestor.input.bin"));
    }
}

//======================================================================================================================
TEST_CASE("one animation rejects duplicate targets while separate animations may overlap",
          "[asset][scene-document][scene-document-review]") {
    auto doc = animatedDocument();
    const auto path = outputPath("unique-targets.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto originalJson = readText(path);
    const auto originalBin = readText(outputPath("unique-targets.scene.bin"));
    auto duplicate = doc;
    duplicate.animations[0].channels.push_back(duplicate.animations[0].channels[0]);
    const auto input = outputPath("duplicate.input.json");
    writeText(input, sceneDocumentJson(duplicate, "duplicate.input.bin"));
    writeBuffer(duplicate, outputPath("duplicate.input.bin"));
    const auto invalid = readSceneDocument(input);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().message.contains("/animations/0/channels/1/target"));
    REQUIRE_FALSE(saveSceneDocument(duplicate, path));
    REQUIRE(readText(path) == originalJson);
    REQUIRE(readText(outputPath("unique-targets.scene.bin")) == originalBin);
    fs::remove(input);
    fs::remove(outputPath("duplicate.input.bin"));
    doc.animations.push_back(doc.animations.front());
    doc.animations.back().name = "Independent clip";
    REQUIRE(saveSceneDocument(doc, outputPath("cross-animation-target.scene.gltf")));
}

//======================================================================================================================
TEST_CASE("rename failures restore existing and absent targets after backups or installation",
          "[asset][scene-document][scene-document-review]") {
    const auto original = animatedDocument();
    auto changed = original;
    changed.name = "Changed pair";
    changed.animations[0].channels[0].values.back().x = 99.0f;
    for (unsigned presentMask = 0; presentMask < 4; ++presentMask) {
        // A companion beside an absent document is foreign data and is refused, not replaced.
        if (presentMask == 2)
            continue;
        const unsigned backups = ((presentMask & 1u) != 0u) + ((presentMask & 2u) != 0u);
        std::vector<unsigned> failures{backups + 1, backups + 2};
        if (presentMask == 3)
            failures.insert(failures.begin(), 2);
        for (const unsigned failAt : failures) {
            INFO(presentMask);
            INFO(failAt);
            const auto path = outputPath("rollback-" + std::to_string(presentMask) + "-" +
                                         std::to_string(failAt) + ".scene.gltf");
            auto binPath = path;
            binPath.replace_extension(".bin");
            REQUIRE(saveSceneDocument(original, path));
            const auto json = readText(path);
            const auto bin = readText(binPath);
            if (!(presentMask & 1u))
                fs::remove(path);
            if (!(presentMask & 2u))
                fs::remove(binPath);
            unsigned renameCalls = 0;
            const auto failed = detail::saveSceneDocumentWithRename(
                changed, path,
                [&](const fs::path& from, const fs::path& to, std::error_code& error) {
                    ++renameCalls;
                    if (renameCalls == failAt)
                        error = std::make_error_code(std::errc::io_error);
                    else
                        fs::rename(from, to, error);
                });
            REQUIRE_FALSE(failed);
            REQUIRE(renameCalls >= failAt);
            REQUIRE(fs::exists(path) == bool(presentMask & 1u));
            REQUIRE(fs::exists(binPath) == bool(presentMask & 2u));
            if (presentMask & 1u)
                REQUIRE(readText(path) == json);
            if (presentMask & 2u)
                REQUIRE(readText(binPath) == bin);
            for (const auto& entry : fs::directory_iterator(path.parent_path()))
                REQUIRE_FALSE(entry.path().filename().string().starts_with(".lmx-save-"));
            REQUIRE(saveSceneDocument(original, path));
        }
    }
}

//======================================================================================================================
TEST_CASE("a document without animations writes no companion buffer", "[asset][scene-document]") {
    auto doc = animatedDocument();
    doc.animations.clear();
    const auto path = outputPath("unbuffered.scene.gltf");
    const auto binPath = outputPath("unbuffered.scene.bin");
    fs::remove(binPath);
    REQUIRE(saveSceneDocument(doc, path));
    CHECK_FALSE(fs::exists(binPath));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK_FALSE(read->sourceBufferUri);
    const auto hash = sceneDocumentHash(path);
    REQUIRE(hash);
    const auto json = readText(path);
    CHECK(*hash == lmx::sha256Hex({reinterpret_cast<const std::byte*>(json.data()), json.size()}));
}

//======================================================================================================================
TEST_CASE("saving an animated document twice replaces both files with identical bytes",
          "[asset][scene-document]") {
    const auto path = outputPath("twice.scene.gltf");
    const auto binPath = outputPath("twice.scene.bin");
    const auto doc = animatedDocument();
    REQUIRE(saveSceneDocument(doc, path));
    const auto firstJson = readText(path);
    const auto firstBin = readText(binPath);
    REQUIRE_FALSE(firstBin.empty());
    REQUIRE(saveSceneDocument(doc, path));
    CHECK(readText(path) == firstJson);
    CHECK(readText(binPath) == firstBin);
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK(read->sourceBufferUri == "twice.scene.bin");
    for (const auto& entry : fs::directory_iterator(path.parent_path()))
        CHECK_FALSE(entry.path().filename().string().starts_with(".lmx-save-"));
}

//======================================================================================================================
TEST_CASE("Save As refuses to overwrite a companion the target document does not reference",
          "[asset][scene-document]") {
    const auto path = outputPath("foreign.scene.gltf");
    const auto binPath = outputPath("foreign.scene.bin");
    fs::remove(path);
    writeText(binPath, "someone else's data");
    const auto doc = animatedDocument();
    const auto result = saveSceneDocument(doc, path);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.contains("foreign.scene.bin"));
    CHECK(readText(binPath) == "someone else's data");
    CHECK_FALSE(fs::exists(path));
    // A document that names the companion may replace it.
    fs::remove(binPath);
    REQUIRE(saveSceneDocument(doc, path));
    REQUIRE(saveSceneDocument(doc, path));
}
