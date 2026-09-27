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
TEST_CASE("preflight rejects independent phases that can shear an imported child",
          "[scene-doc][instantiate][ux3]") {
    asset::GltfScene source;
    source.nodes.resize(2);
    source.nodes[0].name = "Scaling parent";
    source.nodes[1].name = "Rotating child";
    source.nodes[1].parent = 0;
    source.nodes[1].animated = true;
    source.nodes[1].instances = {0};
    source.instances.push_back({.node = 1});
    source.clips = {{.name = "scale",
                     .duration = 2.0,
                     .channels = {{.node = 0,
                                   .path = asset::GltfAnimationPath::Scale,
                                   .step = true,
                                   .keys = {{.time = 0, .value = {1, 1, 1, 0}},
                                            {.time = .5, .value = {2, 1, 1, 0}},
                                            {.time = 1, .value = {1, 1, 1, 0}},
                                            {.time = 2, .value = {1, 1, 1, 0}}}}}},
                    {.name = "rotation",
                     .duration = 4.0,
                     .channels = {{.node = 1,
                                   .path = asset::GltfAnimationPath::Rotation,
                                   .step = true,
                                   .keys = {{.time = 0, .value = {0, 0, 0, 1}},
                                            {.time = 2, .value = {0, 0, .3826834f, .9238795f}},
                                            {.time = 4, .value = {0, 0, 0, 1}}}}}}};
    const glm::mat4 independentPose = glm::scale(glm::mat4(1), glm::vec3(2, 1, 1)) *
                                      glm::mat4_cast(glm::quat(.9238795f, 0, 0, .3826834f));
    REQUIRE_FALSE(decomposeTransform(independentPose));
    auto result = engine::validateIndependentAssetClips(source, glm::mat4(1),
                                                        "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/extensions/LMX_scene/asset") !=
            std::string::npos);
    REQUIRE(result.error().message.find("Scaling parent") != std::string::npos);
    REQUIRE(result.error().message.find("Rotating child") != std::string::npos);

    source.clips[0].channels[0].path = asset::GltfAnimationPath::Translation;
    result = engine::validateIndependentAssetClips(source, glm::mat4(1),
                                                   "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE(result);
}

