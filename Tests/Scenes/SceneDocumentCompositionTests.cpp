#include "App/Model/Options/AppOptions.h"
#include "App/Model/Scene/SceneSession.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"
#include "Scenes/SceneLibrary.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"
#include <array>

//======================================================================================================================
TEST_CASE("retired lab generators name the schema 2 retirement in both schemas",
          "[scene-doc][composition][ux6-retirement]") {
    using namespace lmx;
    for (const uint32_t schema : {1u, 2u}) {
        for (const std::string name : {"material-lab", "temporal-lab"}) {
            DYNAMIC_SECTION("schema " << schema << " generator " << name) {
                asset::SceneDocument document;
                document.schemaVersion = schema;
                document.nodes.push_back({.generator = asset::DocGenerator{.name = name}});
                const auto result = scenes::validateSceneGenerators(document, {});
                REQUIRE_FALSE(result);
                REQUIRE(result.error().code == asset::AssetErrorCode::Unsupported);
                REQUIRE(result.error().message ==
                        "/nodes/0/extensions/LMX_scene/generator/name: generator '" + name +
                            "' was retired in schema 2; its objects are saved in the document");
            }
        }
        for (const std::string name : {"light-lab", "visibility-lab"}) {
            DYNAMIC_SECTION("schema " << schema << " axisStation on " << name) {
                asset::SceneDocument document;
                document.schemaVersion = schema;
                document.nodes.push_back({.generator = asset::DocGenerator{
                                              .name = name, .params = {{"axisStation", 1}}}});
                const auto result = scenes::validateSceneGenerators(document, {});
                REQUIRE_FALSE(result);
                REQUIRE(result.error().code == asset::AssetErrorCode::Unsupported);
                REQUIRE(
                    result.error().message ==
                    "/nodes/0/extensions/LMX_scene/generator/params/axisStation: generator "
                    "'axisStation' was retired in schema 2; its objects are saved in the document");
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("document generator validation counts every generated and authored light",
          "[scene-doc][composition]") {
    using namespace lmx;
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    const auto generatorNode = test::documentGeneratorNode(*document, "light-lab");
    document->nodes[generatorNode].generator->params = {{"lights", 4096}, {"pile", 0}};
    auto second = document->nodes[generatorNode];
    second.generator->params = {{"lights", 1}, {"pile", 0}};
    const auto index = static_cast<uint32_t>(document->nodes.size());
    document->nodes.push_back(second);
    document->rootNodes.push_back(index);
    auto result = scenes::validateSceneGenerators(*document, {});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/" + std::to_string(index) +
                                        "/extensions/LMX_scene/generator/params") !=
            std::string::npos);
    document->nodes[index].generator.reset();
    document->nodes[index].light = static_cast<uint32_t>(document->lights.size());
    document->lights.push_back({.type = asset::DocLightType::Point, .range = 1.0f});
    result = scenes::validateSceneGenerators(*document, {});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("/nodes/" + std::to_string(index)) != std::string::npos);
    document->nodes[generatorNode].generator->params[0].second = 4095;
    REQUIRE(scenes::validateSceneGenerators(*document, {}));
    // A shared definition still creates another identity for each referencing node.
    document->nodes.push_back(document->nodes[index]);
    document->rootNodes.push_back(index + 1);
    REQUIRE_FALSE(scenes::validateSceneGenerators(*document, {}));
    document->nodes.back().enabled = false;
    REQUIRE_FALSE(scenes::validateSceneGenerators(*document, {}));
}

//======================================================================================================================
TEST_CASE("partial light CLI masks combine with authored document parameters",
          "[scene-doc][composition][options]") {
    using namespace lmx;
    const std::array<std::string_view, 4> args{"--scene", "custom.scene.gltf", "--lab-light-pile",
                                               "4000"};
    const auto options = app::parseAppOptions(args);
    REQUIRE(options);
    REQUIRE_FALSE(options->generatorOverrides.lights);
    REQUIRE(options->generatorOverrides.pile == 4000);
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    const auto generatorNode = test::documentGeneratorNode(*document, "light-lab");
    document->nodes[generatorNode].generator->params = {{"lights", 1}, {"pile", 8}};
    REQUIRE(scenes::validateSceneGenerators(*document, options->generatorOverrides));
    document->nodes[generatorNode].generator->params[0].second = 97;
    REQUIRE_FALSE(scenes::validateSceneGenerators(*document, options->generatorOverrides));
    const std::array<std::string_view, 4> lightsArgs{"--scene", "custom.scene.gltf", "--lab-lights",
                                                     "4090"};
    const auto lightsOptions = app::parseAppOptions(lightsArgs);
    REQUIRE(lightsOptions);
    REQUIRE_FALSE(lightsOptions->generatorOverrides.pile);
    REQUIRE_FALSE(scenes::validateSceneGenerators(*document, lightsOptions->generatorOverrides));
    document->nodes[generatorNode].generator->params[1].second = 6;
    REQUIRE(scenes::validateSceneGenerators(*document, lightsOptions->generatorOverrides));
}

//======================================================================================================================
TEST_CASE("document pile edits preserve authored lights and other generator populations",
          "[gpu][scene-doc][composition]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    const auto generatorNode = test::documentGeneratorNode(*document, "light-lab");
    document->nodes[generatorNode].generator->params = {{"lights", 1}, {"pile", 2}};
    const auto secondNode = static_cast<uint32_t>(document->nodes.size());
    auto second = document->nodes[generatorNode];
    second.generator->params = {{"lights", 2}, {"pile", 1}};
    document->nodes.push_back(second);
    document->rootNodes.push_back(secondNode);
    const auto authoredNode = static_cast<uint32_t>(document->nodes.size());
    const auto authoredLight = static_cast<uint32_t>(document->lights.size());
    document->lights.push_back(
        {.type = asset::DocLightType::Point, .intensity = 7.0, .range = 2.0f});
    document->nodes.push_back(
        {.name = "Authored light", .translation = {3, 4, 5}, .light = authoredLight});
    document->rootNodes.push_back(authoredNode);
    const auto path =
        std::filesystem::current_path() / "SceneDocuments" / "light-ownership.scene.gltf";
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(asset::saveSceneDocument(*document, path));
    auto loaded = scenes::loadSceneDocument(**device, path);
    REQUIRE(loaded);
    const auto id = *loaded->binding.nodes[authoredNode].light;
    const auto authored = *loaded->scene->light(id);
    const auto populations = loaded->scene->lightLabPopulations;
    REQUIRE(populations.size() == 2);
    REQUIRE(populations[0].documentNode == generatorNode);
    REQUIRE(populations[1].documentNode == secondNode);
    app::SceneSession session;
    session.activate(*loaded, app::SceneActivationMotion::Reset);
    REQUIRE(session.lightLabPileCount() == 2);
    for (uint32_t count : {0u, 4u, 0u}) {
        REQUIRE(session.setLightLabPile(count));
        REQUIRE(loaded->scene->light(id));
        REQUIRE(loaded->scene->light(id)->position == authored.position);
        REQUIRE(loaded->scene->light(id)->intensity == authored.intensity);
        REQUIRE(loaded->binding.lightNode.at(engine::sceneLightKey(id)) == authoredNode);
        for (const auto kept : populations[0].grid)
            REQUIRE(loaded->scene->light(kept));
        for (const auto kept : populations[1].grid)
            REQUIRE(loaded->scene->light(kept));
        for (const auto kept : populations[1].pile)
            REQUIRE(loaded->scene->light(kept));
        for (const auto added : loaded->scene->lightLabPopulations[0].pile)
            REQUIRE(loaded->binding.lightGeneratorNode.at(engine::sceneLightKey(added)) ==
                    generatorNode);
    }
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("document caster role survives binding with none and disabled roles supported",
          "[gpu][scene-doc][composition]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    for (int caster : {-1, 0, 1, 2}) {
        for (bool enabled : {false, true}) {
            auto document = scenes::readCatalogDocument("light-lab");
            REQUIRE(document);
            const auto generatorNode = test::documentGeneratorNode(*document, "light-lab");
            document->nodes[generatorNode].generator->params = {{"lights", 1}, {"pile", 0}};
            std::array<uint32_t, 3> roleNodes{};
            for (uint32_t n = 0; n < document->nodes.size(); ++n) {
                auto& node = document->nodes[n];
                if (!node.role)
                    continue;
                const int role = node.role == "fill" ? 1 : node.role == "rim" ? 2 : 0;
                roleNodes[role] = n;
                node.castsShadow = role == caster;
                node.enabled = role != caster || enabled;
            }
            const auto path =
                std::filesystem::current_path() / "SceneDocuments" / "caster.scene.gltf";
            std::filesystem::create_directories(path.parent_path());
            REQUIRE(asset::saveSceneDocument(*document, path));
            auto loaded = scenes::loadSceneDocument(**device, path);
            REQUIRE(loaded);
            REQUIRE(loaded->scene->shadowCaster ==
                    (caster < 0 ? std::optional<uint32_t>{} : caster));
            (*device)->beginFrame();
            REQUIRE(loaded->scene->prepareFrame((*device)->frameNumber()));
            std::vector<engine::DrawItem> items;
            const auto view = render::buildSceneView(*loaded->scene, items, false);
            REQUIRE(view.shadowCaster == (enabled ? caster : -1));
            for (uint32_t role = 0; role < 3; ++role) {
                REQUIRE(loaded->binding.nodes[roleNodes[role]].directional == role);
                REQUIRE(view.lights[role].direction == loaded->scene->lights[role].direction);
                if (int(role) == caster && !enabled)
                    REQUIRE(view.lights[role].strength == glm::vec3(0));
            }
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
        }
    }
}

//======================================================================================================================
TEST_CASE("direct LightLab append reports shared capacity without mutating geometry",
          "[scene-doc][composition]") {
    using namespace lmx;
    engine::Scene scene;
    for (uint32_t n = 0; n < engine::kMaxLocalLights; ++n)
        REQUIRE(scene.addLight(engine::LocalLight{}));
    bool environmentCalled = false;
    const auto result =
        scenes::appendLightLab(scene, 1, 0, [&](engine::Scene&) -> asset::AssetResult<void> {
            environmentCalled = true;
            return {};
        });
    REQUIRE_FALSE(result);
    REQUIRE(result.error().message.find("capacity") != std::string::npos);
    REQUIRE(scene.localLights().size() == engine::kMaxLocalLights);
    REQUIRE(scene.objects.empty());
    REQUIRE_FALSE(environmentCalled);
}

//======================================================================================================================
TEST_CASE("document missing directional roles stay inert in the rendered view",
          "[gpu][scene-doc][composition]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    const auto generatorNode = test::documentGeneratorNode(*document, "light-lab");
    document->nodes[generatorNode].generator->params = {{"lights", 1}, {"pile", 0}};
    for (auto& node : document->nodes) {
        if (!node.role)
            continue;
        node.light.reset();
        node.role.reset();
        node.castsShadow = false;
    }
    const auto path =
        std::filesystem::current_path() / "SceneDocuments" / "missing-roles.scene.gltf";
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(asset::saveSceneDocument(*document, path));
    auto loaded = scenes::loadSceneDocument(**device, path);
    REQUIRE(loaded);
    REQUIRE_FALSE(loaded->scene->shadowCaster);
    (*device)->beginFrame();
    REQUIRE(loaded->scene->prepareFrame((*device)->frameNumber()));
    std::vector<engine::DrawItem> items;
    const auto view = render::buildSceneView(*loaded->scene, items, false);
    REQUIRE(view.shadowCaster == -1);
    for (const auto& light : view.lights)
        REQUIRE(light.strength == glm::vec3(0));
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("LightLab append preserves the saved material field and its bounds",
          "[gpu][scene-doc][composition][ux6-retirement]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    engine::Scene scene;
    auto texture = makeProbeTarget(**device, "UX6.saved-field-texture");
    REQUIRE(texture);
    const auto textureId = scene.addTexture(std::move(*texture));
    const auto materialId = scene.addMaterial({.diffuse = textureId, .roughness = 0.85f});
    const auto meshId = scene.addMesh(engine::makeCube(), "UX6.saved-field-mesh");
    scene.addObject(
        {.name = "Saved fixture", .position = {1, 2, 3}, .mesh = meshId, .material = materialId});
    scene.authoredBounds = {{-12, -4, -8}, {16, 10, 20}};
    scene.boundingSphere = {2, 3, 4, 30};
    const auto objects = scene.objects;
    std::vector<const rojoRHI::Texture*> textures;
    std::vector<engine::MaterialRecord> materials;
    std::vector<const engine::MeshRow*> meshes;
    for (const auto& object : objects) {
        const auto& material = scene.material(object.material);
        materials.push_back(material);
        meshes.push_back(scene.tryMesh(object.mesh));
        for (const auto texture : {material.diffuse, material.normalMap, material.metallicRoughness,
                                   material.occlusion, material.emissiveMap})
            if (texture)
                textures.push_back(scene.tryTexture(*texture));
    }
    const auto tables = scene.tableStats();
    const auto bounds = scene.authoredBounds;
    const auto sphere = scene.boundingSphere;
    bool environmentCalled = false;
    const auto result =
        scenes::appendLightLab(scene, 8, 3, [&](engine::Scene&) -> asset::AssetResult<void> {
            environmentCalled = true;
            return {};
        });
    REQUIRE(result);
    REQUIRE(environmentCalled);
    REQUIRE(scene.objects.size() == objects.size());
    REQUIRE(scene.objects.size() == 1);
    size_t textureIndex = 0;
    for (size_t i = 0; i < objects.size(); ++i) {
        REQUIRE(scene.objects[i].name == objects[i].name);
        REQUIRE(scene.objects[i].position == objects[i].position);
        REQUIRE(scene.objects[i].mesh == objects[i].mesh);
        REQUIRE(scene.objects[i].material == objects[i].material);
        REQUIRE(scene.tryMesh(objects[i].mesh) == meshes[i]);
        const auto& material = scene.material(objects[i].material);
        REQUIRE(material.albedo == materials[i].albedo);
        REQUIRE(material.roughness == materials[i].roughness);
        REQUIRE(material.metallic == materials[i].metallic);
        REQUIRE(material.occlusionStrength == materials[i].occlusionStrength);
        REQUIRE(material.emissive == materials[i].emissive);
        REQUIRE(material.uvTransform == materials[i].uvTransform);
        REQUIRE(material.alphaMode == materials[i].alphaMode);
        REQUIRE(material.alphaCutoff == materials[i].alphaCutoff);
        REQUIRE(material.doubleSided == materials[i].doubleSided);
        REQUIRE(material.diffuse == materials[i].diffuse);
        REQUIRE(material.normalMap == materials[i].normalMap);
        REQUIRE(material.metallicRoughness == materials[i].metallicRoughness);
        REQUIRE(material.occlusion == materials[i].occlusion);
        REQUIRE(material.emissiveMap == materials[i].emissiveMap);
        for (const auto texture : {material.diffuse, material.normalMap, material.metallicRoughness,
                                   material.occlusion, material.emissiveMap})
            if (texture)
                REQUIRE(scene.tryTexture(*texture) == textures[textureIndex++]);
    }
    REQUIRE(scene.tableStats().materialCount == tables.materialCount);
    REQUIRE(scene.tableStats().meshCount == tables.meshCount);
    REQUIRE(scene.authoredBounds.minimum == bounds.minimum);
    REQUIRE(scene.authoredBounds.maximum == bounds.maximum);
    REQUIRE(scene.boundingSphere == sphere);
    REQUIRE(scene.localLights().size() == 11);
    REQUIRE(scene.animation.lightTracks.size() == 2);
    (*device)->waitIdle();
}
