//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentExportTests.cpp
/// @brief Tests pure document export, canonical dirty comparison and saved scene round trips.
//----------------------------------------------------------------------------------------------------------------------

#include "Scenes/SceneDocumentExport.h"

#include "App/Model/Scene/EditorPlayback.h"
#include "App/Model/Scene/SceneSession.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Support/EngineTestSupport.h"
#include "Support/GraphTestSupport.h"
#include "Support/SceneDocumentFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace lmx;
namespace fs = std::filesystem;

namespace {

//======================================================================================================================
std::string readText(const fs::path& path) {
    const auto bytes = readWholeFile(path);
    REQUIRE(bytes);
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

//======================================================================================================================
void writeText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file << text;
    REQUIRE(file.good());
}

struct WorkingDirectory {
    fs::path original = fs::current_path();
    ~WorkingDirectory();
};

//======================================================================================================================
WorkingDirectory::~WorkingDirectory() {
    fs::current_path(original);
}

//======================================================================================================================
fs::path fixtureRoot() {
    const auto root = fs::current_path() / "SceneDocuments" / "export-fixture";
    fs::create_directories(root);
    return root;
}

//======================================================================================================================
asset::SceneDocument exportFixture(const fs::path& root) {
    const auto staticPath = test::writeAnimatedQuadGltf(root / "Assets" / "static", "LINEAR", true);
    auto text = readText(staticPath);
    const auto animationStart = text.find("  \"animations\":");
    const auto animationEnd = text.find("  \"buffers\":", animationStart);
    REQUIRE(animationStart != std::string::npos);
    REQUIRE(animationEnd != std::string::npos);
    text.erase(animationStart, animationEnd - animationStart);
    const auto nodesStart = text.find("\"nodes\": [{");
    const auto nodesEnd = text.find("  \"meshes\":", nodesStart);
    text.replace(nodesStart, nodesEnd - nodesStart,
                 R"("nodes": [{"name":"Empty parent","children":[1],"translation":[10,0,0]},
                 {"name":"Two primitives","mesh":0,"translation":[0,1,2]}],)"
                 "\n");
    const auto primitiveStart =
        text.find("\"primitives\": [") + std::string("\"primitives\": [").size();
    const auto primitiveEnd = text.find("}]", primitiveStart) + 1;
    const auto primitive = text.substr(primitiveStart, primitiveEnd - primitiveStart);
    text.insert(primitiveEnd, "," + primitive);
    writeText(staticPath, text);
    const auto animatedPath =
        test::writeAnimatedQuadGltf(root / "Assets" / "animated", "LINEAR", true);
    const auto staticBytes = readWholeFile(staticPath);
    const auto animatedBytes = readWholeFile(animatedPath);
    REQUIRE(staticBytes);
    REQUIRE(animatedBytes);
    asset::SceneDocument doc;
    doc.schemaVersion = asset::kSceneDocumentSchema;
    doc.name = "Export fixture";
    doc.cameras = {{.name = "Saved lens", .farZ = {}, .aspectRatio = 1.6f}};
    doc.lights = {{.name = "Spot",
                   .type = asset::DocLightType::Spot,
                   .colour = {.1234567890123, .5, .75},
                   .intensity = 7.1234567890123,
                   .range = 8,
                   .innerCone = .1f,
                   .outerCone = .5f},
                  {.name = "Point", .type = asset::DocLightType::Point, .range = 4},
                  {.name = "Key", .colour = {.2, .3, .4}, .intensity = 3.7}};
    doc.nodes = {
        {.name = "Camera", .translation = {0, 2, 5}, .rotation = {-1, 0, 0, 0}, .camera = 0},
        {.name = "First asset",
         .translation = {5, 0, 0},
         .asset = asset::DocAsset{"static/quad.gltf", sha256Hex(*staticBytes)}},
        {.name = "Second asset",
         .translation = {100, 0, 0},
         .asset = asset::DocAsset{"static/quad.gltf", sha256Hex(*staticBytes)}},
        {.name = "Local lights", .children = {4, 5}, .enabled = false},
        {.name = "Spot node", .translation = {1, 2, 3}, .rotation = {-1, 0, 0, 0}, .light = 0},
        {.name = "Point node", .light = 1},
        {.name = "Key node",
         .rotation = {-1, 0, 0, 0},
         .light = 2,
         .role = "key",
         .castsShadow = true},
        {.name = "Lab",
         .generator = asset::DocGenerator{"light-lab", {{"lights", 1}, {"pile", 0}}}},
        {.name = "Animated asset",
         .asset = asset::DocAsset{"animated/quad.gltf", sha256Hex(*animatedBytes)}},
        {.name = "Generated objects",
         .generator = asset::DocGenerator{"visibility-lab", {{"instances", 1}, {"occluders", 0}}}}};
    doc.nodes[1].mobility = asset::DocMobility::Movable;
    for (uint32_t node : {4u, 6u})
        doc.nodes[node].mobility = asset::DocMobility::Movable;
    doc.rootNodes = {0, 1, 2, 3, 6, 7, 8, 9};
    // Redundant authored values remain byte-stable instead of being cleaned up during export.
    doc.nodes[2].overrides = {{.node = 0, .name = "Empty parent", .enabled = true},
                              {.node = 1,
                               .name = "Two primitives",
                               .enabled = true,
                               .pose = asset::ObjectPose{{110, 1, 2}, {0, 0, 0}, {1, 1, 1}}}};
    doc.animations = {{.name = "Camera rail",
                       .sampleRate = 60,
                       .keyCount = 2,
                       .channels = {{.node = 0,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 2, 5, 0}, {1, 2, 5, 0}}}}}};
    return doc;
}

