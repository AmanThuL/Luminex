#pragma once
#include "Scenes/SceneDocuments.h"
#include <catch2/catch_test_macros.hpp>

namespace lmx::test {
//======================================================================================================================
inline asset::AssetResult<std::unique_ptr<engine::Scene>>
loadCatalogScene(rojoRHI::Device& device, std::string_view id, std::optional<uint32_t> first = {},
                 std::optional<uint32_t> second = {}) {
    auto path = scenes::catalogDocumentPath(id);
    if (!path)
        return std::unexpected(path.error());
    scenes::GeneratorOverrides overrides;
    if (id == "visibility-lab") {
        overrides.instances = first;
        overrides.occluders = second;
    }
    if (id == "light-lab") {
        overrides.lights = first;
        overrides.pile = second;
    }
    auto loaded = scenes::loadSceneDocument(device, *path, overrides);
    if (!loaded)
        return std::unexpected(loaded.error());
    return std::move(loaded->scene);
}
//======================================================================================================================
inline engine::Scene catalogCamera(std::string_view id) {
    auto document = scenes::readCatalogDocument(id);
    REQUIRE(document);
    engine::Scene scene;
    engine::applyDocumentCamera(scene, *document);
    scene.animation.loop = document->loop;
    return scene;
}
//======================================================================================================================
inline engine::LoadedScene documentLightFixture() {
    auto document = scenes::readCatalogDocument("sponza");
    REQUIRE(document);
    for (auto& node : document->nodes)
        node.asset.reset();
    auto prepared = engine::prepareSceneDocument(*document, std::filesystem::current_path());
    REQUIRE(prepared);
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>(),
                               .document = std::move(*document)};
    loaded.scene->name = "Document lights";
    loaded.binding.nodes.resize(loaded.document.nodes.size());
    std::vector<engine::LightId> ids;
    for (uint32_t n = 0; n < loaded.document.nodes.size(); ++n) {
        if (loaded.document.nodes[n].name == "Local Lights")
            loaded.binding.localLightGroup = n;
        if (!prepared->localLights[n])
            continue;
        auto id = loaded.scene->addLight(*prepared->localLights[n]);
        REQUIRE(id);
        ids.push_back(*id);
        loaded.binding.nodes[n].light = *id;
        loaded.binding.lightNode.emplace(engine::sceneLightKey(*id), n);
    }
    loaded.scene->setRigLightIds(std::move(ids));
    return loaded;
}
} // namespace lmx::test
