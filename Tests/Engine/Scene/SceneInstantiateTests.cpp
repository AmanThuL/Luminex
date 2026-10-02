#include "Core/IO/File.h"
#include "Core/Math/Sphere.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Texture/TextureBake.h"
#include "Engine/Scene/SceneDocumentContent.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Scenes/SceneDocuments.h"
#include "Support/EngineTestSupport.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentFixtures.h"

#include <rojoRHI/RHI.h>

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace lmx;

namespace {
//======================================================================================================================
template <class T>
bool sameContentBits(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

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

//======================================================================================================================
TEST_CASE("document pose overrides require a source node with primitives",
          "[scene-doc][instantiate]") {
    const auto root = std::filesystem::current_path() / "SceneDocuments" / "instantiate-empty-pose";
    std::filesystem::create_directories(root);
    auto doc = fixtureDocument(root);
    const auto gltf = root / doc.nodes[1].asset->uri;
    {
        std::ifstream in(gltf);
        std::string json((std::istreambuf_iterator<char>(in)), {});
        const auto replace = [&](std::string_view from, std::string_view to) {
            const auto at = json.find(from);
            REQUIRE(at != std::string::npos);
            json.replace(at, from.size(), to);
        };
        replace(R"("scenes": [{"nodes": [0]}])", R"("scenes": [{"nodes": [0, 1]}])");
        replace(R"("translation": [10.0, 0.0, 0.0]}])",
                R"("translation": [10.0, 0.0, 0.0]}, {"name": "Empty"}])");
        std::ofstream out(gltf);
        out << json;
    }
    auto bytes = readWholeFile(gltf);
    REQUIRE(bytes);
    doc.nodes[1].asset->sha256 = sha256Hex(*bytes);
    doc.nodes[1].overrides.push_back({.node = 1, .name = "Empty", .pose = asset::ObjectPose{}});
    const auto result = engine::prepareSceneDocument(doc, root);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/1/extensions/LMX_scene/overrides/0/pose") !=
            std::string::npos);
    REQUIRE(result.error().message.find("pose override requires a mesh node") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("asset clip validation rejects STEP scale keys that collapse two axes",
          "[scene-doc][instantiate]") {
    asset::GltfScene source;
    source.nodes.resize(1);
    source.instances.push_back({.node = 0});
    const auto scaleClip = [](glm::vec4 value) {
        return asset::GltfAnimationClip{.duration = 1.0,
                                        .channels = {{.node = 0,
                                                      .path = asset::GltfAnimationPath::Scale,
                                                      .step = true,
                                                      .keys = {{.time = 0.0, .value = {1, 1, 1, 0}},
                                                               {.time = 1.0, .value = value}}}}};
    };
    source.clips = {scaleClip({1, 1, 1, 0})};
    REQUIRE(engine::validateIndependentAssetClips(source, glm::mat4(1.0f), "/asset"));
    source.clips = {scaleClip({0, 0, 1, 0})};
    const auto result = engine::validateIndependentAssetClips(source, glm::mat4(1.0f), "/asset");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/asset") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content preflight rejects unsupported mesh ownership and ancestry",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    std::string pointer;
    SECTION("missing decoded content") {
        doc.content.reset();
        pointer = "/meshes";
    }
    SECTION("asset on mesh") {
        doc.nodes[1].asset = asset::DocAsset{"missing.gltf", std::string(64, 'a')};
        pointer = "/nodes/1/mesh";
    }
    SECTION("generator on mesh") {
        doc.nodes[1].generator = asset::DocGenerator{"test", {}};
        pointer = "/nodes/1/mesh";
    }
    SECTION("camera on mesh") {
        doc.nodes[1].camera = 0;
        pointer = "/nodes/1/mesh";
    }
    SECTION("light on mesh") {
        doc.lights.emplace_back();
        doc.nodes[1].light = 0;
        pointer = "/nodes/1/mesh";
    }
    SECTION("mesh ancestor") {
        doc.nodes[2].children = {1};
        doc.rootNodes = {0, 2};
        pointer = "/nodes/2";
    }
    SECTION("asset ancestor") {
        doc.nodes.push_back(
            {.children = {1}, .asset = asset::DocAsset{"missing.gltf", std::string(64, 'a')}});
        doc.rootNodes = {0, 2, 3};
        pointer = "/nodes/3";
    }
    SECTION("generator ancestor") {
        doc.nodes.push_back({.children = {1}, .generator = asset::DocGenerator{"test", {}}});
        doc.rootNodes = {0, 2, 3};
        pointer = "/nodes/3";
    }
    SECTION("light ancestor") {
        doc.lights.emplace_back();
        doc.nodes.push_back({.children = {1}, .light = 0});
        doc.rootNodes = {0, 2, 3};
        pointer = "/nodes/3";
    }
    SECTION("camera ancestor") {
        doc.nodes[0].children = {1};
        doc.rootNodes = {0, 2};
        pointer = "/nodes/0";
    }
    SECTION("transformed group ancestor") {
        doc.nodes.push_back({.name = "Group", .children = {1}});
        doc.rootNodes = {0, 2, 3};
        SECTION("translation") {
            doc.nodes[3].translation.x = 1;
            pointer = "/nodes/3/translation";
        }
        SECTION("rotation") {
            doc.nodes[3].rotation = glm::angleAxis(0.5f, glm::vec3(0, 1, 0));
            pointer = "/nodes/3/rotation";
        }
        SECTION("scale") {
            doc.nodes[3].scale.y = 2;
            pointer = "/nodes/3/scale";
        }
    }
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find(pointer) != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content accepts decoded payload and identity groups without disk access",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    auto decoded = std::make_shared<asset::DocContent>(*doc.content);
    decoded->geometrySha256.clear();
    for (auto& image : decoded->images) {
        image.file.clear();
        image.sha256.clear();
    }
    doc.content = std::move(decoded);
    doc.nodes.push_back({.name = "Hidden group", .children = {1}, .enabled = false});
    doc.rootNodes = {0, 2, 3};
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "ok" : prepared.error().message));
    REQUIRE(prepared);
    REQUIRE_FALSE(prepared->enabled[1]);
    REQUIRE(doc.nodes[1].enabled);
}

//======================================================================================================================
TEST_CASE("document content channels do not contaminate the selected camera",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    doc.animations = {{.name = "Emission",
                       .keyCount = 2,
                       .channels = {{.path = asset::DocChannelPath::EmissiveStrength,
                                     .material = 0,
                                     .step = true,
                                     .values = {{2, 0, 0, 0}, {3, 0, 0, 0}}}}}};
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "ok" : prepared.error().message));
    REQUIRE(prepared);
    engine::Scene scene;
    engine::applyDocumentCamera(scene, doc);
    REQUIRE(scene.animation.cameraTrack.empty());
    REQUIRE(scene.initialCamera.position == doc.nodes[0].translation);
    REQUIRE(scene.animation.duration == 1.0 / 60.0);
}

