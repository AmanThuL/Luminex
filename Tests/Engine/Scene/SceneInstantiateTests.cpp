#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Scenes/SceneDocuments.h"
#include "Support/EngineTestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using namespace lmx;

namespace {
//======================================================================================================================
asset::SceneDocument fixtureDocument(const std::filesystem::path& root) {
    const auto path = test::writeAnimatedQuadGltf(root, "LINEAR");
    {
        std::ifstream in(path);
        std::string json((std::istreambuf_iterator<char>(in)), {});
        const auto at = json.find("\"nodes\": [{");
        REQUIRE(at != std::string::npos);
        json.insert(at + std::string("\"nodes\": [{").size(), "\"name\": \"Source Node\", ");
        std::ofstream out(path);
        out << json;
    }
    auto bytes = readWholeFile(path);
    REQUIRE(bytes);
    asset::SceneDocument doc;
    doc.name = "Instantiation fixture";
    doc.camera = 0;
    doc.cameras.emplace_back();
    doc.nodes.push_back({.name = "Camera", .camera = 0});
    doc.nodes.push_back(
        {.name = "Asset", .asset = asset::DocAsset{path.filename().string(), sha256Hex(*bytes)}});
    doc.rootNodes = {0, 1};
    return doc;
}
} // namespace

//======================================================================================================================
TEST_CASE("document enabled state ANDs ancestors without rewriting own flags",
          "[scene-doc][instantiate]") {
    asset::SceneDocument doc;
    doc.nodes = {{.name = "Parent", .children = {1}, .enabled = false},
                 {.name = "Child", .children = {2}},
                 {.name = "Leaf"}};
    doc.rootNodes = {0};
    const std::vector<bool> own{false, true, true};
    REQUIRE(engine::effectiveDocumentEnabled(doc, own) == std::vector<bool>{false, false, false});
    REQUIRE(doc.nodes[1].enabled);
    REQUIRE(engine::effectiveDocumentEnabled(doc, std::vector<bool>{true, true, true}) ==
            std::vector<bool>{true, true, true});
}

//======================================================================================================================
TEST_CASE("document preflight rejects stale asset identity with its pointer",
          "[scene-doc][instantiate]") {
    const auto root = std::filesystem::current_path() / "SceneDocuments" / "instantiate-assets";
    std::filesystem::create_directories(root);
    auto doc = fixtureDocument(root);
    doc.nodes[1].asset->sha256 = std::string(64, '0');
    const auto result = engine::prepareSceneDocument(doc, root);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/extensions/LMX_scene/asset/sha256") !=
            std::string::npos);
}

//======================================================================================================================
TEST_CASE("document overrides check source names and reject animated poses",
          "[scene-doc][instantiate]") {
    const auto root = std::filesystem::current_path() / "SceneDocuments" / "instantiate-overrides";
    std::filesystem::create_directories(root);
    auto doc = fixtureDocument(root);
    const auto decoded = asset::loadGltf((root / doc.nodes[1].asset->uri).string());
    REQUIRE(decoded);
    const auto name = decoded->nodes[0].name;
    doc.nodes[1].overrides.push_back({.node = 0, .name = "wrong-name"});
    auto result = engine::prepareSceneDocument(doc, root);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("wrong-name") != std::string::npos);
    REQUIRE(result.error().message.find(name) != std::string::npos);
    doc.nodes[1].overrides[0].name = name;
    doc.nodes[1].overrides[0].pose.emplace();
    result = engine::prepareSceneDocument(doc, root);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/extensions/LMX_scene/overrides/0/pose") !=
            std::string::npos);
    REQUIRE(result.error().message.find("animated") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document references resolve beneath Assets regardless of document location",
          "[scene-doc][instantiate]") {
    const auto root =
        std::filesystem::current_path() / "SceneDocuments" / "assets with %20 literal";
    std::filesystem::create_directories(root);
    auto doc = fixtureDocument(root);
    const auto oldPath = root / doc.nodes[1].asset->uri;
    doc.nodes[1].asset->uri = "literal%20 asset.gltf";
    std::filesystem::rename(oldPath, root / doc.nodes[1].asset->uri);
    const auto result = engine::prepareSceneDocument(doc, root);
    INFO((result ? "ok" : result.error().message));
    REQUIRE(result);
    REQUIRE(result->assets[1]->path == root / doc.nodes[1].asset->uri);
    REQUIRE(result->assets[1]->source.clips.size() == 1);
    doc.look.environment.hdri =
        asset::SceneLook::Hdri{.uri = "missing.hdr", .sha256 = std::string(64, '0')};
    const auto missing = engine::prepareSceneDocument(doc, root);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().message.find("/extensions/LMX_scene/look/environment/hdri/uri") !=
            std::string::npos);
}

//======================================================================================================================
TEST_CASE("document light conversion rejects overflow before GPU creation",
          "[scene-doc][instantiate]") {
    asset::SceneDocument doc;
    doc.nodes = {{.name = "Camera", .camera = 0}, {.name = "Light", .light = 0}};
    doc.cameras.emplace_back();
    doc.lights.push_back({.type = asset::DocLightType::Point, .intensity = 1e100, .range = 1.0f});
    doc.rootNodes = {0, 1};
    const auto result = engine::prepareSceneDocument(doc, std::filesystem::current_path());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/extensions/KHR_lights_punctual/lights/0/intensity") !=
            std::string::npos);
}

