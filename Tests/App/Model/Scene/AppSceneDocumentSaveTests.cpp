#include "App/Model/Scene/DocumentWorkPump.h"
#include "App/Model/Scene/DocumentWorkflow.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneDocumentSave.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Scenes/SceneDocumentExport.h"
#include "Support/EngineTestSupport.h"
#include "Support/GraphTestSupport.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <limits>

using namespace lmx;
namespace fs = std::filesystem;

namespace {
//======================================================================================================================
asset::SceneDocument saveFixture() {
    asset::SceneDocument doc;
    doc.name = "Save workflow";
    doc.cameras = {{.name = "Lens"}};
    doc.lights = {{.name = "Point", .type = asset::DocLightType::Point, .range = 8}};
    doc.nodes = {{.name = "Camera", .translation = {0, 1, 4}, .camera = 0},
                 {.name = "Local Lights", .children = {2}},
                 {.name = "Light", .light = 0}};
    doc.rootNodes = {0, 1};
    // A camera rail gives the document its companion buffer.
    doc.animations = {{.name = "Rail",
                       .sampleRate = 60,
                       .keyCount = 2,
                       .channels = {{.node = 0,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 1, 4, 0}, {1, 1, 4, 0}}}}}};
    return doc;
}
//======================================================================================================================
fs::path savePath(std::string_view name, const asset::SceneDocument& doc = saveFixture()) {
    auto root = fs::current_path() / "SceneDocuments" / "save-workflow";
    // Files from an earlier run must not look like foreign companions to Save As.
    static const bool cleaned = [&] {
        std::error_code cleanup;
        fs::remove_all(root, cleanup);
        return true;
    }();
    (void)cleaned;
    fs::create_directories(root);
    const auto path = root / (std::string(name) + ".scene.gltf");
    std::error_code ignored;
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add, ignored);
    auto bin = path;
    bin.replace_extension(".bin");
    fs::permissions(bin, fs::perms::owner_write, fs::perm_options::add, ignored);
    // Leftovers from an earlier run must not look like foreign files to the companion check.
    fs::remove(path, ignored);
    fs::remove(bin, ignored);
    REQUIRE(asset::saveSceneDocument(doc, path));
    return path;
}
//======================================================================================================================
bool dirty(const app::SceneSession& session) {
    const auto result = scenes::exportSceneDocument(*session.loadedScene(), session.scene(),
                                                    session.documentState());
    return !result || scenes::documentDirty(session.loadedScene()->document, *result);
}
} // namespace

//======================================================================================================================
TEST_CASE("explicit saved camera changes generation without ordinary view persistence",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    auto id = scenes::sceneIdFromPath(savePath("camera"));
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    const auto generation = session.editGeneration();
    const auto original = session.scene().initialCamera;
    session.camera().position.x += 2;
    CHECK_FALSE(dirty(session));
    CHECK(session.editGeneration() == generation);
    session.setSceneCamera();
    REQUIRE(dirty(session));
    REQUIRE(session.editGeneration() == generation + 1);
    CHECK(session.scene().initialCamera.position == original.position);
    session.setSceneCamera();
    CHECK(session.editGeneration() == generation + 1);
    session.camera() = engine::cameraFromScene(original);
    session.setSceneCamera();
    CHECK_FALSE(dirty(session));
    session.setMeasurementActive(true);
    session.camera().position.x += 4;
    REQUIRE_FALSE(session.setSceneCamera());
    CHECK_FALSE(dirty(session));
}

//======================================================================================================================
TEST_CASE("a saved camera yaw beyond a half turn wraps, saves and stays clean",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("yaw-wrap");
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    session.camera().yaw = 10.0f;
    session.camera().pitch = 0.25f;
    REQUIRE(session.setSceneCamera());
    CHECK(session.camera().yaw == asset::unwrapYaw(0.0f, 10.0f));
    REQUIRE(dirty(session));
    REQUIRE(app::saveSessionDocument(library, session, id, path, false));
    CHECK_FALSE(dirty(session));
    CHECK(session.scene().initialCamera.yaw == session.camera().yaw);
    CHECK(session.scene().initialCamera.pitch == 0.25f);
}