//======================================================================================================================
engine::LoadedScene loadFixture(FakeDevice& device, const fs::path& root,
                                const asset::SceneDocument& doc, std::string_view name = "input") {
    const auto path = root / (std::string(name) + ".scene.gltf");
    REQUIRE(asset::saveSceneDocument(doc, path));
    WorkingDirectory restore;
    fs::current_path(root);
    auto loaded = scenes::loadSceneDocument(device, path);
    INFO((loaded ? "ok" : loaded.error().message));
    REQUIRE(loaded);
    return std::move(*loaded);
}

//======================================================================================================================
asset::SceneDocument exported(const engine::LoadedScene& loaded, const app::SceneSession& session) {
    auto result = scenes::exportSceneDocument(loaded, session.scene(), session.documentState());
    INFO((result ? "ok" : result.error().message));
    REQUIRE(result);
    return std::move(*result);
}

//======================================================================================================================
bool dirty(const engine::LoadedScene& loaded, const app::SceneSession& session) {
    return scenes::documentDirty(loaded.document, exported(loaded, session));
}

//======================================================================================================================
uint32_t importedIndex(const engine::LoadedScene& loaded, uint32_t root, uint32_t source) {
    const auto& nodes = loaded.binding.importedNodes;
    const auto found = std::ranges::find_if(nodes, [&](const auto& node) {
        return node.assetRoot == root && node.sourceNode == source;
    });
    REQUIRE(found != nodes.end());
    return static_cast<uint32_t>(found - nodes.begin());
}

//======================================================================================================================
const asset::DocOverride& findOverride(const asset::SceneDocument& doc, uint32_t root,
                                       uint32_t source) {
    const auto& overrides = doc.nodes[root].overrides;
    const auto found = std::ranges::find(overrides, source, &asset::DocOverride::node);
    REQUIRE(found != overrides.end());
    return *found;
}

} // namespace

//======================================================================================================================
TEST_CASE("unedited export preserves loaded fields and canonical bytes", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    REQUIRE(loaded.scene->objects.size() >= 5);
    const auto result = exported(loaded, session);
    CHECK_FALSE(scenes::documentDirty(loaded.document, result));
    CHECK(asset::sceneDocumentJson(result, "same.bin") ==
          asset::sceneDocumentJson(loaded.document, "same.bin"));
    CHECK(asset::sceneDocumentBuffer(result) == asset::sceneDocumentBuffer(loaded.document));
    CHECK(result.lights[0].intensity == loaded.document.lights[0].intensity);
    CHECK(result.nodes[0].rotation == loaded.document.nodes[0].rotation);
    CHECK(result.nodes[4].rotation == loaded.document.nodes[4].rotation);
    CHECK_FALSE(loaded.scene->light(*loaded.binding.nodes[4].light)->enabled);
    CHECK(result.nodes[4].enabled);
    const auto json = asset::sceneDocumentJson(result, "same.bin");
    session.camera().position += glm::vec3(9);
    session.camera().yaw = 1;
    session.camera().fovY = .75f;
    CHECK(asset::sceneDocumentJson(exported(loaded, session), "same.bin") == json);
}

//======================================================================================================================
TEST_CASE("persistent edits then exact restoration become clean despite generation changes",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto source = importedIndex(loaded, 1, 1);
    const auto object = loaded.binding.importedNodes[source].objects.front();
    const auto lightId = *loaded.binding.nodes[4].light;
    SECTION("object pose") {
        REQUIRE(session.editObject(
            object, {.position = {3, 4, 5}, .eulerDegrees = {10, 20, 30}, .scale = {2, 1, 1}}));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.resetObject(object));
    }
    SECTION("object own flag") {
        REQUIRE(session.setObjectEnabled(object, false));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.setObjectEnabled(object, true));
    }
    SECTION("empty imported ancestor") {
        const auto parent = importedIndex(loaded, 1, 0);
        REQUIRE(session.setImportedNodeEnabled(parent, false));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.setImportedNodeEnabled(parent, true));
    }
    SECTION("document group") {
        REQUIRE(session.setNodeEnabled(3, true));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.setNodeEnabled(3, false));
    }
    SECTION("bound local light") {
        auto light = *session.scene().light(lightId);
        light.enabled = session.localLightEnabled(lightId);
        light.intensity += 2;
        REQUIRE(session.editLocalLight(lightId, light));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.resetLocalLight(lightId));
    }
    SECTION("directional light") {
        auto light = session.scene().lights[0];
        light.strength = {1.25f, .125f, 33};
        REQUIRE(session.editLight(0, light));
        REQUIRE(dirty(loaded, session));
        REQUIRE(session.resetLight(0));
    }
    SECTION("look") {
        auto look = session.look();
        look.exposure.ev = 2.5f;
        session.editLook(look);
        REQUIRE(dirty(loaded, session));
        session.editLook(session.lookDefault());
    }
    CHECK(session.editGeneration() >= 2);
    CHECK_FALSE(dirty(loaded, session));
    const auto originalJson = asset::sceneDocumentJson(exported(loaded, session), "same.bin");
    loaded.path = root / "unavailable.scene.gltf";
    CHECK(asset::sceneDocumentJson(exported(loaded, session), "same.bin") == originalJson);
}