//======================================================================================================================
TEST_CASE(
    "runtime preflight rejects unsupported document animation targets and transformed generators",
    "[scene-doc][instantiate]") {
    asset::SceneDocument doc;
    doc.cameras.emplace_back();
    doc.nodes = {{.name = "Camera", .camera = 0},
                 {.name = "Generator", .generator = asset::DocGenerator{.name = "temporal-lab"}}};
    doc.rootNodes = {0, 1};
    doc.animations.push_back(
        {.name = "Unsupported",
         .keyCount = 1,
         .channels = {
             {.node = 0, .path = asset::DocChannelPath::Scale, .values = {glm::vec4(1)}}}});
    auto result = engine::prepareSceneDocument(doc, std::filesystem::current_path());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/animations/0/channels/0/target/path") !=
            std::string::npos);
    doc.animations[0].channels[0].node = 1;
    doc.animations[0].channels[0].path = asset::DocChannelPath::Translation;
    result = engine::prepareSceneDocument(doc, std::filesystem::current_path());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/animations/0/channels/0/target/node") !=
            std::string::npos);
    doc.animations.clear();
    doc.nodes[1].translation.x = 1;
    result = engine::prepareSceneDocument(doc, std::filesystem::current_path());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/translation") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("camera rail decoding uses prior decoded yaw and double sample times",
          "[scene-doc][instantiate]") {
    auto document = lmx::scenes::readCatalogDocument("sponza");
    REQUIRE(document);
    engine::Scene scene;
    engine::applyDocumentCamera(scene, *document);
    const auto& animation = document->animations[0];
    const asset::DocChannel* rotations = nullptr;
    for (const auto& channel : animation.channels)
        if (channel.path == asset::DocChannelPath::Rotation)
            rotations = &channel;
    REQUIRE(rotations);
    float previous = 0;
    for (uint32_t i = 0; i < animation.keyCount; ++i) {
        const auto q = rotations->values[i];
        const auto angles = asset::cameraAnglesForRotation({q.w, q.x, q.y, q.z}, previous);
        previous = angles.x;
        REQUIRE(scene.animation.cameraTrack[i].time == double(i) / animation.sampleRate);
        REQUIRE(scene.animation.cameraTrack[i].yaw == angles.x);
        REQUIRE(scene.animation.cameraTrack[i].pitch == angles.y);
    }
}

//======================================================================================================================
TEST_CASE("source own-enabled overrides survive ancestor suppression", "[scene-doc][instantiate]") {
    const auto root = std::filesystem::current_path() / "SceneDocuments" / "source-enabled";
    std::filesystem::create_directories(root);
    const auto path = test::writeAnimatedQuadGltf(root, "LINEAR", true);
    const auto bytes = readWholeFile(path);
    REQUIRE(bytes);
    asset::SceneDocument doc;
    doc.nodes = {
        {.name = "Asset", .asset = asset::DocAsset{path.filename().string(), sha256Hex(*bytes)}}};
    doc.rootNodes = {0};
    doc.nodes[0].overrides = {{.node = 0, .name = "", .enabled = false},
                              {.node = 1, .name = "", .enabled = true}};
    auto prepared = engine::prepareSceneDocument(doc, root);
    REQUIRE(prepared);
    REQUIRE_FALSE(prepared->assets[0]->enabled[0]);
    REQUIRE(prepared->assets[0]->enabled[1]);
    REQUIRE_FALSE(prepared->assets[0]->effective[1]);
    doc.nodes[0].overrides[0].enabled = true;
    prepared = engine::prepareSceneDocument(doc, root);
    REQUIRE(prepared);
    REQUIRE(prepared->assets[0]->effective[1]);
}

//======================================================================================================================
TEST_CASE("separate document camera clips compose channels in source order",
          "[scene-doc][instantiate]") {
    asset::SceneDocument doc;
    doc.cameras.emplace_back();
    doc.nodes.push_back({.name = "Camera", .camera = 0});
    doc.rootNodes = {0};
    doc.animations = {{.name = "Position",
                       .sampleRate = 1,
                       .keyCount = 3,
                       .channels = {{.node = 0,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, 0, 0, 0}}}}},
                      {.name = "Rotation",
                       .sampleRate = 2,
                       .keyCount = 3,
                       .channels = {{.node = 0,
                                     .path = asset::DocChannelPath::Rotation,
                                     .values = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}}}}}};
    engine::Scene scene;
    engine::applyDocumentCamera(scene, doc);
    REQUIRE(scene.animation.duration == 2);
    REQUIRE(scene.animation.cameraTrack.size() == 4);
    REQUIRE(scene.animation.cameraTrack[1].time == 0.5);
    REQUIRE(scene.animation.cameraTrack[1].position.x == 0.5f);
    REQUIRE(scene.animation.cameraTrack.back().position.x == 2);
}