//======================================================================================================================
TEST_CASE("steep orientations without an exact quaternion save, reload and stay clean",
          "[app][document-save]") {
    std::optional<std::pair<float, float>> steepCamera;
    for (int i = 0; i < 140 && !steepCamera; ++i)
        for (int j = 0; j < 20 && !steepCamera; ++j) {
            const float yaw = 0.37f + 0.05f * static_cast<float>(j);
            const float pitch = 1.5f + 0.0005f * static_cast<float>(i);
            if (!asset::exactRotationForCamera(yaw, pitch, 0))
                steepCamera = {yaw, pitch};
        }
    std::optional<glm::vec3> steepDirection;
    for (int i = 1; i <= 128 && !steepDirection; ++i) {
        const auto direction = glm::normalize(glm::vec3(i, i + 3, i + 7));
        if (!asset::exactRotationForDirection(direction))
            steepDirection = direction;
    }
    REQUIRE(steepCamera);
    REQUIRE(steepDirection);
    auto doc = saveFixture();
    doc.lights.push_back({.name = "Spot",
                          .type = asset::DocLightType::Spot,
                          .intensity = 2,
                          .range = 8,
                          .innerCone = .1f,
                          .outerCone = .5f});
    doc.lights.push_back({.name = "Key", .colour = {.2, .3, .4}, .intensity = 3.7});
    doc.nodes.push_back({.name = "Spot node", .translation = {1, 2, 3}, .light = 1});
    doc.nodes.push_back({.name = "Key node", .light = 2, .role = "key"});
    doc.rootNodes.insert(doc.rootNodes.end(), {3, 4});
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("steep", doc);
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto* loaded = session.loadedScene();
    session.camera().yaw = steepCamera->first;
    session.camera().pitch = steepCamera->second;
    REQUIRE(session.setSceneCamera());
    const auto spot = *loaded->binding.nodes[3].light;
    auto light = *session.scene().light(spot);
    light.direction = *steepDirection;
    REQUIRE(session.scene().updateLight(spot, light));
    const auto key = *loaded->binding.nodes[4].directional;
    session.scene().lights[key].direction = *steepDirection;
    REQUIRE(dirty(session));
    REQUIRE(app::saveSessionDocument(library, session, id, path, false));
    CHECK_FALSE(dirty(session));
    const auto reloaded = asset::readSceneDocument(path);
    REQUIRE(reloaded);
    CHECK_FALSE(scenes::documentDirty(loaded->document, *reloaded));
    CHECK(glm::dot(session.scene().light(spot)->direction, *steepDirection) > 1.0f - 1e-6f);
    CHECK(glm::dot(session.scene().lights[key].direction, *steepDirection) > 1.0f - 1e-6f);
    CHECK(session.scene().light(spot)->direction ==
          asset::directionForRotation(reloaded->nodes[3].rotation));
    const auto& camera = session.scene().initialCamera;
    CHECK(camera.pitch == session.documentState().sceneCamera->pitch);
    CHECK(camera.yaw == session.documentState().sceneCamera->yaw);
    CHECK(camera.pitch == Catch::Approx(steepCamera->second).margin(1e-3f));
}

//======================================================================================================================
TEST_CASE("Save and Save As adopt one snapshot while preserving live identity and own state",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("adoption");
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto* loaded = session.loadedScene();
    auto* scene = &session.scene();
    const auto lightId = scene->localLights().front();
    REQUIRE(session.setLocalLightEnabled(lightId, false));
    auto look = session.look();
    look.bloom.intensity += .5f;
    session.editLook(look);
    session.camera().position.x += 2;
    REQUIRE(session.setSceneCamera());
    const auto ownFlags = session.documentState().nodeEnabled;
    const auto* state = &session.documentState();
    const auto generation = session.editGeneration();
    auto selection = app::EditorSelection{
        .sceneId = id, .subject = app::EditorSubject::LocalLight, .lightId = lightId};
    REQUIRE(app::saveSessionDocument(library, session, id, path, false));
    CHECK_FALSE(dirty(session));
    CHECK(session.loadedScene() == loaded);
    CHECK(&session.scene() == scene);
    CHECK(&session.documentState() == state);
    CHECK(session.documentState().nodeEnabled == ownFlags);
    CHECK(session.editGeneration() == generation);
    CHECK(session.lookDefault() == look);
    CHECK(session.scene().initialCamera.position == session.camera().position);
    CHECK(loaded->hash == *asset::sceneDocumentHash(path));
    CHECK_FALSE(session.localLightDefault(lightId)->enabled);
    const auto copy = path.parent_path() / "adopted-copy.scene.gltf";
    const auto previous = id;
    CHECK(library.entry(previous).displayName == "Save workflow");
    REQUIRE(app::saveSessionDocument(library, session, id, copy, true));
    CHECK(library.entry(previous).displayName == path.filename().string());
    CHECK(library.entry(id).displayName == "Save workflow");
    selection.sceneId = id;
    CHECK(id == scenes::sceneIdFromPath(copy));
    CHECK(library.loaded(id) == loaded);
    CHECK(library.loaded(previous) == nullptr);
    CHECK(session.loadedScene() == loaded);
    CHECK(&session.documentState() == state);
    CHECK_FALSE(dirty(session));
    CHECK(app::resolveSelection(selection, id, session.scene()).lightId == lightId);
    CHECK(loaded->path == copy);
    CHECK(loaded->hash == *asset::sceneDocumentHash(copy));
    REQUIRE(session.setLocalLightEnabled(lightId, true));
    REQUIRE(session.resetLocalLight(lightId));
    CHECK_FALSE(session.localLightEnabled(lightId));
    CHECK_FALSE(dirty(session));
}