//======================================================================================================================
TEST_CASE("saved object group light and look edits survive reader and reinstantiation",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto source = importedIndex(loaded, 1, 1);
    const auto objects = loaded.binding.importedNodes[source].objects;
    REQUIRE(objects.size() == 2);
    const DecomposedTransform pose{{-4, 5, 6}, {10, 20, 30}, {2, 1, 1}};
    REQUIRE(session.editObject(objects.back(), pose));
    REQUIRE(session.setObjectEnabled(objects.front(), false));
    REQUIRE(session.setImportedNodeEnabled(importedIndex(loaded, 2, 0), false));
    REQUIRE(session.setNodeEnabled(3, true));
    const auto spotId = *loaded.binding.nodes[4].light;
    auto spot = *loaded.scene->light(spotId);
    spot.enabled = false;
    spot.position = {9, 8, 7};
    spot.colour = {.25f, .5f, .75f};
    spot.intensity = 6.5f;
    spot.range = 12;
    spot.innerCone = .2f;
    spot.outerCone = .7f;
    spot.direction = {0, 0, 1};
    REQUIRE(session.editLocalLight(spotId, spot));
    auto key = session.scene().lights[0];
    key.strength = {.125f, 3.7f, 129};
    key.direction = {0, 0, 1};
    key.enabled = false;
    REQUIRE(session.editLight(0, key));
    auto look = session.look();
    look.exposure.ev = 2;
    look.exposure.autoEnabled = true;
    look.exposure.lowPercentile = 40;
    look.exposure.highPercentile = 90;
    look.exposure.targetGrey = .25f;
    look.exposure.evMin = -10;
    look.exposure.evMax = 10;
    look.exposure.compensationEv = -1;
    look.exposure.adaptUpStopsPerSecond = 2;
    look.exposure.adaptDownStopsPerSecond = 1;
    look.bloom = {false, 2, .5f};
    look.shadowFilter = asset::ShadowFilter::PCSS;
    session.editLook(look);
    const auto result = exported(loaded, session);
    REQUIRE(scenes::documentDirty(loaded.document, result));
    CHECK(findOverride(result, 1, 1).pose->translation == pose.position);
    CHECK(findOverride(result, 1, 1).enabled == false);
    CHECK(findOverride(result, 2, 0).enabled == false);
    CHECK(findOverride(result, 2, 1).pose->translation == glm::vec3(110, 1, 2));
    auto saved = loadFixture(device, root, result, "saved");
    app::SceneSession reopened;
    reopened.activate(saved, app::SceneActivationMotion::Reset);
    for (size_t object : saved.binding.importedNodes[importedIndex(saved, 1, 1)].objects) {
        CHECK(saved.scene->objects[object].position == pose.position);
        CHECK(saved.scene->objects[object].eulerDegrees == pose.eulerDegrees);
        CHECK(saved.scene->objects[object].scale == pose.scale);
        CHECK_FALSE(saved.scene->objects[object].enabled);
    }
    const auto& other =
        saved.scene
            ->objects[saved.binding.importedNodes[importedIndex(saved, 2, 1)].objects.front()];
    CHECK(other.position == glm::vec3(110, 1, 2));
    CHECK_FALSE(other.enabled);
    CHECK(reopened.importedNodeEnabled(importedIndex(saved, 2, 1)));
    const auto& restored = *saved.scene->light(*saved.binding.nodes[4].light);
    CHECK(restored.position == spot.position);
    CHECK(restored.colour == spot.colour);
    CHECK(restored.intensity == spot.intensity);
    CHECK(restored.range == spot.range);
    CHECK(restored.innerCone == spot.innerCone);
    CHECK(restored.outerCone == spot.outerCone);
    CHECK(restored.direction == spot.direction);
    CHECK_FALSE(restored.enabled);
    CHECK(saved.scene->lights[0].strength == key.strength);
    CHECK(saved.scene->lights[0].direction == key.direction);
    CHECK_FALSE(saved.scene->lights[0].enabled);
    CHECK(saved.scene->look == look);
    CHECK(reopened.nodeEnabled(3));
    CHECK_FALSE(dirty(saved, reopened));
    CHECK_FALSE(scenes::documentDirty(result, exported(saved, reopened)));
}