//======================================================================================================================
TEST_CASE("preflight rejects decoupled rotations beneath a static nonuniform root",
          "[scene-doc][instantiate][ux3]") {
    asset::GltfScene source;
    source.nodes.resize(2);
    source.nodes[0].name = "Empty rotating parent";
    source.nodes[1].name = "Mesh rotating child";
    source.nodes[1].parent = 0;
    source.nodes[0].animated = true;
    source.nodes[1].animated = true;
    source.nodes[1].instances = {0};
    source.instances.push_back({.node = 1});
    const glm::vec4 plus45{0, 0, .3826834f, .9238795f};
    const glm::vec4 minus45{0, 0, -.3826834f, .9238795f};
    const glm::vec4 identity{0, 0, 0, 1};
    source.clips = {{.name = "parent",
                     .duration = 2,
                     .channels = {{.node = 0,
                                   .path = asset::GltfAnimationPath::Rotation,
                                   .step = true,
                                   .keys = {{.time = 0, .value = identity},
                                            {.time = .5, .value = plus45},
                                            {.time = 1, .value = identity},
                                            {.time = 2, .value = identity}}}}},
                    {.name = "child",
                     .duration = 4,
                     .channels = {{.node = 1,
                                   .path = asset::GltfAnimationPath::Rotation,
                                   .step = true,
                                   .keys = {{.time = 0, .value = identity},
                                            {.time = .5, .value = minus45},
                                            {.time = 1, .value = identity},
                                            {.time = 4, .value = identity}}}}}};
    const glm::mat4 rootWorld = glm::scale(glm::mat4(1), glm::vec3(2, 1, 1));
    const glm::mat4 sharedPose =
        rootWorld * glm::mat4_cast(glm::quat(plus45.w, plus45.x, plus45.y, plus45.z)) *
        glm::mat4_cast(glm::quat(minus45.w, minus45.x, minus45.y, minus45.z));
    REQUIRE(decomposeTransform(sharedPose));
    const glm::mat4 decoupledPose =
        rootWorld * glm::mat4_cast(glm::quat(plus45.w, plus45.x, plus45.y, plus45.z));
    REQUIRE_FALSE(decomposeTransform(decoupledPose));
    const auto result = engine::validateIndependentAssetClips(
        source, rootWorld, "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/extensions/LMX_scene/asset") !=
            std::string::npos);
    REQUIRE(result.error().message.find("Empty rotating parent") != std::string::npos);
    REQUIRE(result.error().message.find("Mesh rotating child") != std::string::npos);

    source.nodes.push_back({.name = "Static scaled ancestor", .scale = {2, 1, 1}});
    source.nodes[0].parent = 2;
    const auto restScale = engine::validateIndependentAssetClips(
        source, glm::mat4(1), "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE_FALSE(restScale);
    REQUIRE(restScale.error().message.find("Mesh rotating child") != std::string::npos);
    source.nodes[2].scale = glm::vec3(1);
    source.nodes[2].matrix = glm::scale(glm::mat4(1), glm::vec3(2, 1, 1));
    const auto restMatrix = engine::validateIndependentAssetClips(
        source, glm::mat4(1), "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE_FALSE(restMatrix);
}

//======================================================================================================================
TEST_CASE("preflight rejects shear between baked samples of one short clip",
          "[scene-doc][instantiate][ux3]") {
    asset::GltfScene source;
    source.nodes.resize(2);
    source.nodes[0].name = "Scaling parent";
    source.nodes[1].name = "Rotating child";
    source.nodes[1].parent = 0;
    source.nodes[1].instances = {0};
    source.instances.push_back({.node = 1});
    source.clips = {
        {.name = "short",
         .duration = .025,
         .channels = {{.node = 0,
                       .path = asset::GltfAnimationPath::Scale,
                       .keys = {{.time = 0, .value = {2, 1, 1, 0}},
                                {.time = 1.0 / 60.0, .value = {1, 1, 1, 0}},
                                {.time = .025, .value = {1, 1, 1, 0}}}},
                      {.node = 1,
                       .path = asset::GltfAnimationPath::Rotation,
                       .keys = {{.time = 0, .value = {0, 0, 0, 1}},
                                {.time = 1.0 / 60.0, .value = {0, 0, .3826834f, .9238795f}},
                                {.time = .025, .value = {0, 0, .3826834f, .9238795f}}}}}}};
    const glm::mat4 betweenBakeKeys =
        glm::scale(glm::mat4(1), glm::vec3(1.5f, 1, 1)) *
        glm::mat4_cast(glm::angleAxis(glm::radians(22.5f), glm::vec3(0, 0, 1)));
    REQUIRE_FALSE(decomposeTransform(betweenBakeKeys));
    const auto result = engine::validateIndependentAssetClips(
        source, glm::mat4(1), "/nodes/1/extensions/LMX_scene/asset");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("Scaling parent") != std::string::npos);
    REQUIRE(result.error().message.find("Rotating child") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("structural clip preflight preserves the two catalog asset stations",
          "[scene-doc][instantiate][ux3]") {
    const auto assets = std::filesystem::path(LMX_REPO_ROOT) / "Assets";
    for (const char* id : {"material-lab", "temporal-lab"}) {
        const auto document = scenes::readCatalogDocument(id);
        REQUIRE(document);
        const auto prepared = engine::prepareSceneDocument(*document, assets);
        INFO((prepared ? "ok" : prepared.error().message));
        REQUIRE(prepared);
    }
}

//======================================================================================================================
TEST_CASE("preflight rejects a scale segment that crosses two zero axes between bake keys",
          "[scene-doc][instantiate][ux3]") {
    asset::GltfScene source;
    source.nodes.resize(1);
    source.nodes[0].name = "Collapsing draw";
    source.nodes[0].instances = {0};
    source.instances.push_back({.node = 0});
    source.clips = {{.duration = .025,
                     .channels = {{.node = 0,
                                   .path = asset::GltfAnimationPath::Scale,
                                   .keys = {{.time = 0, .value = {1, 1, 1, 0}},
                                            {.time = 1.0 / 60.0, .value = {-1, -1, 1, 0}},
                                            {.time = .025, .value = {-1, -1, 1, 0}}}}}}};
    REQUIRE_FALSE(decomposeTransform(glm::scale(glm::mat4(1), glm::vec3(0, 0, 1))));
    const auto result = engine::validateIndependentAssetClips(
        source, glm::mat4(1), "/nodes/2/extensions/LMX_scene/asset");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("Collapsing draw") != std::string::npos);
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