//======================================================================================================================
TEST_CASE("save failures preserve metadata baselines and block save-first destruction",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("failed");
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    auto* loaded = session.loadedScene();
    const auto oldHash = loaded->hash;
    const auto oldDoc = asset::sceneDocumentJson(loaded->document, "same.bin");
    const auto baseline = session.lookDefault();
    const auto oldId = id;
    app::SceneDocumentSaveIO io;
    SECTION("export validity fails before any disk writes") {
        look.bloom.intensity = std::numeric_limits<float>::infinity();
        session.editLook(look);
    }
    SECTION("write failure") {
        io.write = [](const auto&, const auto&) -> asset::AssetResult<void> {
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io, "write failure"});
        };
    }
    SECTION("canonical read failure after successful write") {
        io.read = [](const auto&) -> asset::AssetResult<asset::SceneDocument> {
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io, "read failure"});
        };
    }
    SECTION("canonical read differs from the exported model") {
        io.read = [](const auto& path) -> asset::AssetResult<asset::SceneDocument> {
            auto result = asset::readSceneDocument(path);
            if (result)
                result->look.bloom.intensity += 1;
            return result;
        };
    }
    SECTION("hash failure after canonical read") {
        io.hash = [](const auto&) -> asset::AssetResult<std::string> {
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io, "hash failure"});
        };
    }
    app::DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(app::DocumentAction::OpenCatalog, scenes::SceneId{"sponza"}));
    flow.confirm(app::ConfirmChoice::Save);
    REQUIRE(flow.takeWork()->saveFirst);
    const auto result = app::saveSessionDocument(library, session, id, path, false, io);
    REQUIRE_FALSE(result);
    flow.complete(result.has_value());
    CHECK(flow.dirty());
    CHECK_FALSE(flow.takeWork());
    CHECK(id == oldId);
    CHECK(loaded == library.loaded(id));
    CHECK(loaded->path == path);
    CHECK(loaded->hash == oldHash);
    CHECK(asset::sceneDocumentJson(loaded->document, "same.bin") == oldDoc);
    CHECK(session.lookDefault() == baseline);
}