//======================================================================================================================
TEST_CASE("generated and animated previews never enter the saved document", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    REQUIRE(loaded.binding.nodes[9].objects.size() == 1);
    const auto generated = loaded.binding.nodes[9].objects.front();
    const auto generatedLight = loaded.scene->lightLabPopulations.front().grid.front();
    const auto generatedPose = loaded.scene->objects[generated];
    const auto refusedGenerated = session.editObject(generated, {.position = {8, 9, 10}});
    REQUIRE_FALSE(refusedGenerated);
    CHECK(refusedGenerated.error().message == "Generated objects are placed by their generator");
    CHECK(loaded.scene->objects[generated].position == generatedPose.position);
    CHECK(loaded.scene->objects[generated].previousModel == generatedPose.previousModel);
    REQUIRE(session.setObjectEnabled(generated, false));
    auto light = *session.scene().light(generatedLight);
    light.position = {4, 5, 6};
    light.intensity = 9;
    light.enabled = false;
    REQUIRE(session.editLocalLight(generatedLight, light));
    REQUIRE(session.setLightLabPile(2));
    const auto animated = loaded.binding.importedNodes[importedIndex(loaded, 8, 1)].objects.front();
    const auto animatedPose = loaded.scene->objects[animated];
    const auto refusedAnimated = session.editObject(animated, {.position = {30, 40, 50}});
    REQUIRE_FALSE(refusedAnimated);
    CHECK(refusedAnimated.error().message == "Animation owns this transform");
    CHECK(loaded.scene->objects[animated].position == animatedPose.position);
    CHECK(loaded.scene->objects[animated].previousModel == animatedPose.previousModel);
    CHECK_FALSE(dirty(loaded, session));
    CHECK(session.editGeneration() == 0);
    app::EditorPlayback playback;
    bool follow = true;
    for (bool priorDirty : {false, true}) {
        if (priorDirty) {
            auto look = session.look();
            look.exposure.ev = 3;
            session.editLook(look);
        }
        const auto before = exported(loaded, session);
        const auto generation = session.editGeneration();
        REQUIRE(playback.play(session, follow));
        session.advanceEditorFrame(true, follow, false);
        CHECK_FALSE(scenes::documentDirty(before, exported(loaded, session)));
        REQUIRE(playback.pause());
        REQUIRE(playback.step(session, follow));
        CHECK_FALSE(scenes::documentDirty(before, exported(loaded, session)));
        REQUIRE(playback.stop(session, follow));
        CHECK(session.editGeneration() == generation);
        CHECK(dirty(loaded, session) == priorDirty);
        CHECK_FALSE(scenes::documentDirty(before, exported(loaded, session)));
    }
    REQUIRE(session.setObjectEnabled(animated, false));
    const auto animatedExport = exported(loaded, session);
    const auto& animatedOverride = findOverride(animatedExport, 8, 1);
    CHECK(animatedOverride.enabled == false);
    CHECK_FALSE(animatedOverride.pose);
}

//======================================================================================================================
TEST_CASE("CLI lights remain clean until persisted values change", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    REQUIRE(session.setLocalLightRig(true));
    CHECK(loaded.scene->light(*loaded.binding.nodes[4].light)->enabled);
    CHECK_FALSE(session.nodeEnabled(3));
    CHECK_FALSE(dirty(loaded, session));
    REQUIRE(session.setNodeEnabled(3, false));
    CHECK(session.editGeneration() == 1);
    CHECK_FALSE(dirty(loaded, session));
    REQUIRE(session.setLocalLightRig(true));
    const auto id = *loaded.binding.nodes[4].light;
    auto light = *loaded.scene->light(id);
    light.intensity += 2;
    REQUIRE(session.editLocalLight(id, light));
    CHECK(dirty(loaded, session));
    const auto result = exported(loaded, session);
    CHECK_FALSE(result.nodes[3].enabled);
    CHECK(result.nodes[4].enabled);
    CHECK(result.lights[0].intensity == double(light.intensity));
    CHECK(result.lights[0].colour == loaded.document.lights[0].colour);
    REQUIRE(session.resetLocalLight(id));
    CHECK_FALSE(dirty(loaded, session));
}

//======================================================================================================================
TEST_CASE("export rejects divergent primitive poses rather than losing one edit",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    const auto state = scenes::initialDocumentState(loaded);
    const auto& objects = loaded.binding.importedNodes[importedIndex(loaded, 1, 1)].objects;
    REQUIRE(objects.size() == 2);
    loaded.scene->objects[objects.back()].position.x += 1;
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("/nodes/1/extensions/LMX_scene/overrides") !=
          std::string::npos);
    CHECK(result.error().message.find("source node 1") != std::string::npos);
    CHECK(loaded.document.nodes[1].overrides.empty());
}