//======================================================================================================================
TEST_CASE("document meshes instantiate with shared resources and authored bounds",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    doc.nodes[1].rotation = glm::quat(.5f, .5f, .5f, .5f);
    doc.nodes[1].translation = {-0.f, 2.f, 3.f};
    doc.nodes[1].scale = {-2.f, 3.f, .125f};
    const auto path = std::filesystem::current_path() / "SceneDocuments" / "ux6-content.scene.gltf";
    const auto saved = asset::saveSceneDocument(doc, path);
    INFO((saved ? "saved" : saved.error().message));
    REQUIRE(saved);
    auto loaded = engine::instantiateSceneDocument(**device, doc, path, {});
    INFO((loaded ? "loaded" : loaded.error().message));
    REQUIRE(loaded);
    const auto& scene = *loaded->scene;
    REQUIRE(scene.objects.size() == 2);
    REQUIRE(scene.objects[0].mesh == scene.objects[1].mesh);
    REQUIRE(scene.objects[0].material != scene.objects[1].material);
    for (size_t i = 0; i < 2; ++i) {
        const auto& object = scene.objects[i];
        const auto& node = doc.nodes[i + 1];
        REQUIRE(object.name == node.name);
        REQUIRE(sameContentBits(object.position, node.translation));
        REQUIRE(
            sameContentBits(object.eulerDegrees, asset::eulerDegreesForRotation(node.rotation)));
        REQUIRE(sameContentBits(object.scale, node.scale));
        REQUIRE(object.enabled == node.enabled);
        REQUIRE(loaded->binding.objectNode[i] == i + 1);
        REQUIRE(loaded->binding.objectImportedNode[i] == engine::kGeneratedNode);
        REQUIRE(loaded->binding.objectGeneratorNode[i] == engine::kGeneratedNode);
        REQUIRE(loaded->binding.nodes[i + 1].objects == std::vector<size_t>{i});
    }
    REQUIRE(scene.objects[0].motionClass == engine::MotionClass::Rigid);
    REQUIRE(scene.objects[1].motionClass == engine::MotionClass::Invalid);
    REQUIRE(sameContentBits(scene.authoredBounds.minimum, doc.bounds->first));
    REQUIRE(sameContentBits(scene.authoredBounds.maximum, doc.bounds->second));
    const auto& color = scene.material(scene.objects[0].material);
    const auto& data = scene.material(scene.objects[1].material);
    REQUIRE(sameContentBits(color.albedo, doc.materials[0].values.baseColorFactor));
    REQUIRE(sameContentBits(color.emissive, doc.materials[0].values.emissiveFactor));
    REQUIRE(sameContentBits(color.roughness, doc.materials[0].values.roughness));
    REQUIRE(sameContentBits(color.metallic, doc.materials[0].values.metallic));
    REQUIRE(sameContentBits(data.occlusionStrength, doc.materials[1].values.occlusionStrength));
    REQUIRE(sameContentBits(data.alphaCutoff, doc.materials[1].values.alphaCutoff));
    REQUIRE(data.alphaMode == engine::AlphaMode::Mask);
    REQUIRE(data.doubleSided);
    REQUIRE(sameContentBits(scene.objects[0].emissiveStrength, doc.materials[0].emissiveStrength));
    REQUIRE(color.diffuse == color.emissiveMap);
    REQUIRE(scene.tryTexture(*color.diffuse)->mipLevels() == 3);
    REQUIRE(scene.tryTexture(*color.diffuse)->format() == rojoRHI::Format::RGBA8Unorm_sRGB);
    REQUIRE(data.normalMap == data.metallicRoughness);
    REQUIRE(data.normalMap == data.occlusion);
    REQUIRE(scene.tryTexture(*data.normalMap)->mipLevels() == 1);
    REQUIRE(scene.tryTexture(*data.normalMap)->format() == rojoRHI::Format::RGBA8Unorm);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document content preflight checks track representation and texture uses",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    doc.animations = {{.keyCount = 2,
                       .channels = {{.node = 1,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{1, 2, 3, 0}, {4, 5, 6, 0}}}}}};
    std::string pointer;
    SECTION("animated mesh beneath identity group") {
        doc.nodes.push_back({.children = {1}});
        doc.rootNodes = {0, 2, 3};
        pointer = "/animations/0/channels/0/target/node";
    }
    SECTION("unsupported group target") {
        doc.nodes.push_back({.name = "Group"});
        doc.rootNodes.push_back(3);
        doc.animations[0].channels[0].node = 3;
        pointer = "/animations/0/channels/0/target/node";
    }
    SECTION("mixed interpolation") {
        doc.animations[0].channels.push_back({.node = 1,
                                              .path = asset::DocChannelPath::Scale,
                                              .step = true,
                                              .values = {{1, 1, 1, 0}, {2, 2, 2, 0}}});
        pointer = "/animations/0/channels/1/sampler";
    }
    SECTION("different sample rate") {
        doc.animations.push_back({.sampleRate = 30,
                                  .keyCount = 2,
                                  .channels = {{.node = 1,
                                                .path = asset::DocChannelPath::Scale,
                                                .values = {{1, 1, 1, 0}, {2, 2, 2, 0}}}}});
        pointer = "/animations/1/channels/0/sampler";
    }
    SECTION("different key count") {
        doc.animations.push_back(
            {.keyCount = 1,
             .channels = {
                 {.node = 1, .path = asset::DocChannelPath::Scale, .values = {{1, 1, 1, 0}}}}});
        pointer = "/animations/1/channels/0/sampler";
    }
    SECTION("one image in color and data slots") {
        doc.materials[1].values.normalImage = 0;
        pointer = "/materials/1/normalTexture";
    }
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find(pointer) != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content append copies rigid and emissive keys and expands authored bounds",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    doc.nodes[1].rotation = glm::quat(0.99999f, -0.0f, 0.0f, 0.0f);
    doc.nodes[1].scale = {1.25f, 2.5f, 0.75f};
    doc.nodes.push_back({.name = "Shared emissive", .mesh = 0});
    doc.rootNodes.push_back(3);
    doc.animations = {
        {.keyCount = 3,
         .channels = {
             {.node = 1,
              .path = asset::DocChannelPath::Translation,
              .values = {{-0.f, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}}},
             {.node = 1,
              .path = asset::DocChannelPath::Rotation,
              .values = {{-0.f, 0, 0, .99999f}, {0, .1f, 0, .9949874f}, {0, -.1f, 0, .9949874f}}},
             {.node = 1,
              .path = asset::DocChannelPath::Scale,
              .values = {{1, 2, 3, 0}, {2, 3, 4, 0}, {3, 4, 5, 0}}},
             {.path = asset::DocChannelPath::EmissiveStrength,
              .material = 0,
              .step = true,
              .values = {{.25f, 0, 0, 0}, {8, 0, 0, 0}, {0, 0, 0, 0}}}}}};
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "ok" : prepared.error().message));
    REQUIRE(prepared);
    engine::Scene scene;
    scene.authoredBounds = {glm::vec3(-20, -1, -1), glm::vec3(-10, 1, 1)};
    engine::DocumentContentBinding binding;
    const std::array<uint32_t, 3> nodes{1, 2, 3};
    const auto appended = engine::appendDocumentContent(**device, scene, doc, nodes, binding);
    INFO((appended ? "ok" : appended.error().message));
    REQUIRE(appended);
    REQUIRE(binding.objectOfNode == std::vector<uint32_t>{engine::kGeneratedNode, 0, 1, 2});
    REQUIRE(sameContentBits(scene.objects[0].position, doc.nodes[1].translation));
    REQUIRE(sameContentBits(scene.objects[0].scale, doc.nodes[1].scale));
    REQUIRE(sameContentBits(scene.objects[0].eulerDegrees,
                            asset::eulerDegreesForRotation(doc.nodes[1].rotation)));
    REQUIRE(scene.objects[0].material == scene.objects[2].material);
    REQUIRE(scene.animation.tracks.size() == 1);
    const auto& track = scene.animation.tracks[0];
    REQUIRE(track.objectIndex == 0);
    REQUIRE_FALSE(track.step);
    REQUIRE(track.loopDuration == 0);
    REQUIRE(track.keys.size() == 3);
    REQUIRE(scene.animation.emissiveTracks.size() == 2);
    for (size_t k = 0; k < 3; ++k) {
        const auto& channels = doc.animations[0].channels;
        const auto& rotation = channels[1].values[k];
        REQUIRE(sameContentBits(track.keys[k].time, double(k) / 60.0));
        REQUIRE(sameContentBits(track.keys[k].translation, glm::vec3(channels[0].values[k])));
        REQUIRE(sameContentBits(track.keys[k].rotation,
                                glm::quat(rotation.w, rotation.x, rotation.y, rotation.z)));
        REQUIRE(sameContentBits(track.keys[k].scale, glm::vec3(channels[2].values[k])));
        for (size_t t = 0; t < 2; ++t) {
            const auto& emission = scene.animation.emissiveTracks[t];
            REQUIRE(emission.objectIndex == t * 2);
            REQUIRE(emission.keys.size() == 3);
            REQUIRE(sameContentBits(emission.keys[k].time, double(k) / 60.0));
            REQUIRE(sameContentBits(emission.keys[k].strength, channels[3].values[k].x));
        }
    }
    REQUIRE(sameContentBits(scene.authoredBounds.minimum, glm::vec3(-20, -3, -4)));
    REQUIRE(sameContentBits(scene.authoredBounds.maximum, glm::vec3(5, 6, 7)));
    REQUIRE(sameContentBits(scene.boundingSphere, toVec4(boundingSphere(scene.authoredBounds))));
    REQUIRE(scene.finalize(**device));
    REQUIRE(scene.tableStats().meshCount == 1);
    REQUIRE(scene.tableStats().materialCount == 2);
    REQUIRE(scene.tableStats().vertexBytes == doc.content->geometries[0].vertices.size() * 48);
    REQUIRE(scene.tableStats().indexBytes == doc.content->geometries[0].indices.size() * 4);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document content preserves object and resource order across a generator",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    doc.nodes.insert(doc.nodes.begin() + 2,
                     {.name = "Between", .generator = asset::DocGenerator{"test", {}}});
    doc.rootNodes = {0, 1, 2, 3};
    const auto path = std::filesystem::current_path() / "SceneDocuments" / "ux6-order.scene.gltf";
    REQUIRE(asset::saveSceneDocument(doc, path));
    const engine::SceneGenerator generator =
        [](engine::Scene& scene, const asset::DocGenerator&,
           const engine::EnvironmentHook& environment) -> asset::AssetResult<void> {
        REQUIRE(scene.objects.size() == 1);
        REQUIRE(scene.objects[0].mesh.slot == 0);
        // The content run has uploaded both materials before environment creates the sky mesh.
        REQUIRE(scene.material(scene.objects[0].material).diffuse->slot == 0);
        REQUIRE(environment(scene));
        REQUIRE(scene.skySphere);
        REQUIRE(scene.skySphere->slot == 1);
        const auto mesh = scene.addMesh(engine::makeCube(), "between");
        const auto material = scene.addMaterial({});
        REQUIRE(mesh.slot == 2);
        REQUIRE(material.slot == 2);
        scene.addObject({.name = "Generated middle", .mesh = mesh, .material = material});
        return {};
    };
    const auto loaded =
        engine::instantiateSceneDocument(**device, doc, path, [&](std::string_view name) {
            return name == "test" ? &generator : nullptr;
        });
    INFO((loaded ? "ok" : loaded.error().message));
    REQUIRE(loaded);
    const auto& scene = *loaded->scene;
    REQUIRE(scene.objects.size() == 3);
    REQUIRE(scene.objects[0].name == "First cube");
    REQUIRE(scene.objects[1].name == "Generated middle");
    REQUIRE(scene.objects[2].name == "Second cube");
    REQUIRE(scene.objects[2].mesh == scene.objects[0].mesh);
    REQUIRE(scene.objects[2].material.slot == 1);
    REQUIRE(loaded->binding.objectNode == std::vector<uint32_t>{1, engine::kGeneratedNode, 3});
    REQUIRE(loaded->binding.objectGeneratorNode ==
            std::vector<uint32_t>{engine::kGeneratedNode, 2, engine::kGeneratedNode});
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document content mip filtering follows each image color space",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    for (const bool srgb : {true, false}) {
        auto doc = test::contentDocument();
        auto content = std::make_shared<asset::DocContent>(*doc.content);
        content->images[1] = content->images[0];
        doc.content = content;
        engine::Scene scene;
        engine::DocumentContentBinding binding;
        const std::array<uint32_t, 2> nodes{1, 2};
        REQUIRE(engine::appendDocumentContent(**device, scene, doc, nodes, binding));
        const auto& material = scene.material(scene.objects[srgb ? 0 : 1].material);
        auto* texture = scene.tryTexture(*(srgb ? material.diffuse : material.normalMap));
        REQUIRE(texture->mipLevels() == 3);
        const auto& pixels = content->images[0];
        const auto bytes =
            std::span(reinterpret_cast<const uint8_t*>(pixels.rgba8.data()), pixels.rgba8.size());
        const auto baked =
            asset::bakeMips(bytes, 4, 4, srgb ? asset::BakeMode::Srgb : asset::BakeMode::Linear);
        auto expected = (*device)->createTexture(
            {.width = 4,
             .height = 4,
             .format = srgb ? rojoRHI::Format::RGBA8Unorm_sRGB : rojoRHI::Format::RGBA8Unorm,
             .mipLevels = 3,
             .sampled = true,
             .label = "expected document mips"},
            baked.mips);
        REQUIRE(expected);
        auto destination = makeProbeTarget(**device, "document mip probe");
        auto library = (*device)->loadShaderLibrary("Shaders/SamplerSmoke");
        REQUIRE(destination);
        REQUIRE(library);
        auto pipeline =
            (*device)->createGraphicsPipeline({.library = library->get(),
                                               .vertexEntry = "vertexMain",
                                               .fragmentEntry = "fragmentMipLevel1",
                                               .colorFormat = rojoRHI::Format::BGRA8Unorm,
                                               .label = "document mip probe"});
        auto sampler = (*device)->createSampler({.addressMode = rojoRHI::AddressMode::Clamp});
        REQUIRE(pipeline);
        REQUIRE(sampler);
        const auto actual =
            renderSampledImage(**device, **pipeline, 0, *texture, **sampler, **destination);
        const auto reference =
            renderSampledImage(**device, **pipeline, 0, **expected, **sampler, **destination);
        REQUIRE(actual == reference);
    }
}