//======================================================================================================================
TEST_CASE("read-only companion and Save As aliases preserve both existing files",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("protected");
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    auto bin = path;
    bin.replace_extension(".bin");
    const auto beforeJson = *readWholeFile(path);
    const auto beforeBin = *readWholeFile(bin);
    SECTION("companion is read only") {
        fs::permissions(bin,
                        fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
        const auto result = app::saveSessionDocument(library, session, id, path, false);
        fs::permissions(bin, fs::perms::owner_write, fs::perm_options::add);
        REQUIRE_FALSE(result);
    }
    SECTION("normalized current path") {
        REQUIRE_FALSE(app::saveSessionDocument(library, session, id,
                                               path.parent_path() / "." / path.filename(), true));
    }
    SECTION("symlink current path") {
        const auto alias = path.parent_path() / "alias.scene.gltf";
        std::error_code error;
        fs::remove(alias, error);
        fs::create_symlink(path, alias);
        REQUIRE_FALSE(app::saveSessionDocument(library, session, id, alias, true));
        fs::remove(alias);
    }
    SECTION("hard link current path") {
        const auto alias = path.parent_path() / "hard.scene.gltf";
        std::error_code error;
        fs::remove(alias, error);
        fs::create_hard_link(path, alias);
        REQUIRE_FALSE(app::saveSessionDocument(library, session, id, alias, true));
        fs::remove(alias);
    }
    CHECK(*readWholeFile(path) == beforeJson);
    CHECK(*readWholeFile(bin) == beforeBin);
    CHECK(dirty(session));
}

//======================================================================================================================
TEST_CASE("replacement builds before invalidation and discard cannot return through the cache",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto path = savePath("replace-current");
    const auto destination = savePath("replace-next");
    auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    const auto* previous = session.activeScene();
    const auto previousId = id;
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    bool invalidated = false;
    const auto before = [&] {
        REQUIRE(session.activeScene() == previous);
        REQUIRE(dirty(session));
        invalidated = true;
    };
    SECTION("failed target retains everything") {
        auto failed = app::replaceSessionDocument(
            library, session, id,
            scenes::sceneIdFromPath(destination.parent_path() / "missing.scene.gltf"), before);
        REQUIRE_FALSE(failed);
        CHECK_FALSE(invalidated);
        CHECK(id == previousId);
        CHECK(session.activeScene() == previous);
        CHECK(session.look() == look);
        CHECK(dirty(session));
    }
    SECTION("successful target discards old cached edits") {
        REQUIRE(app::replaceSessionDocument(library, session, id,
                                            scenes::sceneIdFromPath(destination), before));
        CHECK(invalidated);
        CHECK(id == scenes::sceneIdFromPath(destination));
        CHECK_FALSE(dirty(session));
        CHECK(library.loaded(previousId) == nullptr);
        REQUIRE(app::replaceSessionDocument(library, session, id, previousId, {}));
        CHECK(session.look() == saveFixture().look);
        CHECK_FALSE(dirty(session));
    }
    SECTION("revert reloads even the same identity") {
        REQUIRE(app::replaceSessionDocument(library, session, id, previousId, before));
        CHECK(invalidated);
        CHECK(id == previousId);
        CHECK(session.look() == saveFixture().look);
        CHECK_FALSE(dirty(session));
    }
}

//======================================================================================================================
TEST_CASE("session reload preserves the operator view only when its subject survives",
          "[app][document-save][session]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    auto id = scenes::sceneIdFromPath(savePath("session-view"));
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    session.camera().position = {12.0f, 4.0f, -3.0f};
    session.camera().yaw = 0.4f;
    const auto camera = session.camera();
    app::EditorSelection selection{.sceneId = id, .subject = app::EditorSubject::Camera};
    auto changed = library.loaded(id)->document;
    CHECK(app::canPreserveSessionSelection(selection, *library.loaded(id), changed));
    REQUIRE(app::replaceSessionDocumentPreservingView(library, session, id, selection));
    CHECK(selection.subject == app::EditorSubject::Camera);
    CHECK(session.camera().position == camera.position);
    CHECK(session.camera().yaw == camera.yaw);

    const auto active = session.activeScene();
    const auto unchanged = library.loaded(id)->document;
    CHECK_FALSE(app::replaceSessionDocumentPreservingView(
        library, session, id, selection, unchanged, library.loaded(id)->hash,
        [](const fs::path&) -> asset::AssetResult<std::string> {
            return std::string("late-disk-hash");
        }));
    CHECK(session.activeScene() == active);
    CHECK(selection.subject == app::EditorSubject::Camera);
    CHECK(session.camera().position == camera.position);
    CHECK_FALSE(app::replaceSessionDocumentPreservingView(
        library, session, id, selection, library.loaded(id)->document, "wrong-hash"));
    CHECK(session.activeScene() == active);
    CHECK(selection.subject == app::EditorSubject::Camera);
    CHECK(session.camera().position == camera.position);

    selection.subject = app::EditorSubject::Group;
    selection.node = 0;
    changed.nodes.clear();
    CHECK_FALSE(app::canPreserveSessionSelection(selection, *library.loaded(id), changed));
    const auto retained = session.activeScene();
    CHECK_FALSE(
        app::replaceSessionDocumentPreservingView(library, session, id, selection, changed));
    CHECK(session.activeScene() == retained);
    CHECK(selection.subject == app::EditorSubject::Group);
    CHECK(session.camera().position == camera.position);

    if (!session.scene().objects.empty()) {
        selection.subject = app::EditorSubject::Object;
        selection.index = 0;
        selection.node = library.loaded(id)->binding.objectNode[0];
        CHECK_FALSE(app::canPreserveSessionSelection(selection, *library.loaded(id), changed));
    }
}

//======================================================================================================================
TEST_CASE("Save As retires a cached destination without invalidating the live source",
          "[app][document-save]") {
    FakeDevice device;
    scenes::SceneLibrary library(device);
    const auto source = savePath("alias-source");
    auto id = scenes::sceneIdFromPath(source);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto* loaded = session.loadedScene();
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    REQUIRE(app::saveSessionDocument(library, session, id, source, false));
    CHECK(library.loaded(id) == loaded);
    CHECK_FALSE(dirty(session));
    const auto target = savePath("cached-target");
    const auto targetId = scenes::sceneIdFromPath(target);
    REQUIRE(library.get(targetId));
    auto* oldTarget = library.loaded(targetId)->scene.get();
    session.activate(*library.loaded(targetId), app::SceneActivationMotion::Reset);
    session.activate(*loaded, app::SceneActivationMotion::Reset);
    REQUIRE(app::saveSessionDocument(library, session, id, target, true));
    CHECK(session.loadedScene() == loaded);
    CHECK(session.activeScene() != oldTarget);
    CHECK(library.loaded(targetId) == loaded);
    CHECK_FALSE(dirty(session));
}

//======================================================================================================================
TEST_CASE("Save preserves immutable imported poses and generated session defaults",
          "[app][document-save]") {
    const auto path = savePath("imported");
    const auto root = path.parent_path();
    const auto meshPath = test::writeAnimatedQuadGltf(root / "Assets" / "static", "LINEAR", true);
    const auto bytes = readWholeFile(meshPath);
    REQUIRE(bytes);
    std::string mesh(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto animationStart = mesh.find("  \"animations\":");
    const auto animationEnd = mesh.find("  \"buffers\":", animationStart);
    REQUIRE(animationStart != std::string::npos);
    REQUIRE(animationEnd != std::string::npos);
    mesh.erase(animationStart, animationEnd - animationStart);
    {
        std::ofstream out(meshPath);
        out << mesh;
    }
    auto doc = saveFixture();
    doc.nodes.push_back(
        {.name = "Static asset",
         .asset = asset::DocAsset{"static/quad.gltf", sha256Hex(*readWholeFile(meshPath))}});
    doc.nodes.push_back(
        {.name = "Generator",
         .generator = asset::DocGenerator{"light-lab", {{"lights", 1}, {"pile", 0}}}});
    doc.rootNodes.push_back(3);
    doc.rootNodes.push_back(4);
    REQUIRE(asset::saveSceneDocument(doc, path));
    const auto oldCwd = fs::current_path();
    FakeDevice device;
    scenes::SceneLibrary library(device);
    auto id = scenes::sceneIdFromPath(path);
    fs::current_path(root);
    const auto result = library.get(id);
    fs::current_path(oldCwd);
    INFO((result ? "loaded" : result.error().message));
    REQUIRE(result);
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    const auto& bindings = session.loadedScene()->binding;
    const auto imported = std::find_if(bindings.importedNodes.begin(), bindings.importedNodes.end(),
                                       [](const auto& node) { return !node.objects.empty(); });
    REQUIRE(imported != bindings.importedNodes.end());
    const auto importedIndex = static_cast<uint32_t>(imported - bindings.importedNodes.begin());
    const auto object = imported->objects.front();
    const auto original = session.objectDefault(object);
    REQUIRE(session.documentState().importedPoseBaseline[importedIndex]);
    const auto immutable = *session.documentState().importedPoseBaseline[importedIndex];
    auto edited = original;
    edited.position.x += 5;
    session.editObject(object, edited);
    REQUIRE(session.setObjectEnabled(object, false));
    REQUIRE(session.setNodeEnabled(3, false));
    const auto generated = std::find_if(
        session.scene().objects.begin(), session.scene().objects.end(), [&](const auto& entry) {
            return session.isGenerated(app::EditorSubject::Object,
                                       &entry - session.scene().objects.data(), {});
        });
    REQUIRE(generated != session.scene().objects.end());
    const auto generatedIndex = static_cast<size_t>(generated - session.scene().objects.begin());
    const auto generatedDefault = session.objectDefault(generatedIndex);
    auto generatedEdit = generatedDefault;
    generatedEdit.position.x += 10;
    session.editObject(generatedIndex, generatedEdit);
    REQUIRE(app::saveSessionDocument(library, session, id, path, false));
    REQUIRE_FALSE(dirty(session));
    CHECK_FALSE(session.objectEnabled(object));
    CHECK_FALSE(session.nodeEnabled(3));
    CHECK(session.documentState().importedPoseBaseline[importedIndex]->translation ==
          immutable.translation);
    CHECK(session.objectDefault(object).position == edited.position);
    CHECK(session.objectDefault(generatedIndex).position == generatedDefault.position);
    session.editObject(object, original);
    REQUIRE(dirty(session));
    session.resetObject(object);
    CHECK_FALSE(dirty(session));
    auto destination = path.parent_path() / "imported-copy.scene.gltf";
    REQUIRE(app::saveSessionDocument(library, session, id, destination, true));
    CHECK_FALSE(dirty(session));
    CHECK(session.documentState().importedPoseBaseline[importedIndex]->translation ==
          immutable.translation);
    CHECK(session.objectDefault(generatedIndex).position == generatedDefault.position);
}

//======================================================================================================================
TEST_CASE("Save As protects the actual padded alternate source buffer",
          "[app][document-save][document-repair]") {
    const auto source = savePath("alternate-source");
    // The source names its own padded buffer; the canonical companion beside it does not exist yet.
    fs::remove(fs::path(source).replace_extension(".bin"));
    const auto sourceBuffer = source.parent_path() / "alternate copy.scene.bin";
    auto doc = saveFixture();
    doc.animations = {{.name = "Camera rail",
                       .sampleRate = 60,
                       .keyCount = 2,
                       .channels = {{.node = 0,
                                     .path = asset::DocChannelPath::Translation,
                                     .values = {{0, 1, 4, 0}, {1, 1, 4, 0}}}}}};
    auto json = asset::sceneDocumentJson(doc, sourceBuffer.filename().string());
    const auto canonicalBuffer = asset::sceneDocumentBuffer(doc);
    const auto lengthKey = json.find("\"byteLength\": ", json.find("\"buffers\""));
    REQUIRE(lengthKey != std::string::npos);
    const auto lengthStart = lengthKey + 14;
    const auto lengthEnd = json.find_first_not_of("0123456789", lengthStart);
    json.replace(lengthStart, lengthEnd - lengthStart, std::to_string(canonicalBuffer.size() + 4));
    size_t offsetStart = 0;
    while ((offsetStart = json.find("\"byteOffset\": ", offsetStart)) != std::string::npos) {
        offsetStart += 14;
        const auto offsetEnd = json.find_first_not_of("0123456789", offsetStart);
        const auto offset = std::stoul(json.substr(offsetStart, offsetEnd - offsetStart));
        const auto value = std::to_string(offset + 4);
        json.replace(offsetStart, offsetEnd - offsetStart, value);
        offsetStart += value.size();
    }
    {
        std::ofstream file(source);
        file << json;
    }
    {
        std::ofstream file(sourceBuffer, std::ios::binary);
        const std::array<char, 4> padding{'P', 'A', 'D', '!'};
        file.write(padding.data(), padding.size());
        file.write(reinterpret_cast<const char*>(canonicalBuffer.data()), canonicalBuffer.size());
    }
    const auto originalRead = asset::readSceneDocument(source);
    INFO((originalRead ? "read" : originalRead.error().message));
    REQUIRE(originalRead);
    CHECK(originalRead->sourceBufferUri == sourceBuffer.filename().string());
    auto otherProvenance = *originalRead;
    otherProvenance.sourceBufferUri = "unrelated.bin";
    CHECK_FALSE(scenes::documentDirty(*originalRead, otherProvenance));
    CHECK(asset::sceneDocumentJson(*originalRead, "same.bin") ==
          asset::sceneDocumentJson(otherProvenance, "same.bin"));
    const auto originalJson = *readWholeFile(source);
    const auto originalBin = *readWholeFile(sourceBuffer);
    FakeDevice device;
    scenes::SceneLibrary library(device);
    auto id = scenes::sceneIdFromPath(source);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto* loaded = session.loadedScene();
    auto* live = session.activeScene();
    const auto oldId = id;
    const auto oldHash = loaded->hash;
    const auto oldPath = loaded->path;
    const auto oldModel = asset::sceneDocumentJson(loaded->document, "same.bin");
    const auto baseline = session.lookDefault();
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    auto destination = source.parent_path() / "alternate copy.scene.gltf";
    bool collision = true;
    SECTION("different document name but same real companion") {}
    SECTION("equivalent hardlink of the decoded source URI") {
        destination = source.parent_path() / "alternate-alias.scene.gltf";
        auto alias = destination;
        alias.replace_extension(".bin");
        std::error_code error;
        fs::remove(alias, error);
        fs::create_hard_link(sourceBuffer, alias);
    }
    SECTION("noncolliding Save As remains available") {
        destination = source.parent_path() / "alternate-independent.scene.gltf";
        collision = false;
    }
    SECTION("ordinary Save can canonicalize the current source") {
        REQUIRE(app::saveSessionDocument(library, session, id, source, false));
        REQUIRE_FALSE(dirty(session));
        REQUIRE(asset::readSceneDocument(source));
        CHECK(loaded->document.sourceBufferUri == "alternate-source.scene.bin");
        CHECK(*readWholeFile(sourceBuffer) == originalBin);
        return;
    }
    const auto saved = app::saveSessionDocument(library, session, id, destination, true);
    if (collision) {
        CHECK_FALSE(saved);
        CHECK(id == oldId);
        CHECK(session.loadedScene() == loaded);
        CHECK(session.activeScene() == live);
        CHECK(loaded->path == oldPath);
        CHECK(loaded->hash == oldHash);
        CHECK(asset::sceneDocumentJson(loaded->document, "same.bin") == oldModel);
        CHECK(session.lookDefault() == baseline);
        CHECK(dirty(session));
    } else {
        REQUIRE(saved);
        CHECK(id == scenes::sceneIdFromPath(destination));
        CHECK_FALSE(dirty(session));
        CHECK(asset::readSceneDocument(destination));
        CHECK(loaded->document.sourceBufferUri == "alternate-independent.scene.bin");
    }
    CHECK(*readWholeFile(source) == originalJson);
    CHECK(*readWholeFile(sourceBuffer) == originalBin);
    CHECK(asset::readSceneDocument(source));
}

//======================================================================================================================
TEST_CASE("queued Quit waits for actual Save As adoption or preserves a failed live document",
          "[app][document-save][document-repair]") {
    const auto source = savePath("queued-save-source");
    const auto destination = source.parent_path() / "queued-save-destination.scene.gltf";
    std::error_code ignored;
    fs::remove(destination, ignored);
    FakeDevice device;
    scenes::SceneLibrary library(device);
    auto id = scenes::sceneIdFromPath(source);
    REQUIRE(library.get(id));
    app::SceneSession session;
    session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
    auto* loaded = session.loadedScene();
    auto* live = session.activeScene();
    const auto oldId = id;
    const auto oldHash = loaded->hash;
    const auto oldDefault = session.lookDefault();
    const auto oldBytes = *readWholeFile(source);
    auto look = session.look();
    look.bloom.intensity += 1;
    session.editLook(look);
    app::SceneDocumentSaveIO io;
    bool succeeds = true;
    SECTION("successful Save As writes and adopts before Quit") {}
    SECTION("write failure keeps old dirty document for confirmation") {
        succeeds = false;
        io.write = [](const auto&, const auto&) -> asset::AssetResult<void> {
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io, "write failed"});
        };
    }
    SECTION("canonical read failure never adopts partial saved metadata") {
        succeeds = false;
        io.read = [](const auto&) -> asset::AssetResult<asset::SceneDocument> {
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io, "read failed"});
        };
    }
    app::DocumentWorkflow workflow;
    app::DocumentDialogMailbox mailbox;
    workflow.setContext(dirty(session), true, false);
    REQUIRE(workflow.request(app::DocumentAction::SaveAs));
    REQUIRE(mailbox.begin());
    REQUIRE(workflow.request(app::DocumentAction::Quit));
    int saves = 0;
    int quits = 0;
    const auto execute = [&](const app::PendingDocumentWork& work) {
        if (work.action == app::DocumentAction::Quit) {
            ++quits;
            CHECK(saves == 1);
            CHECK(id == scenes::sceneIdFromPath(destination));
            CHECK(asset::readSceneDocument(destination));
            CHECK_FALSE(dirty(session));
            return true;
        }
        ++saves;
        CHECK(work.action == app::DocumentAction::SaveAs);
        CHECK(work.path == destination);
        const auto result = app::saveSessionDocument(library, session, id, *work.path, true, io);
        workflow.setContext(dirty(session), true, false);
        return result.has_value();
    };
    CHECK_FALSE(app::pumpDocumentWork(workflow, mailbox, execute, {}));
    CHECK(saves == 0);
    CHECK(quits == 0);
    mailbox.post({.path = destination});
    CHECK(app::pumpDocumentWork(workflow, mailbox, execute, {}) == succeeds);
    CHECK(saves == 1);
    CHECK(quits == (succeeds ? 1 : 0));
    CHECK(session.loadedScene() == loaded);
    CHECK(session.activeScene() == live);
    CHECK(*readWholeFile(source) == oldBytes);
    if (succeeds) {
        CHECK(loaded->path == destination);
        CHECK(loaded->hash == *asset::sceneDocumentHash(destination));
        CHECK(session.lookDefault() == look);
    } else {
        CHECK(id == oldId);
        CHECK(loaded->path == source);
        CHECK(loaded->hash == oldHash);
        CHECK(session.lookDefault() == oldDefault);
        CHECK(dirty(session));
        CHECK(workflow.step() == app::WorkflowStep::Confirm);
        CHECK(workflow.action() == app::DocumentAction::Quit);
    }
    CHECK_FALSE(app::pumpDocumentWork(workflow, mailbox, execute, {}));
    CHECK(saves == 1);
    CHECK(quits == (succeeds ? 1 : 0));
}

