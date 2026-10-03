#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Document/SceneDocumentSaveInternal.h"

#include "Core/IO/File.h"
#include "Core/IO/JsonWriter.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Model/JsonTokens.h"
#include "Support/GoldenFile.h"
#include "Support/SceneDocumentFixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <ranges>
#include <string>

using namespace lmx::asset;
using lmx::test::animatedDocument;
using lmx::test::completeDocument;
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
TEST_CASE("look ranges name the first violated bound below the look object",
          "[asset][scene-document]") {
    CHECK_FALSE(sceneLookRangeError(SceneLook{}));
    const auto path = [](auto&& change) {
        SceneLook look;
        change(look);
        const auto error = sceneLookRangeError(look);
        return error ? error->path : std::string{};
    };
    CHECK(path([](SceneLook& l) { l.exposure.lowPercentile = -0.5f; }) ==
          "/exposure/lowPercentile");
    CHECK(path([](SceneLook& l) { l.exposure.lowPercentile = 95.0f; }) ==
          "/exposure/lowPercentile");
    CHECK(path([](SceneLook& l) { l.exposure.highPercentile = 100.5f; }) ==
          "/exposure/highPercentile");
    CHECK(path([](SceneLook& l) { l.exposure.targetGrey = 0.0f; }) == "/exposure/targetGrey");
    CHECK(path([](SceneLook& l) { l.exposure.evMin = 8.5f; }) == "/exposure/evMin");
    CHECK(path([](SceneLook& l) { l.exposure.adaptUpStopsPerSecond = -0.5f; }) ==
          "/exposure/adaptUpStopsPerSecond");
    CHECK(path([](SceneLook& l) { l.exposure.adaptDownStopsPerSecond = -0.5f; }) ==
          "/exposure/adaptDownStopsPerSecond");
    CHECK(path([](SceneLook& l) { l.bloom.threshold = -0.5f; }) == "/bloom/threshold");
    CHECK(path([](SceneLook& l) { l.bloom.intensity = -0.5f; }) == "/bloom/intensity");
    CHECK(path([](SceneLook& l) {
              l.exposure.lowPercentile = 0.0f;
              l.exposure.highPercentile = 100.0f;
              l.exposure.evMin = l.exposure.evMax;
              l.exposure.adaptUpStopsPerSecond = 0.0f;
              l.exposure.adaptDownStopsPerSecond = 0.0f;
              l.bloom.threshold = 0.0f;
              l.bloom.intensity = 0.0f;
          }).empty());
}