//======================================================================================================================
TEST_CASE("saved camera uses only the explicit rest camera and preserves untouched lens fields",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto doc = exportFixture(root);
    doc.nodes.push_back({.name = "Second camera", .camera = 0});
    doc.rootNodes.push_back(static_cast<uint32_t>(doc.nodes.size() - 1));
    auto loaded = loadFixture(device, root, doc);
    auto state = scenes::initialDocumentState(loaded);
    auto camera = loaded.scene->initialCamera;
    camera.position = {9, 8, 7};
    camera.fovY = .7f;
    camera.nearZ = .2f;
    camera.farZ = 90;
    camera.yaw = 0;
    camera.pitch = .25f;
    state.sceneCamera = camera;
    auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    INFO((result ? "ok" : result.error().message));
    REQUIRE(result);
    CHECK(scenes::documentDirty(loaded.document, *result));
    CHECK(result->cameras.size() == 2);
    CHECK(result->nodes.back().camera == 0);
    CHECK(result->cameras[0].fovY == doc.cameras[0].fovY);
    CHECK(result->cameras[*result->nodes[0].camera].aspectRatio == doc.cameras[0].aspectRatio);
    auto saved = loadFixture(device, root, *result, "camera");
    CHECK(saved.scene->initialCamera.position == camera.position);
    CHECK(saved.scene->initialCamera.yaw == camera.yaw);
    CHECK(saved.scene->initialCamera.pitch == camera.pitch);
    CHECK(saved.scene->initialCamera.fovY == camera.fovY);
    CHECK(saved.scene->initialCamera.nearZ == camera.nearZ);
    CHECK(saved.scene->initialCamera.farZ == camera.farZ);
    state.sceneCamera = loaded.scene->initialCamera;
    result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    REQUIRE(result);
    CHECK_FALSE(scenes::documentDirty(doc, *result));
}

//======================================================================================================================
TEST_CASE("shared light definitions split only for a changed bound node", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto doc = exportFixture(root);
    doc.nodes[5].light = doc.nodes[4].light;
    auto loaded = loadFixture(device, root, doc);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto id = *loaded.binding.nodes[4].light;
    auto light = *loaded.scene->light(id);
    light.enabled = true;
    light.intensity = 20;
    REQUIRE(session.editLocalLight(id, light));
    const auto result = exported(loaded, session);
    CHECK(result.lights.size() == doc.lights.size() + 1);
    CHECK(result.nodes[5].light == doc.nodes[5].light);
    CHECK(result.lights[*result.nodes[5].light].intensity == doc.lights[0].intensity);
    CHECK(result.lights[*result.nodes[4].light].intensity == 20);
    auto saved = loadFixture(device, root, result, "shared-light");
    CHECK(saved.scene->light(*saved.binding.nodes[5].light)->intensity ==
          float(doc.lights[0].intensity));
    app::SceneSession reopened;
    reopened.activate(saved, app::SceneActivationMotion::Reset);
    CHECK_FALSE(scenes::documentDirty(result, exported(saved, reopened)));
    REQUIRE(session.resetLocalLight(id));
    CHECK_FALSE(dirty(loaded, session));
}

//======================================================================================================================
TEST_CASE("nonfinite or non-unit orientation export fails explicitly", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    auto state = scenes::initialDocumentState(loaded);
    std::string expected;
    SECTION("directional") {
        loaded.scene->lights[0].direction = {0, 0, -2};
        expected = "/nodes/6/rotation";
    }
    SECTION("spot") {
        const auto id = *loaded.binding.nodes[4].light;
        auto light = *loaded.scene->light(id);
        light.direction = {0, 0, -2};
        REQUIRE(loaded.scene->updateLight(id, light));
        expected = "/nodes/4/rotation";
    }
    SECTION("camera") {
        state.sceneCamera = loaded.scene->initialCamera;
        state.sceneCamera->yaw = std::numeric_limits<float>::quiet_NaN();
        expected = "/nodes/0/rotation";
    }
    const auto before = asset::sceneDocumentJson(loaded.document, "unchanged.bin");
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == asset::AssetErrorCode::Unsupported);
    CHECK(result.error().message.find(expected) != std::string::npos);
    CHECK(result.error().message.find("exact") != std::string::npos);
    CHECK(asset::sceneDocumentJson(loaded.document, "unchanged.bin") == before);
}

namespace {
//======================================================================================================================
std::optional<glm::vec3> directionWithoutExactQuaternion() {
    for (int i = 1; i <= 128; ++i) {
        const auto direction = glm::normalize(glm::vec3(i, i + 3, i + 7));
        if (!asset::exactRotationForDirection(direction))
            return direction;
    }
    return std::nullopt;
}
} // namespace

//======================================================================================================================
TEST_CASE("directions without an exact quaternion save the nearest one and are reported",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    const auto state = scenes::initialDocumentState(loaded);
    const auto direction = directionWithoutExactQuaternion();
    REQUIRE(direction);
    loaded.scene->lights[0].direction = *direction;
    const auto id = *loaded.binding.nodes[4].light;
    auto light = *loaded.scene->light(id);
    light.direction = *direction;
    REQUIRE(loaded.scene->updateLight(id, light));
    scenes::ExportReport report;
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state, &report);
    INFO((result ? "ok" : result.error().message));
    REQUIRE(result);
    REQUIRE(report.approximations.size() == 2);
    CHECK(report.approximations[0].node == 4);
    CHECK(report.approximations[1].node == 6);
    for (const uint32_t node : {4u, 6u})
        CHECK(glm::dot(asset::directionForRotation(result->nodes[node].rotation), *direction) >
              1.0f - 1e-6f);
    const auto path = root / "nearest.scene.gltf";
    REQUIRE(asset::saveSceneDocument(*result, path));
    const auto read = asset::readSceneDocument(path);
    REQUIRE(read);
    CHECK_FALSE(scenes::documentDirty(*result, *read));
    CHECK(scenes::exportSceneDocument(loaded, *loaded.scene, state)); // No report requested.
}