//======================================================================================================================
TEST_CASE("confirmed Save persists exposure before Open chooser cancellation",
          "[app][document-save][save-before-open]") {
    for (bool succeeds : {false, true}) {
        for (bool queuedQuit : {false, true}) {
            for (bool nativeError : {false, true}) {
                CAPTURE(succeeds, queuedQuit, nativeError);
                const auto source = savePath("save-before-open");
                auto document = saveFixture();
                document.look.exposure.ev = .86f;
                REQUIRE(asset::saveSceneDocument(document, source));
                auto companion = source;
                companion.replace_extension(".bin");
                const auto originalJson = *readWholeFile(source);
                const auto originalBin = *readWholeFile(companion);
                FakeDevice device;
                scenes::SceneLibrary library(device);
                auto id = scenes::sceneIdFromPath(source);
                REQUIRE(library.get(id));
                app::SceneSession session;
                session.activate(*library.loaded(id), app::SceneActivationMotion::Reset);
                auto* loaded = session.loadedScene();
                const auto oldHash = loaded->hash;
                auto edited = session.look();
                edited.exposure.ev = 1.54f;
                session.editLook(edited);
                REQUIRE(dirty(session));
                app::DocumentWorkflow flow;
                app::DocumentDialogMailbox mailbox;
                flow.setContext(dirty(session), true, false);
                REQUIRE(flow.request(app::DocumentAction::Open));
                flow.confirm(app::ConfirmChoice::Save);
                int saves = 0;
                int opens = 0;
                int quits = 0;
                int errors = 0;
                app::SceneDocumentSaveIO io;
                if (!succeeds)
                    io.write = [](const auto&, const auto&) -> asset::AssetResult<void> {
                        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io,
                                                                 "save before Open failed"});
                    };
                const auto execute = [&](const app::PendingDocumentWork& work) {
                    if (work.action == app::DocumentAction::Quit) {
                        ++quits;
                        CHECK(saves == 1);
                        CHECK_FALSE(dirty(session));
                        return true;
                    }
                    if (work.action == app::DocumentAction::Open) {
                        ++opens;
                        return true;
                    }
                    REQUIRE(work.action == app::DocumentAction::Save);
                    ++saves;
                    if (queuedQuit)
                        REQUIRE(flow.request(app::DocumentAction::Quit));
                    const auto saved =
                        app::saveSessionDocument(library, session, id, source, false, io);
                    flow.setContext(dirty(session), true, false);
                    return saved.has_value();
                };
                const auto reportError = [&](const std::string&) { ++errors; };
                CHECK_FALSE(app::pumpDocumentWork(flow, mailbox, execute, reportError));
                CHECK(saves == 1);
                CHECK(opens == 0);
                CHECK(quits == 0);
                CHECK(session.loadedScene() == loaded);
                const auto disk = asset::readSceneDocument(source);
                REQUIRE(disk);
                CHECK(disk->look.exposure.ev == (succeeds ? 1.54f : .86f));
                CHECK(dirty(session) == !succeeds);
                if (!succeeds) {
                    CHECK(flow.step() ==
                          (queuedQuit ? app::WorkflowStep::Confirm : app::WorkflowStep::Idle));
                    CHECK(loaded->hash == oldHash);
                    CHECK(*readWholeFile(source) == originalJson);
                    CHECK(*readWholeFile(companion) == originalBin);
                    CHECK(session.lookDefault().exposure.ev == .86f);
                    CHECK_FALSE(mailbox.pending());
                    continue;
                }
                REQUIRE(flow.step() == app::WorkflowStep::ChoosePath);
                CHECK(session.lookDefault().exposure.ev == 1.54f);
                CHECK(loaded->hash == *asset::sceneDocumentHash(source));
                REQUIRE(mailbox.begin());
                CHECK_FALSE(app::pumpDocumentWork(flow, mailbox, execute, reportError));
                CHECK(quits == 0);
                mailbox.post({.error = nativeError ? "native Open failed" : ""});
                CHECK(app::pumpDocumentWork(flow, mailbox, execute, reportError) == queuedQuit);
                CHECK(saves == 1);
                CHECK(opens == 0);
                CHECK(quits == (queuedQuit ? 1 : 0));
                CHECK(errors == (nativeError ? 1 : 0));
                CHECK_FALSE(dirty(session));
                CHECK(asset::readSceneDocument(source)->look.exposure.ev == 1.54f);
                CHECK_FALSE(app::pumpDocumentWork(flow, mailbox, execute, reportError));
                CHECK(saves == 1);
                CHECK(quits == (queuedQuit ? 1 : 0));
            }
        }
    }
}