//======================================================================================================================
TEST_CASE("the reader reports a look range violation at its document pointer",
          "[asset][scene-document]") {
    SceneDocument doc;
    doc.nodes = {{.name = "Camera", .camera = 0}};
    doc.rootNodes = {0};
    doc.cameras = {{.name = "Perspective"}};
    doc.look.bloom.intensity = -1.0f;
    const auto path = outputPath("look-range.scene.gltf");
    writeText(path, sceneDocumentJson(doc, "look-range.scene.bin"));
    const auto read = readSceneDocument(path);
    REQUIRE_FALSE(read);
    CHECK(read.error().message.contains("'/extensions/LMX_scene/look/bloom/intensity'"));
    CHECK(read.error().message.contains("must be nonnegative"));
    fs::remove(path);
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
    const auto doc = sceneDocumentSaveForm(completeDocument());
    const auto path = outputPath("complete.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto text = sceneDocumentJson(doc, "complete.scene.bin");
    // Legacy validation inputs retain their independently named animation buffer.
    const auto parsed =
        JsonTokens::parse(sceneDocumentJson(completeDocument(), "complete.scene.bin"));
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
    doc.schemaVersion = kSceneDocumentSchema;
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

//======================================================================================================================
TEST_CASE("emissive animation pointer reads beside a mesh rotation channel",
          "[asset][scene-document][ux6-emissive]") {
    auto doc = lmx::test::contentDocument();
    doc.animations = {
        {.name = "Object and sign",
         .keyCount = 2,
         .channels = {
             {.node = 1, .path = DocChannelPath::Rotation, .values = {{0, 0, 0, 1}, {0, 1, 0, 0}}},
             {.node = 2, .step = true, .values = {{1, 0, 0, 0}, {3, 0, 0, 0}}}}}};
    const auto path = outputPath("emissive-pointer.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    auto json = readText(path);
    const auto replace = [&](std::string_view pointer, std::string_view replacement) {
        const auto parsed = JsonTokens::parse(json);
        REQUIRE(parsed);
        const auto root = parsed->root();
        std::function<std::optional<JsonNode>(const JsonNode&)> find;
        find = [&](const JsonNode& node) -> std::optional<JsonNode> {
            if (node.path() == pointer)
                return node;
            if (node.isObject() || node.isArray())
                for (size_t i = 0; i < node.size(); ++i)
                    if (auto found = find(node.isObject() ? node.memberValue(i) : node.at(i)))
                        return found;
            return std::nullopt;
        };
        const auto value = find(root);
        REQUIRE(value);
        const auto old = value->sourceJson();
        json.replace(size_t(old.data() - root.sourceJson().data()), old.size(), replacement);
    };
    replace(
        "/animations/0/channels/1/target",
        R"({"path":"pointer","extensions":{"KHR_animation_pointer":{"pointer":"/materials/0/extensions/KHR_materials_emissive_strength/emissiveStrength"}}})");
    replace("/buffers/0/byteLength", "48");
    replace("/bufferViews/2/byteLength", "8");
    replace("/accessors/2/type", R"("SCALAR")");
    replace("/extensionsUsed",
            R"(["LMX_scene","KHR_materials_emissive_strength","KHR_animation_pointer"])");
    writeText(path, json);
    auto bytes = sceneDocumentBuffer(doc);
    std::copy_n(bytes.begin() + 52, 4, bytes.begin() + 44);
    bytes.resize(48);
    {
        std::ofstream file(outputPath("emissive-pointer.scene.bin"),
                           std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        REQUIRE(file.good());
    }
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(read->animations[0].channels.size() == 2);
    CHECK(read->animations[0].channels[1].path == DocChannelPath::EmissiveStrength);
    CHECK(read->animations[0].channels[1].material == 0);
    CHECK(read->animations[0].channels[1].step);
    CHECK_FALSE(read->animations[0].channels[0].material);
    CHECK(read->animations[0].channels[0].values == doc.animations[0].channels[0].values);
    CHECK(read->animations[0].channels[1].values == doc.animations[0].channels[1].values);
    REQUIRE(saveSceneDocument(*read, outputPath("emissive-roundtrip.scene.gltf")));
    const auto roundtrip = readSceneDocument(outputPath("emissive-roundtrip.scene.gltf"));
    REQUIRE(roundtrip);
    CHECK(sceneDocumentBuffer(*roundtrip) == bytes);
    CHECK(sceneDocumentJson(*roundtrip, "emissive-roundtrip.scene.bin") ==
          readText(outputPath("emissive-roundtrip.scene.gltf")));
    const auto originalJson = json;
    const std::pair<std::string, std::string> failures[]{
        {"/animations/0/channels/1/target/extensions/KHR_animation_pointer/pointer",
         R"("/materials/0/emissiveFactor")"},
        {"/animations/0/channels/1/target/extensions/KHR_animation_pointer/pointer",
         R"("/materials/2/extensions/KHR_materials_emissive_strength/emissiveStrength")"},
        {"/animations/0/channels/1/target/extensions/KHR_animation_pointer/pointer",
         R"("/materials/00/extensions/KHR_materials_emissive_strength/emissiveStrength")"},
        {"/animations/0/samplers/1/interpolation", R"("LINEAR")"},
        {"/animations/0/extensions/LMX_scene/sampleRate", "30"},
        {"/accessors/2/type", R"("VEC3")"},
        {"/extensionsUsed", R"(["LMX_scene","KHR_materials_emissive_strength"])"},
        {"/extensionsUsed", R"(["LMX_scene","KHR_animation_pointer"])"},
        {"/animations/0/channels/1/target",
         R"({"node":2,"path":"translation","extensions":{"KHR_animation_pointer":{"pointer":"/materials/0/emissiveFactor"}}})"}};
    for (const auto& [pointer, replacement] : std::views::reverse(failures)) {
        INFO(pointer);
        json = originalJson;
        replace(pointer, replacement);
        writeText(path, json);
        const auto invalid = readSceneDocument(path);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().message.contains(
            pointer.ends_with("/sampleRate") ? "/animations/0"
            : pointer.ends_with("/target")   ? pointer + "/extensions/KHR_animation_pointer/pointer"
                                             : pointer));
    }
    writeText(path, originalJson);
}

//======================================================================================================================
TEST_CASE("emissive material targets remain independent of unused node identities",
          "[asset][scene-document][ux6-emissive]") {
    auto doc = lmx::test::contentDocument();
    doc.animations = {{.name = "Two signs",
                       .keyCount = 2,
                       .channels = {{.path = DocChannelPath::EmissiveStrength,
                                     .material = 0,
                                     .step = true,
                                     .values = {{1, 0, 0, 0}, {3, 0, 0, 0}}},
                                    {.path = DocChannelPath::EmissiveStrength,
                                     .material = 1,
                                     .step = true,
                                     .values = {{2, 0, 0, 0}, {4, 0, 0, 0}}}}}};
    const auto path = outputPath("emissive-materials.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK(read->animations[0].channels[0].material == 0);
    CHECK(read->animations[0].channels[1].material == 1);
    doc.animations[0].channels[1].material = 0;
    writeText(path, sceneDocumentJson(doc, "emissive-materials.scene.bin"));
    const auto duplicate = readSceneDocument(path);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().message.contains("/animations/0/channels/1/target"));
    CHECK(duplicate.error().message.contains("duplicate"));
    doc.animations[0].channels[1].material = 1;
    writeText(path, sceneDocumentJson(doc, "emissive-materials.scene.bin"));
    SECTION("missing material") {
        doc.animations[0].channels[0].material.reset();
    }
    SECTION("out of range material") {
        doc.animations[0].channels[0].material = 2;
    }
    SECTION("LINEAR material") {
        doc.animations[0].channels[0].step = false;
    }
    SECTION("non 60 Hz material") {
        doc.animations[0].sampleRate = 30;
    }
    SECTION("negative strength") {
        doc.animations[0].channels[0].values[0].x = -1;
    }
    SECTION("material on transform") {
        doc.animations[0].channels[0].path = DocChannelPath::Translation;
    }
    REQUIRE_FALSE(validateSceneDocumentModel(doc));
}

//======================================================================================================================
TEST_CASE("schema one saves migrate to schema two without changing scene values",
          "[asset][scene-document][ux6-mobility]") {
    const auto source = lmx::test::goldenPath("scene-document-min.scene.gltf");
    const auto legacy = readSceneDocument(source);
    REQUIRE(legacy);
    CHECK(legacy->schemaVersion == 1);
    const auto path = outputPath("mobility-migration.scene.gltf");
    REQUIRE(saveSceneDocument(*legacy, path));
    const auto migrated = readSceneDocument(path);
    REQUIRE(migrated);
    CHECK(migrated->schemaVersion == 2);
    CHECK(migrated->nodes[0].translation == legacy->nodes[0].translation);
    CHECK(migrated->nodes[0].rotation == legacy->nodes[0].rotation);
    CHECK(migrated->nodes[0].scale == legacy->nodes[0].scale);
    CHECK(sceneDocumentBuffer(*migrated) == sceneDocumentBuffer(*legacy));
}

//======================================================================================================================
TEST_CASE("mobility rejects invalid values and unsupported node kinds with its pointer",
          "[asset][scene-document][ux6-mobility]") {
    auto doc = animatedDocument();
    doc.schemaVersion = 2;
    const auto kind = GENERATE(0, 1, 2, 3);
    if (kind == 1) {
        doc.nodes.push_back({.name = "Group"});
        doc.rootNodes.push_back(1);
    } else if (kind == 2) {
        doc.nodes.push_back({.name = "Generator", .generator = DocGenerator{"visibility-lab", {}}});
        doc.rootNodes.push_back(1);
    } else if (kind == 3) {
        doc.nodes.push_back(
            {.name = "Asset", .asset = DocAsset{"model.gltf", std::string(64, 'a')}});
        doc.rootNodes.push_back(1);
    }
    auto json = sceneDocumentJson(doc, "mobility-invalid.scene.bin");
    const size_t first = json.find("\"enabled\": true");
    const size_t at = kind == 0 ? first : json.find("\"enabled\": true", first + 1);
    REQUIRE(at != std::string::npos);
    json.insert(at, kind == 3 ? "\"mobility\": \"dynamic\",\n" : "\"mobility\": \"static\",\n");
    const auto path = outputPath("mobility-invalid.scene.gltf");
    writeText(path, json);
    writeBuffer(doc, outputPath("mobility-invalid.scene.bin"));
    const auto read = readSceneDocument(path);
    REQUIRE_FALSE(read);
    CHECK(read.error().message.contains("/nodes/" + std::to_string(kind == 0 ? 0 : 1) +
                                        "/extensions/LMX_scene/mobility"));
}

//======================================================================================================================
TEST_CASE("schema two mobility round trips and override static stays explicit",
          "[asset][scene-document][ux6-mobility]") {
    auto doc = completeDocument();
    doc.schemaVersion = 2;
    doc.nodes[1].mobility = DocMobility::Movable;
    doc.nodes[1].overrides[0].mobility = DocMobility::Static;
    doc.nodes[3].mobility = DocMobility::Movable;
    const auto path = outputPath("mobility-roundtrip.scene.gltf");
    REQUIRE(saveSceneDocument(doc, path));
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    CHECK(read->nodes[0].mobility == DocMobility::Static);
    CHECK(read->nodes[1].mobility == DocMobility::Movable);
    CHECK(read->nodes[1].overrides[0].mobility == DocMobility::Static);
    CHECK(read->nodes[3].mobility == DocMobility::Movable);
    CHECK(sceneDocumentJson(*read, "mobility-roundtrip.scene.bin") == readText(path));
    auto mobilityOnly = doc;
    mobilityOnly.nodes[1].overrides[0].pose.reset();
    mobilityOnly.nodes[1].overrides[0].enabled.reset();
    REQUIRE(saveSceneDocument(mobilityOnly, outputPath("mobility-only.scene.gltf")));
    REQUIRE(readSceneDocument(outputPath("mobility-only.scene.gltf")));
}

//======================================================================================================================
TEST_CASE("schema one migration keeps every authored light movable and objects static",
          "[asset][scene-document][ux6-mobility]") {
    auto doc = completeDocument();
    doc.schemaVersion = 1;
    const auto oldPath = outputPath("mobility-legacy.scene.gltf");
    writeText(oldPath, sceneDocumentJson(doc, "mobility-legacy.scene.bin"));
    writeBuffer(doc, outputPath("mobility-legacy.scene.bin"));
    const auto legacy = readSceneDocument(oldPath);
    REQUIRE(legacy);
    CHECK(legacy->schemaVersion == 1);
    for (const auto& node : legacy->nodes)
        CHECK(node.mobility == (node.light ? DocMobility::Movable : DocMobility::Static));
    const auto newPath = outputPath("mobility-upgraded.scene.gltf");
    REQUIRE(saveSceneDocument(*legacy, newPath));
    const auto upgraded = readSceneDocument(newPath);
    REQUIRE(upgraded);
    CHECK(upgraded->schemaVersion == 2);
    for (size_t i = 0; i < legacy->nodes.size(); ++i)
        CHECK(upgraded->nodes[i].mobility == legacy->nodes[i].mobility);
    CHECK(sceneDocumentBuffer(*upgraded) == sceneDocumentBuffer(*legacy));
    auto expected = *legacy;
    expected.schemaVersion = 2;
    CHECK(sceneDocumentJson(*upgraded, "mobility-upgraded.scene.bin") ==
          sceneDocumentJson(expected, "mobility-upgraded.scene.bin"));
}

//======================================================================================================================
TEST_CASE("legacy save validates mobility before schema migration",
          "[asset][scene-document][ux6-mobility][ux6-mobility-invalid]") {
    auto doc = animatedDocument();
    doc.schemaVersion = 1;
    SECTION("invalid node enum") {
        doc.nodes[0].mobility = static_cast<DocMobility>(255);
    }
    SECTION("movable camera") {
        doc.nodes[0].mobility = DocMobility::Movable;
    }
    SECTION("movable group") {
        doc.nodes.push_back({.name = "Group", .mobility = DocMobility::Movable});
        doc.rootNodes.push_back(1);
    }
    SECTION("movable generator") {
        doc.nodes.push_back({.name = "Generator",
                             .mobility = DocMobility::Movable,
                             .generator = DocGenerator{"visibility-lab", {}}});
        doc.rootNodes.push_back(1);
    }
    SECTION("invalid override enum") {
        doc = completeDocument();
        doc.schemaVersion = 1;
        doc.nodes[1].overrides[0].mobility = static_cast<DocMobility>(255);
    }
    const auto path = outputPath("mobility-invalid-legacy-save.scene.gltf");
    auto bin = path;
    bin.replace_extension(".bin");
    fs::remove(path);
    fs::remove(bin);
    writeText(path, "unchanged target");
    const auto result = saveSceneDocument(doc, path);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.contains("mobility"));
    CHECK(readText(path) == "unchanged target");
    CHECK_FALSE(fs::exists(bin));
    fs::remove(path);
}