//======================================================================================================================
TEST_CASE("a saved camera yaw beyond a half turn wraps and stays exportable", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    auto state = scenes::initialDocumentState(loaded);
    state.sceneCamera = loaded.scene->initialCamera;
    state.sceneCamera->yaw = 10.0f;
    state.sceneCamera->pitch = 0.25f;
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    INFO((result ? "ok" : result.error().message));
    REQUIRE(result);
    const auto angles = asset::cameraAnglesForRotation(result->nodes[0].rotation, 0);
    CHECK(angles.x == asset::unwrapYaw(0.0f, 10.0f));
    CHECK(angles.y == 0.25f);
}

//======================================================================================================================
TEST_CASE("nonfinite look values fail export instead of aborting the dirty comparison",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    const auto state = scenes::initialDocumentState(loaded);
    loaded.scene->look.bloom.intensity = std::numeric_limits<float>::infinity();
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == asset::AssetErrorCode::Malformed);
    CHECK(result.error().message.find("/extensions/LMX_scene/look") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("noncanonical input is clean and saves the canonical model", "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    const auto canonical = readText(loaded.path);
    writeText(loaded.path, " \n\t" + canonical + " \n");
    WorkingDirectory restore;
    fs::current_path(root);
    auto raw = scenes::loadSceneDocument(device, loaded.path);
    REQUIRE(raw);
    app::SceneSession session;
    session.activate(*raw, app::SceneActivationMotion::Reset);
    CHECK_FALSE(dirty(*raw, session));
    const auto result = exported(*raw, session);
    REQUIRE(asset::saveSceneDocument(result, raw->path));
    CHECK(readText(raw->path) == canonical);
    const auto read = asset::readSceneDocument(raw->path);
    REQUIRE(read);
    CHECK_FALSE(scenes::documentDirty(result, *read));
}

//======================================================================================================================
TEST_CASE("dirty compares animation buffer bytes as well as canonical JSON", "[scene-export]") {
    const auto root = fixtureRoot();
    const auto original = exportFixture(root);
    auto changed = original;
    changed.warnings.push_back("Read diagnostic only");
    CHECK_FALSE(scenes::documentDirty(original, changed));
    changed.animations[0].channels[0].values[1].x += 1;
    CHECK(asset::sceneDocumentJson(original, "same.bin") ==
          asset::sceneDocumentJson(changed, "same.bin"));
    CHECK(scenes::documentDirty(original, changed));
}

//======================================================================================================================
TEST_CASE("successful save adoption uses the new override before the immutable pose baseline",
          "[scene-export]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto loaded = loadFixture(device, root, exportFixture(root));
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto object = loaded.binding.importedNodes[importedIndex(loaded, 1, 1)].objects.front();
    const auto original = session.objectDefault(object);
    const DecomposedTransform changed{{4, 5, 6}, {10, 20, 30}, {1, 2, 1}};
    REQUIRE(session.editObject(object, changed));
    REQUIRE(session.setObjectEnabled(object, false));
    auto saved = exported(loaded, session);
    const auto path = root / "adopted.scene.gltf";
    REQUIRE(asset::saveSceneDocument(saved, path));
    auto canonical = asset::readSceneDocument(path);
    const auto hash = asset::sceneDocumentHash(path);
    REQUIRE(canonical);
    REQUIRE(hash);
    loaded.document = std::move(*canonical);
    loaded.path = path;
    loaded.hash = *hash;
    session.adoptDocumentResetBaseline();
    CHECK_FALSE(dirty(loaded, session));
    REQUIRE(session.editObject(object, original));
    CHECK(dirty(loaded, session));
    REQUIRE(session.resetObject(object));
    CHECK_FALSE(dirty(loaded, session));
    REQUIRE(session.setObjectEnabled(object, true));
    CHECK(dirty(loaded, session));
    REQUIRE(session.setObjectEnabled(object, false));
    CHECK_FALSE(dirty(loaded, session));
}

//======================================================================================================================
TEST_CASE("saved mesh export preserves loaded bits and independently persists edits",
          "[gpu][scene-export][ux6-mesh-export]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    doc.nodes[1].mobility = asset::DocMobility::Movable;
    doc.nodes[1].rotation = glm::quat(-.5f, -.5f, -.5f, -.5f);
    doc.nodes[1].translation.x = -0.f;
    const auto path = fixtureRoot() / "mesh-export.scene.gltf";
    REQUIRE(asset::saveSceneDocument(doc, path));
    auto read = asset::readSceneDocument(path);
    REQUIRE(read);
    auto loaded = engine::instantiateSceneDocument(**device, *read, path, {});
    REQUIRE(loaded);
    app::SceneSession session;
    session.activate(*loaded, app::SceneActivationMotion::Reset);
    auto result = exported(*loaded, session);
    CHECK_FALSE(scenes::documentDirty(loaded->document, result));
    CHECK(result.content == loaded->document.content);
    CHECK(asset::sceneDocumentJson(result, "same.bin") ==
          asset::sceneDocumentJson(loaded->document, "same.bin"));
    const auto object = loaded->binding.nodes[1].objects.front();
    SECTION("position and signed nonuniform scale retain the loaded quaternion") {
        auto pose = session.objectDefault(object);
        pose.position = {5.f, -0.f, 7.f};
        pose.scale = {-2.f, .125f, 3.f};
        REQUIRE(session.editObject(object, pose));
        result = exported(*loaded, session);
        CHECK(std::memcmp(&result.nodes[1].rotation, &loaded->document.nodes[1].rotation,
                          sizeof(glm::quat)) == 0);
    }
    SECTION("rotation and pose export exactly") {
        auto pose = session.objectDefault(object);
        pose.position = {5.f, -0.f, 7.f};
        pose.eulerDegrees = {0.f, 30.f, 0.f};
        pose.scale = {-2.f, .125f, 3.f};
        REQUIRE(session.editObject(object, pose));
        result = exported(*loaded, session);
        const auto decoded = asset::eulerDegreesForRotation(result.nodes[1].rotation);
        for (int k = 0; k < 3; ++k)
            CHECK(std::bit_cast<uint32_t>(decoded[k]) ==
                  std::bit_cast<uint32_t>(pose.eulerDegrees[k]));
    }
    SECTION("enabled exports the own node flag") {
        REQUIRE(session.setObjectEnabled(object, false));
        CHECK_FALSE(session.nodeEnabled(1));
        CHECK_FALSE(session.scene().objects[object].enabled);
        result = exported(*loaded, session);
        CHECK_FALSE(result.nodes[1].enabled);
    }
    CHECK(scenes::documentDirty(loaded->document, result));
    auto onlyEditedNode = loaded->document;
    onlyEditedNode.nodes[1] = result.nodes[1];
    CHECK_FALSE(scenes::documentDirty(onlyEditedNode, result));
    CHECK(result.content == loaded->document.content);
    REQUIRE(asset::saveSceneDocument(result, path));
    read = asset::readSceneDocument(path);
    REQUIRE(read);
    auto reloaded = engine::instantiateSceneDocument(**device, *read, path, {});
    REQUIRE(reloaded);
    const auto& before = session.scene().objects[object];
    const auto& after = reloaded->scene->objects[reloaded->binding.nodes[1].objects.front()];
    for (int k = 0; k < 3; ++k) {
        CHECK(std::bit_cast<uint32_t>(before.position[k]) ==
              std::bit_cast<uint32_t>(after.position[k]));
        CHECK(std::bit_cast<uint32_t>(before.eulerDegrees[k]) ==
              std::bit_cast<uint32_t>(after.eulerDegrees[k]));
        CHECK(std::bit_cast<uint32_t>(before.scale[k]) == std::bit_cast<uint32_t>(after.scale[k]));
    }
    CHECK(after.enabled == before.enabled);
    device->get()->waitIdle();
}

//======================================================================================================================
TEST_CASE("mesh TRS animation excludes pose export but emissive animation does not",
          "[gpu][scene-export][ux6-mesh-export]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto doc = test::contentDocument();
    bool rigid = false;
    SECTION("rigid track owns pose") {
        rigid = true;
        doc.animations = {{.keyCount = 2,
                           .channels = {{.node = 1,
                                         .path = asset::DocChannelPath::Translation,
                                         .values = {{1, 2, 3, 0}, {4, 5, 6, 0}}}}}};
    }
    SECTION("emissive track leaves pose editable") {
        doc.nodes[1].mobility = asset::DocMobility::Movable;
        doc.animations = {{.keyCount = 2,
                           .channels = {{.node = 0,
                                         .path = asset::DocChannelPath::EmissiveStrength,
                                         .material = 0,
                                         .step = true,
                                         .values = {{1, 0, 0, 0}, {2, 0, 0, 0}}}}}};
    }
    const auto path = fixtureRoot() / "animated-mesh-export.scene.gltf";
    REQUIRE(asset::saveSceneDocument(doc, path));
    auto loaded = engine::instantiateSceneDocument(**device, doc, path, {});
    INFO((loaded ? "ok" : loaded.error().message));
    REQUIRE(loaded);
    app::SceneSession session;
    session.activate(*loaded, app::SceneActivationMotion::Reset);
    const auto object = loaded->binding.nodes[1].objects.front();
    auto pose = session.objectDefault(object);
    pose.position.x = 9.f;
    const auto before = session.scene().objects[object];
    const auto generation = session.editGeneration();
    const auto edit = session.editObject(object, pose);
    if (rigid) {
        REQUIRE_FALSE(edit);
        CHECK(edit.error().message == "Animation owns this transform");
        CHECK(session.scene().objects[object].position == before.position);
        CHECK(session.scene().objects[object].previousModel == before.previousModel);
        CHECK(session.editGeneration() == generation);
    } else
        REQUIRE(edit);
    auto result = exported(*loaded, session);
    if (rigid)
        CHECK_FALSE(scenes::documentDirty(loaded->document, result));
    else {
        CHECK(scenes::documentDirty(loaded->document, result));
        CHECK(result.nodes[1].translation.x == 9.f);
    }
    REQUIRE(session.setNodeEnabled(1, false));
    result = exported(*loaded, session);
    CHECK_FALSE(result.nodes[1].enabled);
    CHECK(result.nodes[1].translation.x == (rigid ? doc.nodes[1].translation.x : 9.f));
    device->get()->waitIdle();
}

//======================================================================================================================
TEST_CASE("mesh export refuses unrepresentable Euler edits without approximation",
          "[scene-export][ux6-mesh-export]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document = test::contentDocument();
    loaded.binding.nodes.resize(3);
    loaded.binding.nodes[1].objects = {0};
    loaded.scene->objects.push_back({.position = loaded.document.nodes[1].translation});
    loaded.binding.nodes[2].objects = {1};
    loaded.scene->objects.push_back({.scale = loaded.document.nodes[2].scale});
    auto state = scenes::initialDocumentState(loaded);
    for (const auto angles : {glm::vec3(0, 360, 0), glm::vec3(100, 0, 0),
                              glm::vec3(0, std::numeric_limits<float>::quiet_NaN(), 0)}) {
        loaded.scene->objects[0].eulerDegrees = angles;
        scenes::ExportReport report;
        auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state, &report);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.contains("/nodes/1/rotation"));
        CHECK(report.approximations.empty());
    }
}