//======================================================================================================================
TEST_CASE("document content preflight rejects rigid scale collapse before playback",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    doc.animations = {{.keyCount = 2,
                       .channels = {{.node = 1,
                                     .path = asset::DocChannelPath::Scale,
                                     .values = {{1, 1, 1, 0}, {-1, -1, 1, 0}}}}}};
    SECTION("linear collapse between keys") {}
    SECTION("step collapse at a key") {
        doc.animations[0].channels[0].step = true;
        doc.animations[0].channels[0].values[1] = {0, 0, 1, 0};
    }
    SECTION("translation retains a collapsed rest scale") {
        doc.animations[0].channels[0].path = asset::DocChannelPath::Translation;
        doc.nodes[1].scale = {0, 0, 1};
    }
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find("/animations/0/channels/0") != std::string::npos);
    REQUIRE(prepared.error().message.find("two axes") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content refuses repeated mesh and emissive targets across animations",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    asset::DocChannel channel{.node = 1,
                              .path = asset::DocChannelPath::Translation,
                              .values = {{0, 0, 0, 0}, {1, 2, 3, 0}}};
    SECTION("repeated mesh path") {}
    SECTION("repeated emissive material") {
        channel.path = asset::DocChannelPath::EmissiveStrength;
        channel.material = 0;
        channel.step = true;
        channel.values = {{1, 0, 0, 0}, {2, 0, 0, 0}};
    }
    doc.animations = {{.keyCount = 2, .channels = {channel}},
                      {.keyCount = 2, .channels = {channel}}};
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find("/animations/1/channels/0") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document mesh tracks merge distinct paths across arrays and retain STEP rest components",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    doc.nodes[1].rotation = {.5f, .5f, .5f, .5f};
    doc.nodes[1].scale = {-2.f, 3.f, .125f};
    doc.nodes[2].enabled = true;
    doc.nodes.push_back({.name = "Disabled group", .children = {2}, .enabled = false});
    doc.rootNodes = {0, 1, 3};
    const asset::DocChannel translation{.node = 1,
                                        .path = asset::DocChannelPath::Translation,
                                        .step = true,
                                        .values = {{-0.f, 2, 3, 0}, {4, 5, 6, 0}}};
    doc.animations = {{.keyCount = 2, .channels = {translation}}};
    SECTION("rest rotation and scale") {}
    SECTION("scale in a second animation array") {
        doc.animations.push_back({.keyCount = 2,
                                  .channels = {{.node = 1,
                                                .path = asset::DocChannelPath::Scale,
                                                .step = true,
                                                .values = {{1, 2, 3, 0}, {4, 5, 6, 0}}}}});
    }
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "ok" : prepared.error().message));
    REQUIRE(prepared);
    engine::Scene scene;
    engine::DocumentContentBinding binding;
    const std::array<uint32_t, 2> nodes{1, 2};
    REQUIRE(engine::appendDocumentContent(**device, scene, doc, nodes, binding));
    REQUIRE_FALSE(scene.objects[1].enabled);
    REQUIRE(doc.nodes[2].enabled);
    REQUIRE(scene.animation.tracks.size() == 1);
    const auto& track = scene.animation.tracks.front();
    REQUIRE(track.step);
    REQUIRE(track.keys.size() == 2);
    for (size_t k = 0; k < track.keys.size(); ++k) {
        REQUIRE(sameContentBits(track.keys[k].translation, glm::vec3(translation.values[k])));
        REQUIRE(sameContentBits(track.keys[k].rotation, doc.nodes[1].rotation));
        const auto scale = doc.animations.size() == 1
                               ? doc.nodes[1].scale
                               : glm::vec3(doc.animations[1].channels[0].values[k]);
        REQUIRE(sameContentBits(track.keys[k].scale, scale));
    }
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document mesh resources stay shared across an imported asset",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    const auto catalog = scenes::readCatalogDocument("material-lab");
    REQUIRE(catalog);
    const auto source = std::ranges::find_if(
        catalog->nodes, [](const auto& node) { return node.asset.has_value(); });
    REQUIRE(source != catalog->nodes.end());
    auto assetNode = *source;
    assetNode.children.clear();
    doc.nodes.insert(doc.nodes.begin() + 2, std::move(assetNode));
    doc.rootNodes = {0, 1, 2, 3};
    const auto path =
        std::filesystem::current_path() / "SceneDocuments" / "ux6-asset-order.scene.gltf";
    REQUIRE(asset::saveSceneDocument(doc, path));
    const auto loaded = engine::instantiateSceneDocument(**device, doc, path, {});
    INFO((loaded ? "ok" : loaded.error().message));
    REQUIRE(loaded);
    const auto& scene = *loaded->scene;
    REQUIRE(scene.objects.size() >= 3);
    REQUIRE(scene.objects.front().name == "First cube");
    REQUIRE(scene.objects.back().name == "Second cube");
    REQUIRE(scene.objects.front().mesh == scene.objects.back().mesh);
    REQUIRE(scene.objects.front().mesh.slot == 0);
    REQUIRE(scene.skySphere->slot == 1);
    REQUIRE(scene.objects[1].mesh.slot == 2);
    REQUIRE(scene.objects.back().material.slot == 1);
    REQUIRE(loaded->binding.objectNode.front() == 1);
    REQUIRE(loaded->binding.objectNode.back() == 3);
    REQUIRE(loaded->binding.assets.size() == 1);
    REQUIRE(loaded->binding.assets.front().objectBase == 1);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document content preflight rejects animated scales that overflow playback factorization",
          "[scene-doc][instantiate][ux6-content]") {
    auto doc = test::contentDocument();
    doc.animations = {{.keyCount = 2,
                       .channels = {{.node = 1,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 0, 0, 0}, {1, 2, 3, 0}}}}}};
    std::string pointer;
    SECTION("rest scale under a translation track") {
        pointer = "/nodes/1/scale";
        doc.nodes[1].scale = glm::vec3(1e20f);
    }
    SECTION("authored scale key") {
        pointer = "/animations/0/channels/0";
        doc.animations[0].channels[0].path = asset::DocChannelPath::Scale;
        doc.animations[0].channels[0].values = {{1, 1, 1, 0}, {1e20f, 1e20f, 1e20f, 0}};
    }
    asset::RigidTrack overflow{.keys = {{.scale = glm::vec3(1e20f)}}};
    REQUIRE_FALSE(decomposeTransform(asset::sampleRigidTrack(overflow, 0)));
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find(pointer) != std::string::npos);
    REQUIRE(prepared.error().message.find("scale") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content bounds scale before rotated norms can overflow between keys",
          "[scene-doc][instantiate][ux6-content]") {
    const float scale = std::bit_cast<float>(uint32_t{0x5f7fffff});
    const glm::quat rotation{.02393580042f, -.2280997932f, .7069686055f, .6690239906f};
    REQUIRE(std::isfinite(scale * scale));
    const asset::RigidTrack overflow{.keys = {{.rotation = rotation, .scale = glm::vec3(scale)}}};
    REQUIRE_FALSE(decomposeTransform(asset::sampleRigidTrack(overflow, 0)));
    auto doc = test::contentDocument();
    doc.nodes[1].rotation = rotation;
    doc.animations = {{.keyCount = 2,
                       .channels = {{.node = 1,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 0, 0, 0}, {1, 2, 3, 0}}}}}};
    std::string pointer;
    SECTION("rest scale") {
        doc.nodes[1].scale = glm::vec3(scale);
        pointer = "/nodes/1/scale";
    }
    SECTION("scale key") {
        doc.animations[0].channels[0].path = asset::DocChannelPath::Scale;
        doc.animations[0].channels[0].values = {{1, 1, 1, 0}, {scale, scale, scale, 0}};
        pointer = "/animations/0/channels/0/sampler";
    }
    REQUIRE(asset::validateSceneDocumentModel(doc));
    const auto prepared = engine::prepareSceneDocument(doc, {});
    INFO((prepared ? "unexpected success" : prepared.error().message));
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find(pointer) != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content refuses finite norms whose Euler factorization fails",
          "[scene-doc][instantiate][ux6-content]") {
    const float scale = std::bit_cast<float>(uint32_t{0x5e7fffff});
    const glm::quat rotation{.02393580042f, -.2280997932f, .7069686055f, .6690239906f};
    auto doc = test::contentDocument();
    doc.nodes[1].scale = glm::vec3(scale);
    doc.animations = {
        {.sampleRate = 1,
         .keyCount = 2,
         .channels = {
             {.node = 1,
              .path = asset::DocChannelPath::Rotation,
              .values = {{0, 0, 0, 1}, {rotation.x, rotation.y, rotation.z, rotation.w}}}}}};
    const asset::RigidTrack track{
        .keys = {{.time = 0, .scale = glm::vec3(scale)},
                 {.time = 1, .rotation = rotation, .scale = glm::vec3(scale)}}};
    for (uint32_t k = 0; k <= 256; ++k) {
        const auto matrix = asset::sampleRigidTrack(track, double(k) / 256.0);
        for (int c = 0; c < 3; ++c)
            REQUIRE(std::isfinite(glm::length(glm::vec3(matrix[c]))));
    }
    REQUIRE(decomposeTransform(asset::sampleRigidTrack(track, 0)));
    REQUIRE_FALSE(decomposeTransform(asset::sampleRigidTrack(track, 1)));
    const auto prepared = engine::prepareSceneDocument(doc, {});
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find("/nodes/1/scale") != std::string::npos);
    REQUIRE(prepared.error().message.find("[-100, 100]") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content refuses out-of-domain animation scale at either key or rest field",
          "[scene-doc][instantiate][ux6-content]") {
    for (bool scaleChannel : {false, true}) {
        for (float sign : {-1.f, 1.f}) {
            for (int axis = 0; axis < 3; ++axis) {
                CAPTURE(scaleChannel, sign, axis);
                auto doc = test::contentDocument();
                glm::vec3 scale(1);
                scale[axis] = sign * std::nextafter(100.f, 101.f);
                doc.animations = {{.keyCount = 2,
                                   .channels = {{.node = 1,
                                                 .path = asset::DocChannelPath::Translation,
                                                 .values = {{0, 0, 0, 0}, {1, 2, 3, 0}}}}}};
                std::string pointer = "/nodes/1/scale";
                if (scaleChannel) {
                    auto& channel = doc.animations[0].channels[0];
                    channel.path = asset::DocChannelPath::Scale;
                    channel.step = true;
                    channel.values = {glm::vec4(1, 1, 1, 0), glm::vec4(scale, 0)};
                    pointer = "/animations/0/channels/0/sampler";
                } else {
                    doc.nodes[1].scale = scale;
                }
                REQUIRE(asset::validateSceneDocumentModel(doc));
                const auto prepared = engine::prepareSceneDocument(doc, {});
                INFO((prepared ? "unexpected success" : prepared.error().message));
                REQUIRE_FALSE(prepared);
                REQUIRE(prepared.error().code == asset::AssetErrorCode::Unsupported);
                REQUIRE(prepared.error().message.find(pointer) != std::string::npos);
                REQUIRE(prepared.error().message.find("[-100, 100]") != std::string::npos);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("document content domain refuses an interval failure despite factorable endpoints",
          "[scene-doc][instantiate][ux6-content]") {
    const glm::quat first{.60656774044036865f, .33893471956253052f, .46048766374588013f,
                          .55240392684936523f};
    const glm::quat last{-.54079592227935791f, .49777358770370483f, .42351913452148438f,
                         -.52952134609222412f};
    const asset::RigidTrack track{
        .keys = {{.time = 0, .rotation = first, .scale = glm::vec3(1000)},
                 {.time = 1.0 / 60.0, .rotation = last, .scale = glm::vec3(1000)}}};
    REQUIRE(decomposeTransform(asset::sampleRigidTrack(track, 0)));
    REQUIRE(decomposeTransform(asset::sampleRigidTrack(track, 1.0 / 60.0)));
    const auto middle = asset::sampleRigidTrack(track, 43.0 / (64.0 * 60.0));
    for (int c = 0; c < 3; ++c)
        REQUIRE(std::isfinite(glm::length(glm::vec3(middle[c]))));
    REQUIRE_FALSE(decomposeTransform(middle));
    auto doc = test::contentDocument();
    doc.nodes[1].scale = glm::vec3(1000);
    doc.animations = {{.keyCount = 2,
                       .channels = {{.node = 1,
                                     .path = asset::DocChannelPath::Rotation,
                                     .values = {{first.x, first.y, first.z, first.w},
                                                {last.x, last.y, last.z, last.w}}}}}};
    const auto prepared = engine::prepareSceneDocument(doc, {});
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error().message.find("/nodes/1/scale") != std::string::npos);
    REQUIRE(prepared.error().message.find("[-100, 100]") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("document content accepts signed scale boundaries and samples their rotation tracks",
          "[gpu][scene-doc][instantiate][ux6-content]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    const glm::quat first{.60656774044036865f, .33893471956253052f, .46048766374588013f,
                          .55240392684936523f};
    const glm::quat last{-.54079592227935791f, .49777358770370483f, .42351913452148438f,
                         -.52952134609222412f};
    for (bool scaleChannel : {false, true}) {
        for (float sign : {-1.f, 1.f}) {
            CAPTURE(scaleChannel, sign);
            auto doc = test::contentDocument();
            const glm::vec3 scale(100.f * sign);
            doc.nodes[1].scale = scale;
            doc.animations = {{.keyCount = 2,
                               .channels = {{.node = 1,
                                             .path = asset::DocChannelPath::Rotation,
                                             .values = {{first.x, first.y, first.z, first.w},
                                                        {last.x, last.y, last.z, last.w}}}}}};
            if (scaleChannel)
                doc.animations[0].channels.push_back(
                    {.node = 1,
                     .path = asset::DocChannelPath::Scale,
                     .values = {glm::vec4(glm::vec3(sign), 0), glm::vec4(scale, 0)}});
            const auto prepared = engine::prepareSceneDocument(doc, {});
            INFO((prepared ? "prepared" : prepared.error().message));
            REQUIRE(prepared);
            engine::Scene scene;
            engine::DocumentContentBinding binding;
            const std::array<uint32_t, 1> meshNodes{1};
            REQUIRE(engine::appendDocumentContent(**device, scene, doc, meshNodes, binding));
            REQUIRE(scene.animation.tracks.size() == 1);
            const auto& track = scene.animation.tracks.front();
            REQUIRE(sameContentBits(scene.objects[0].scale, scale));
            REQUIRE(sameContentBits(track.keys[0].scale, scaleChannel ? glm::vec3(sign) : scale));
            REQUIRE(sameContentBits(track.keys[1].scale, scale));
            for (uint32_t k = 0; k <= 256; ++k) {
                const double time = double(k) / (256.0 * 60.0);
                REQUIRE(decomposeTransform(asset::sampleRigidTrack(track, time)));
                scene.animate(time);
            }
            (*device)->waitIdle();
        }
    }
}
