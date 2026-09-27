#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"
#include "Scenes/SceneLibrary.h"
#include "Support/GpuTestSupport.h"
#include <fstream>

//======================================================================================================================
TEST_CASE("every catalog document instantiates and Sponza retains its lights and rail",
          "[gpu][scene-doc]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    lmx::scenes::SceneLibrary library(**device);
    for (const auto& entry : library.entries()) {
        INFO(entry.stableId);
        REQUIRE(entry.available);
        auto scene = library.get(entry.id);
        INFO((scene ? "ok" : scene.error().message));
        REQUIRE(scene);
        auto* loaded = library.loaded(entry.id);
        REQUIRE(loaded != nullptr);
        REQUIRE(loaded->scene.get() == *scene);
        REQUIRE_FALSE(loaded->hash.empty());
        REQUIRE(loaded->binding.objectNode.size() == (*scene)->objects.size());
        if (entry.stableId == "sponza") {
            REQUIRE((*scene)->localLights().size() == 16);
            REQUIRE((*scene)->animation.cameraTrack.size() == 7201);
            REQUIRE(loaded->binding.localLightGroup.has_value());
        }
    }
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document reload failure preserves activation and success invalidates old pointer state",
          "[gpu][scene-doc]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scenes::SceneLibrary library(**device);
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    document->nodes[0].generator->params = {{"lights", 1}, {"pile", 0}};
    const auto path = std::filesystem::current_path() / "SceneDocuments" / "reload.scene.gltf";
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(asset::saveSceneDocument(*document, path));
    const auto id = scenes::sceneIdFromPath(path);
    REQUIRE(library.get(id));
    auto* original = library.loaded(id);
    auto* oldScene = original->scene.get();
    const auto oldHash = original->hash;
    app::SceneSession session;
    session.activate(*original, app::SceneActivationMotion::Reset);
    auto selection = app::initialSelection(id);
    const auto invalidate = [&](const engine::LoadedScene& old) {
        REQUIRE(old.scene.get() == oldScene);
        REQUIRE(session.activeScene() == oldScene);
        session.invalidate(*old.scene);
        selection = {};
    };
    {
        std::ofstream broken(path);
        broken << "{broken";
    }
    const auto failed = library.reload(id, invalidate);
    REQUIRE_FALSE(failed);
    REQUIRE(session.activeScene() == oldScene);
    REQUIRE(library.loaded(id)->hash == oldHash);
    REQUIRE(library.get(id).value() == oldScene);
    auto overflow = *document;
    overflow.nodes[0].generator->params = {{"lights", 4096}, {"pile", 0}};
    const auto extraNode = static_cast<uint32_t>(overflow.nodes.size());
    auto extra = overflow.nodes[0];
    extra.generator->params = {{"lights", 1}, {"pile", 0}};
    overflow.nodes.push_back(extra);
    overflow.rootNodes.push_back(extraNode);
    REQUIRE(asset::saveSceneDocument(overflow, path));
    const auto capacityFailure = library.reload(id, invalidate);
    REQUIRE_FALSE(capacityFailure);
    REQUIRE(session.activeScene() == oldScene);
    REQUIRE(library.loaded(id)->hash == oldHash);
    REQUIRE(session.documentState().nodeEnabled.size() == document->nodes.size());
    REQUIRE(selection.sceneId == id);
    REQUIRE(selection.subject == app::EditorSubject::Camera);
    document->name = "Reloaded scene";
    document->look.bloom.intensity = 0.3f;
    REQUIRE(asset::saveSceneDocument(*document, path));
    const auto replacement = library.reload(id, invalidate);
    INFO((replacement ? "ok" : replacement.error().message));
    REQUIRE(replacement);
    REQUIRE(session.activeScene() == nullptr);
    REQUIRE((*replacement)->scene.get() != oldScene);
    REQUIRE((*replacement)->document.name == "Reloaded scene");
    REQUIRE((*replacement)->scene->look.bloom.intensity == 0.3f);
    REQUIRE((*replacement)->hash != oldHash);
    REQUIRE((*replacement)->path == path);
    REQUIRE((*replacement)->binding.objectNode.size() == (*replacement)->scene->objects.size());
    session.activate(**replacement, app::SceneActivationMotion::Reset);
    REQUIRE(session.loadedScene() == *replacement);
    REQUIRE(session.documentState().nodeEnabled.size() == document->nodes.size());
}

//======================================================================================================================
TEST_CASE("document assets append with distinct handles, source nodes, clips and combined bounds",
          "[gpu][scene-doc]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto document = scenes::readCatalogDocument("temporal-lab");
    REQUIRE(document);
    constexpr uint32_t firstIndex = 5;
    const auto secondIndex = static_cast<uint32_t>(document->nodes.size());
    auto second = document->nodes[firstIndex];
    second.name = "Second truck";
    second.translation.x = 100;
    second.enabled = false;
    document->nodes.push_back(second);
    document->rootNodes.push_back(secondIndex);
    const auto path = std::filesystem::current_path() / "SceneDocuments" / "two-assets.scene.gltf";
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(asset::saveSceneDocument(*document, path));
    auto loaded = scenes::loadSceneDocument(**device, path);
    INFO((loaded ? "ok" : loaded.error().message));
    REQUIRE(loaded);
    const auto& binding = loaded->binding;
    REQUIRE(binding.assets.size() == 2);
    const auto firstBase = binding.assets[0].objectBase;
    REQUIRE(firstBase > 0); // TemporalLab's generated motion comes before both asset nodes.
    const auto count = binding.assets[1].objectBase - firstBase;
    const auto secondBase = binding.assets[1].objectBase;
    REQUIRE(count > 0);
    REQUIRE(loaded->scene->objects.size() == firstBase + count * 2);
    REQUIRE(loaded->scene->objects[firstBase].mesh != loaded->scene->objects[secondBase].mesh);
    REQUIRE(binding.objectNode[firstBase] == firstIndex);
    REQUIRE(binding.objectNode[secondBase] == secondIndex);
    REQUIRE(binding.objectImportedNode[firstBase] != binding.objectImportedNode[secondBase]);
    REQUIRE_FALSE(binding.assets[0].clips.empty());
    REQUIRE(binding.assets[1].clips[0].duration == binding.assets[0].clips[0].duration);
    REQUIRE(loaded->scene->authoredBounds.maximum.x > 90);
    for (size_t i = secondBase; i < secondBase + count; ++i)
        REQUIRE_FALSE(binding.objectEffective[i]);
    for (const auto& node : binding.importedNodes) {
        REQUIRE(node.enabled);
        if (node.assetRoot == secondIndex)
            REQUIRE_FALSE(node.effective);
    }
    bool rebased = false;
    for (const auto& track : loaded->scene->animation.tracks)
        rebased = rebased || track.objectIndex >= secondBase;
    REQUIRE(rebased);
    REQUIRE(loaded->scene->animation.duration == 24.0);
    REQUIRE(loaded->scene->assetAnimations.size() == 2);
    (*device)->waitIdle();
}