//======================================================================================================================
TEST_CASE("mesh export keeps own enabled state under a disabled ancestor",
          "[scene-export][ux6-mesh-export]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document = test::contentDocument();
    loaded.document.nodes.push_back({.name = "Disabled parent", .children = {1}, .enabled = false});
    loaded.document.rootNodes = {0, 2, 3};
    loaded.binding.nodes.resize(4);
    for (uint32_t n = 1; n <= 2; ++n) {
        loaded.binding.nodes[n].objects = {n - 1};
        const auto& node = loaded.document.nodes[n];
        loaded.scene->objects.push_back(
            {.position = node.translation,
             .eulerDegrees = asset::eulerDegreesForRotation(node.rotation),
             .scale = node.scale,
             .enabled = false});
    }
    const auto state = scenes::initialDocumentState(loaded);
    const auto result = scenes::exportSceneDocument(loaded, *loaded.scene, state);
    REQUIRE(result);
    CHECK(result->nodes[1].enabled);
    CHECK_FALSE(result->nodes[3].enabled);
    CHECK_FALSE(scenes::documentDirty(loaded.document, *result));
}

//======================================================================================================================
TEST_CASE("mobility follows nearest source override and export preserves authored values",
          "[scene-export][ux6-mobility]") {
    FakeDevice device;
    const auto root = fixtureRoot();
    auto doc = exportFixture(root);
    doc.schemaVersion = 2;
    doc.nodes[1].mobility = asset::DocMobility::Movable;
    doc.nodes[2].mobility = asset::DocMobility::Static;
    doc.nodes[2].overrides[0].mobility = asset::DocMobility::Movable;
    doc.nodes[4].mobility = asset::DocMobility::Movable;
    doc.nodes[6].mobility = asset::DocMobility::Movable;
    SECTION("nearest child override wins") {
        doc.nodes[2].overrides[1].mobility = asset::DocMobility::Static;
    }
    auto loaded = loadFixture(device, root, doc, "mobility-bound");
    REQUIRE(loaded.objectMobility.size() == loaded.scene->objects.size());
    for (auto object : loaded.binding.nodes[1].objects)
        CHECK(loaded.objectMobility[object] == asset::DocMobility::Movable);
    const auto expected = doc.nodes[2].overrides[1].mobility.value_or(asset::DocMobility::Movable);
    for (auto object : loaded.binding.nodes[2].objects)
        CHECK(loaded.objectMobility[object] == expected);
    for (auto object : loaded.binding.nodes[9].objects)
        CHECK(loaded.objectMobility[object] == asset::DocMobility::Static);
    CHECK(loaded.lightMobility == std::vector<asset::DocMobility>{asset::DocMobility::Movable,
                                                                  asset::DocMobility::Static,
                                                                  asset::DocMobility::Movable});
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto result = exported(loaded, session);
    CHECK_FALSE(scenes::documentDirty(loaded.document, result));
    CHECK(asset::sceneDocumentJson(result, "same.bin") ==
          asset::sceneDocumentJson(loaded.document, "same.bin"));
    auto reread = loadFixture(device, root, result, "mobility-exported");
    CHECK(reread.objectMobility == loaded.objectMobility);
    CHECK(reread.lightMobility == loaded.lightMobility);
}
