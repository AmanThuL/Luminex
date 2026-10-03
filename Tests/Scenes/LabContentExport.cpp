#include "Scenes/LabContentCapture.h"

#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Engine/Scene/SceneTables.h"
#include "Scenes/CatalogScenes.h"
#include "Scenes/LightLab.h"
#include "Scenes/SceneDocuments.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <tuple>

using namespace lmx;
namespace fs = std::filesystem;
namespace {
//======================================================================================================================
void writeBytes(const fs::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out);
}
//======================================================================================================================
template <class T>
std::vector<T> readRows(rojoRHI::Buffer& buffer, size_t count) {
    std::vector<T> rows(count);
    if (count)
        buffer.readback(rows.data(), rows.size() * sizeof(T));
    return rows;
}
//======================================================================================================================
void exactBytes(std::string_view field, std::span<const std::byte> parent,
                std::span<const std::byte> candidate) {
    INFO(field);
    if (parent.size() != candidate.size())
        std::cout << std::format("MISMATCH {} size parent={} candidate={} delta={}\n", field,
                                 parent.size(), candidate.size(),
                                 int64_t(candidate.size()) - int64_t(parent.size()))
                  << std::flush;
    REQUIRE(parent.size() == candidate.size());
    const auto first = std::mismatch(parent.begin(), parent.end(), candidate.begin());
    if (first.first != parent.end()) {
        const size_t index = static_cast<size_t>(first.first - parent.begin());
        size_t different = 0;
        for (size_t i = 0; i < parent.size(); ++i)
            different += parent[i] != candidate[i];
        std::cout << std::format("MISMATCH {} firstByte={} parent={:02x} candidate={:02x} "
                                 "differentBytes={}/{}\n",
                                 field, index, std::to_integer<unsigned>(parent[index]),
                                 std::to_integer<unsigned>(candidate[index]), different,
                                 parent.size());
        if (parent.size() % 4 == 0) {
            uint32_t a, b;
            std::memcpy(&a, parent.data() + (index / 4) * 4, 4);
            std::memcpy(&b, candidate.data() + (index / 4) * 4, 4);
            std::cout << std::format(
                "word[{}] parentBits={:08x} candidateBits={:08x} "
                "parentFloat={:.9g} candidateFloat={:.9g} floatDelta={:.17g}\n",
                index / 4, a, b, std::bit_cast<float>(a), std::bit_cast<float>(b),
                double(std::bit_cast<float>(b)) - double(std::bit_cast<float>(a)));
        }
        std::cout << std::flush;
        FAIL("exact lab field differs; owner decision required");
    }
}
//======================================================================================================================
template <class T>
void exactValue(std::string_view field, const T& parent, const T& candidate) {
    exactBytes(field, std::as_bytes(std::span(&parent, 1)),
               std::as_bytes(std::span(&candidate, 1)));
}
//======================================================================================================================
std::vector<std::byte> texturePixels(rojoRHI::Device& device, rojoRHI::Texture& texture) {
    const uint64_t row = uint64_t{texture.width()} * 4;
    auto buffer = device.createBuffer({.size = row * texture.height(),
                                       .cpuReadback = true,
                                       .label = "lmx.test.labTextureReadback"},
                                      nullptr);
    REQUIRE(buffer);
    auto& commands = device.beginFrame();
    commands.beginCopyPass("lmx.test.labTextureCopy");
    commands.copyTextureToBuffer(texture, {.width = texture.width(), .height = texture.height()},
                                 **buffer, {.bytesPerRow = row});
    commands.endCopyPass();
    device.endFrame(nullptr);
    device.waitIdle();
    return readRows<std::byte>(**buffer, static_cast<size_t>(row * texture.height()));
}
//======================================================================================================================
void prepare(rojoRHI::Device& device, engine::Scene& scene) {
    device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()));
    device.endFrame(nullptr);
    device.waitIdle();
}
//======================================================================================================================
void snapshot(const fs::path& root, const engine::Scene& scene) {
    fs::create_directories(root);
    const auto tables = scene.tables();
    writeBytes(root / "vertices.bin",
               readRows<std::byte>(*tables.vertices, scene.tableStats().vertexBytes));
    writeBytes(root / "indices.bin",
               readRows<std::byte>(*tables.indices, scene.tableStats().indexBytes));
    writeBytes(
        root / "materials.bin",
        readRows<std::byte>(*tables.materials, tables.materialCount * sizeof(engine::MaterialRow)));
    std::ofstream poses(root / "objects.txt");
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const auto& o = scene.objects[i];
        poses << i << " " << o.name << " mesh=" << o.mesh.slot << " material=" << o.material.slot
              << "\n";
        for (const auto v : {o.position, o.eulerDegrees, o.scale})
            poses << std::format("[{:.9g}, {:.9g}, {:.9g}] [{:08x}, {:08x}, {:08x}]\n", v.x, v.y,
                                 v.z, std::bit_cast<uint32_t>(v.x), std::bit_cast<uint32_t>(v.y),
                                 std::bit_cast<uint32_t>(v.z));
    }
    REQUIRE(poses);
    for (size_t i = 0; i < scene.animation.tracks.size(); ++i) {
        const auto& keys = scene.animation.tracks[i].keys;
        writeBytes(root / ("rigid-" + std::to_string(i) + ".bin"), std::as_bytes(std::span(keys)));
    }
}
//======================================================================================================================
std::map<uint32_t, engine::TextureId> textureSlots(const engine::Scene& scene) {
    std::map<uint32_t, engine::TextureId> result;
    for (const auto& object : scene.objects) {
        const auto& material = scene.material(object.material);
        for (const auto id : {material.diffuse, material.normalMap, material.metallicRoughness,
                              material.occlusion, material.emissiveMap})
            if (id)
                result.emplace(id->slot, *id);
    }
    return result;
}
//======================================================================================================================
void compareObjects(const engine::Scene& parent, const engine::Scene& candidate,
                    std::span<const uint32_t> kept, double seconds) {
    REQUIRE(candidate.objects.size() == kept.size());
    for (size_t i = 0; i < kept.size(); ++i) {
        const auto& a = parent.objects[kept[i]];
        const auto& b = candidate.objects[i];
        const auto prefix = std::format("t={:.17g}/object[{}]/{}", seconds, i, a.name);
        INFO(prefix);
        REQUIRE(a.name == b.name);
        exactValue(prefix + "/position", a.position, b.position);
        exactValue(prefix + "/eulerDegrees", a.eulerDegrees, b.eulerDegrees);
        exactValue(prefix + "/scale", a.scale, b.scale);
        exactValue(prefix + "/model", a.modelMatrix(), b.modelMatrix());
        exactValue(prefix + "/enabled", a.enabled, b.enabled);
        exactValue(prefix + "/motionClass", a.motionClass, b.motionClass);
        exactValue(prefix + "/emissiveStrength", a.emissiveStrength, b.emissiveStrength);
        exactValue(prefix + "/meshSlot", a.mesh.slot, b.mesh.slot);
        exactValue(prefix + "/materialSlot", a.material.slot, b.material.slot);
    }
}
//======================================================================================================================
void compareMaterialBindings(const engine::Scene& parent, const engine::Scene& candidate,
                             std::span<const uint32_t> kept) {
    for (size_t i = 0; i < kept.size(); ++i) {
        const auto& a = parent.material(parent.objects[kept[i]].material);
        const auto& b = candidate.material(candidate.objects[i].material);
        const std::array bindings{
            std::tuple{"diffuse", a.diffuse, b.diffuse},
            std::tuple{"normalMap", a.normalMap, b.normalMap},
            std::tuple{"metallicRoughness", a.metallicRoughness, b.metallicRoughness},
            std::tuple{"occlusion", a.occlusion, b.occlusion},
            std::tuple{"emissiveMap", a.emissiveMap, b.emissiveMap}};
        for (const auto& [name, left, right] : bindings) {
            const auto field = std::format("object[{}]/material/{}", i, name);
            exactValue(field + "/present", left.has_value(), right.has_value());
            if (left)
                exactValue(field + "/textureSlot", left->slot, right->slot);
        }
    }
}
//======================================================================================================================
void compareTracks(const engine::Scene& parent, const engine::Scene& candidate,
                   std::span<const uint32_t> kept) {
    REQUIRE(parent.animation.tracks.size() == candidate.animation.tracks.size());
    for (size_t t = 0; t < parent.animation.tracks.size(); ++t) {
        const auto& a = parent.animation.tracks[t];
        const auto& b = candidate.animation.tracks[t];
        INFO(t);
        REQUIRE(kept[b.objectIndex] == a.objectIndex);
        exactValue("rigid/step", a.step, b.step);
        exactValue("rigid/loopDuration", a.loopDuration, b.loopDuration);
        REQUIRE(a.keys.size() == b.keys.size());
        for (size_t k = 0; k < a.keys.size(); ++k) {
            const auto prefix = std::format("rigid[{}]/key[{}]", t, k);
            exactValue(prefix + "/time", a.keys[k].time, b.keys[k].time);
            exactValue(prefix + "/translation", a.keys[k].translation, b.keys[k].translation);
            exactValue(prefix + "/rotation", a.keys[k].rotation, b.keys[k].rotation);
            exactValue(prefix + "/scale", a.keys[k].scale, b.keys[k].scale);
        }
    }
    const auto& cameraA = parent.initialCamera;
    const auto& cameraB = candidate.initialCamera;
    exactValue("initialCamera/position", cameraA.position, cameraB.position);
    exactValue("initialCamera/yaw", cameraA.yaw, cameraB.yaw);
    exactValue("initialCamera/pitch", cameraA.pitch, cameraB.pitch);
    exactValue("initialCamera/fovY", cameraA.fovY, cameraB.fovY);
    exactValue("initialCamera/nearZ", cameraA.nearZ, cameraB.nearZ);
    exactValue("initialCamera/farZ", cameraA.farZ, cameraB.farZ);
    REQUIRE(parent.animation.cameraTrack.size() == candidate.animation.cameraTrack.size());
    for (size_t k = 0; k < parent.animation.cameraTrack.size(); ++k) {
        const auto& a = parent.animation.cameraTrack[k];
        const auto& b = candidate.animation.cameraTrack[k];
        const auto field = std::format("cameraTrack/key[{}]", k);
        exactValue(field + "/time", a.time, b.time);
        exactValue(field + "/position", a.position, b.position);
        exactValue(field + "/yaw", a.yaw, b.yaw);
        exactValue(field + "/pitch", a.pitch, b.pitch);
    }
    REQUIRE(parent.animation.lightTracks.size() == candidate.animation.lightTracks.size());
    for (size_t k = 0; k < parent.animation.lightTracks.size(); ++k) {
        const auto& a = parent.animation.lightTracks[k];
        const auto& b = candidate.animation.lightTracks[k];
        const auto field = std::format("lightTrack[{}]", k);
        exactValue(field + "/light", a.light, b.light);
        exactValue(field + "/centre", a.centre, b.centre);
        exactValue(field + "/axis", a.axis, b.axis);
        exactValue(field + "/radius", a.radius, b.radius);
        exactValue(field + "/phase", a.phase, b.phase);
        exactValue(field + "/period", a.period, b.period);
    }
    exactValue("animation/duration", parent.animation.duration, candidate.animation.duration);
    exactValue("animation/loop", parent.animation.loop, candidate.animation.loop);
    REQUIRE(parent.animation.emissiveTracks.size() == candidate.animation.emissiveTracks.size());
    for (size_t t = 0; t < parent.animation.emissiveTracks.size(); ++t) {
        REQUIRE(kept[candidate.animation.emissiveTracks[t].objectIndex] ==
                parent.animation.emissiveTracks[t].objectIndex);
        for (uint32_t k = 0; k <= 1440; ++k) {
            const double time = double(k) / 60;
            exactValue(std::format("emissive[{}]/key[{}]", t, k),
                       asset::sampleEmissiveTrack(parent.animation.emissiveTracks[t], time),
                       asset::sampleEmissiveTrack(candidate.animation.emissiveTracks[t], time));
        }
    }
}
//======================================================================================================================
void compareScenes(rojoRHI::Device& device, engine::LoadedScene& parent,
                   engine::LoadedScene& candidate, const fs::path& root,
                   std::span<const uint32_t> skip) {
    auto& a = *parent.scene;
    auto& b = *candidate.scene;
    prepare(device, a);
    prepare(device, b);
    snapshot(root / "parent", a);
    snapshot(root / "candidate", b);
    const auto ta = a.tables(), tb = b.tables();
    for (const auto& field : {"vertices.bin", "indices.bin", "materials.bin"}) {
        exactBytes(field, *readWholeFile(root / "parent" / field),
                   *readWholeFile(root / "candidate" / field));
        std::cout << "EXACT " << field << "\n";
    }
    exactValue("meshCount", ta.meshCount, tb.meshCount);
    const auto meshRowsA = readRows<engine::MeshRow>(*ta.meshes, ta.meshCount);
    const auto meshRowsB = readRows<engine::MeshRow>(*tb.meshes, tb.meshCount);
    exactBytes("meshRows", std::as_bytes(std::span(meshRowsA)),
               std::as_bytes(std::span(meshRowsB)));
    const auto texturesA = textureSlots(a), texturesB = textureSlots(b);
    REQUIRE(texturesA.size() == texturesB.size());
    for (const auto& [slot, id] : texturesA) {
        REQUIRE(texturesB.contains(slot));
        auto* left = a.tryTexture(id);
        auto* right = b.tryTexture(texturesB.at(slot));
        exactValue("texture/format", left->format(), right->format());
        exactValue("texture/width", left->width(), right->width());
        exactValue("texture/height", left->height(), right->height());
        exactValue("texture/mipLevels", left->mipLevels(), right->mipLevels());
        const auto leftPixels = texturePixels(device, *left);
        const auto rightPixels = texturePixels(device, *right);
        const auto filename = "texture-" + std::to_string(slot) + ".rgba8";
        writeBytes(root / "parent" / filename, leftPixels);
        writeBytes(root / "candidate" / filename, rightPixels);
        exactBytes("texture[" + std::to_string(slot) + "]/actualGpuLevel0", leftPixels,
                   rightPixels);
        std::cout << std::format("EXACT texture[{}] {}x{} levels={} actualGpuLevel0Bytes={}\n",
                                 slot, left->width(), left->height(), left->mipLevels(),
                                 leftPixels.size());
    }
    std::vector<uint32_t> kept;
    for (uint32_t i = 0; i < a.objects.size(); ++i)
        if (std::ranges::find(skip, i) == skip.end())
            kept.push_back(i);
    compareObjects(a, b, kept, 0);
    compareMaterialBindings(a, b, kept);
    exactValue("authoredBounds/min", a.authoredBounds.minimum, b.authoredBounds.minimum);
    exactValue("authoredBounds/max", a.authoredBounds.maximum, b.authoredBounds.maximum);
    exactValue("boundingSphere", a.boundingSphere, b.boundingSphere);
    exactValue("shadowCaster", a.shadowCaster.value_or(UINT32_MAX),
               b.shadowCaster.value_or(UINT32_MAX));
    for (uint32_t i = 0; i < 3; ++i) {
        exactValue("directional/direction", a.lights[i].direction, b.lights[i].direction);
        exactValue("directional/strength", a.lights[i].strength, b.lights[i].strength);
        exactValue("directional/enabled", a.lights[i].enabled, b.lights[i].enabled);
    }
    compareTracks(a, b, kept);
    REQUIRE(a.animation.lightTracks.size() == b.animation.lightTracks.size());
    REQUIRE(a.lightLabPopulations.size() == b.lightLabPopulations.size());
    for (size_t i = 0; i < a.lightLabPopulations.size(); ++i) {
        const auto& pa = a.lightLabPopulations[i];
        const auto& pb = b.lightLabPopulations[i];
        REQUIRE(pa.grid.size() == pb.grid.size());
        REQUIRE(pa.pile.size() == pb.pile.size());
        REQUIRE(parent.document.nodes[pa.documentNode].generator->params ==
                candidate.document.nodes[pb.documentNode].generator->params);
        for (size_t k = 0; k < pa.grid.size(); ++k)
            REQUIRE(pa.grid[k].slot == pb.grid[k].slot);
        for (size_t k = 0; k < pa.pile.size(); ++k)
            REQUIRE(pa.pile[k].slot == pb.pile[k].slot);
    }
    for (uint32_t k = 0; k <= 1440; ++k) {
        const double time = double(k) / 60;
        a.animate(time);
        b.animate(time);
        compareObjects(a, b, kept, time);
        REQUIRE(a.localLights().size() == b.localLights().size());
        for (size_t i = 0; i < a.localLights().size(); ++i) {
            const auto& la = *a.light(a.localLights()[i]);
            const auto& lb = *b.light(b.localLights()[i]);
            exactValue("local/position", la.position, lb.position);
            exactValue("local/direction", la.direction, lb.direction);
            exactValue("local/colour", la.colour, lb.colour);
            exactValue("local/intensity", la.intensity, lb.intensity);
            exactValue("local/range", la.range, lb.range);
            exactValue("local/type", la.type, lb.type);
            exactValue("local/innerCone", la.innerCone, lb.innerCone);
            exactValue("local/outerCone", la.outerCone, lb.outerCone);
            exactValue("local/enabled", la.enabled, lb.enabled);
        }
    }
    std::cout
        << "EXACT object order/names/pose/enabled/motion/material slots, bounds, raw rigid keys, "
           "emissive and animated poses at all 1441 steps (0..24s), directional/local lights\n"
        << std::flush;
}
//======================================================================================================================
asset::AssetResult<void> lightsOnlyFixture(engine::Scene& scene, const asset::DocGenerator& g,
                                           const engine::EnvironmentHook& environment) {
    uint32_t count = 256, pile = 0;
    for (const auto& [key, value] : g.params) {
        if (key == "lights")
            count = static_cast<uint32_t>(value);
        if (key == "pile")
            pile = static_cast<uint32_t>(value);
    }
    engine::LightLabPopulation population;
    const uint32_t base = static_cast<uint32_t>(scene.localLights().size());
    for (const auto& light : scenes::lightLabLights(count, pile)) {
        const auto added = scene.addLight(light);
        REQUIRE(added);
        auto& identities = population.grid.size() < count ? population.grid : population.pile;
        identities.push_back(*added);
    }
    scene.lightLabPopulations.push_back(std::move(population));
    for (auto track : scenes::lightLabTracks(count, pile)) {
        track.light += base;
        scene.animation.lightTracks.push_back(track);
    }
    scene.animation.duration =
        std::max(scene.animation.duration, double(scenes::kLightLabOrbitPeriod));
    scene.animation.loop = true;
    scene.animate(0.0);
    scene.resetMotion();
    return environment(scene);
}
//======================================================================================================================
asset::AssetResult<asset::SceneDocument>
groupDirectionalLights(const asset::SceneDocument& source) {
    const auto failure = [](std::string message) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                 "Lights grouping: " + std::move(message)});
    };
    std::vector<uint32_t> roles;
    std::optional<uint32_t> existing;
    for (uint32_t n = 0; n < source.nodes.size(); ++n) {
        const auto& node = source.nodes[n];
        if (node.role) {
            if (!node.light || source.lights[*node.light].type != asset::DocLightType::Directional)
                return failure("role must reference a directional light");
            roles.push_back(n);
        }
        if (node.name == "Lights") {
            if (existing)
                return failure("more than one node is named Lights");
            existing = n;
        }
    }
    if (roles.size() != 3 || source.nodes[roles[0]].role != "key" ||
        source.nodes[roles[1]].role != "fill" || source.nodes[roles[2]].role != "rim")
        return failure("expected Key, Fill and Rim in their original node order");
    auto grouped = source;
    grouped.schemaVersion = asset::kSceneDocumentSchema;
    if (existing) {
        const auto& node = source.nodes[*existing];
        if (node.children != roles || node.translation != glm::vec3(0) ||
            node.rotation != glm::quat(1, 0, 0, 0) || node.scale != glm::vec3(1) || !node.enabled ||
            node.camera || node.light || node.mesh || node.asset || node.generator || node.role ||
            node.castsShadow || !node.overrides.empty() || node.motion != asset::DocMotion::Rigid ||
            std::ranges::find(source.rootNodes, *existing) == source.rootNodes.end())
            return failure("existing Lights node is not the expected enabled identity group");
        for (uint32_t n : roles)
            if (std::ranges::find(source.rootNodes, n) != source.rootNodes.end())
                return failure("grouped light is also a scene root");
        return grouped;
    }
    for (uint32_t n : roles)
        if (std::ranges::find(source.rootNodes, n) == source.rootNodes.end())
            return failure("directional role is not a scene root");
    std::erase_if(grouped.rootNodes,
                  [&](uint32_t n) { return std::ranges::find(roles, n) != roles.end(); });
    grouped.rootNodes.push_back(static_cast<uint32_t>(grouped.nodes.size()));
    grouped.nodes.push_back({.name = "Lights", .children = roles});
    return grouped;
}
//======================================================================================================================
engine::SceneGeneratorLookup lookup(std::map<std::string, engine::SceneGenerator>& registry) {
    return [&registry](std::string_view name) -> const engine::SceneGenerator* {
        auto found = registry.find(std::string(name));
        return found == registry.end() ? nullptr : &found->second;
    };
}
} // namespace

//======================================================================================================================
TEST_CASE("lab capture refuses a document without a generator", "[.ux6-export][ux6-contract]") {
    asset::SceneDocument document;
    engine::Scene scene;
    const auto captured = scenes::captureLabDocument(document, scene, {}, {});
    REQUIRE_FALSE(captured);
    REQUIRE(captured.error().message.find("generator") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("captured lab documents retain exact generated resources and animation",
          "[gpu][.ux6-export][ux6-equivalence]") {
    const char* directory = std::getenv("LMX_UX6_EXPORT_ROOT");
    REQUIRE(directory);
    const fs::path root(directory);
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    for (const auto id : {"material-lab", "temporal-lab", "light-lab"}) {
        INFO(id);
        std::cout << "BEGIN " << id << "\n" << std::flush;
        const fs::path path = root / "source" / (std::string(id) + ".scene.gltf");
        const auto source = asset::readSceneDocument(path);
        INFO((source ? "" : source.error().message));
        REQUIRE(source);
        auto registry = scenes::sceneGenerators(**device, *source, {});
        REQUIRE(registry);
        engine::Scene bare;
        bare.name = id;
        const auto generator = std::ranges::find_if(
            source->nodes, [](const auto& n) { return n.generator.has_value(); });
        REQUIRE(generator != source->nodes.end());
        const engine::EnvironmentHook noEnvironment =
            [](engine::Scene&) -> asset::AssetResult<void> { return {}; };
        REQUIRE(registry->at(id)(bare, *generator->generator, noEnvironment));
        REQUIRE(bare.finalize(**device));
        prepare(**device, bare);
        const fs::path output = root / id;
        fs::create_directories(output);
        snapshot(output / "generated", bare);
        std::vector<uint32_t> skip;
        if (std::string_view(id) == "material-lab") {
            for (uint32_t i = 0; i < bare.objects.size(); ++i)
                if (bare.objects[i].name.starts_with("material-lab axis "))
                    skip.push_back(i);
            REQUIRE(skip.size() == 6);
        }
        const auto textures = std::string_view(id) == "material-lab" ? scenes::materialLabTextures()
                              : std::string_view(id) == "temporal-lab"
                                  ? scenes::temporalLabTextures()
                                  : std::vector<scenes::LabTexture>{};
        const auto captured = scenes::captureLabDocument(*source, bare, textures, skip);
        if (!captured) {
            std::cout << "CAPTURE REFUSED " << captured.error().message << "\n" << std::flush;
            std::ofstream(output / "capture-refusal.txt") << captured.error().message << "\n";
        }
        INFO((captured ? "" : captured.error().message));
        REQUIRE(captured);
        const fs::path candidatePath = output / (std::string(id) + ".scene.gltf");
        const auto saved = asset::saveSceneDocument(*captured, candidatePath);
        INFO((saved ? "" : saved.error().message));
        REQUIRE(saved);
        const auto reread = asset::readSceneDocument(candidatePath);
        INFO((reread ? "" : reread.error().message));
        REQUIRE(reread);
        auto parent = engine::instantiateSceneDocument(**device, *source, path, lookup(*registry));
        INFO((parent ? "" : parent.error().message));
        REQUIRE(parent);
        // This fixture models the separately scheduled field retirement. Production appendLightLab
        // still appends its field, so this comparison does not certify that production route.
        if (std::string_view(id) == "light-lab")
            registry->at(id) = lightsOnlyFixture;
        auto candidate =
            engine::instantiateSceneDocument(**device, *reread, candidatePath, lookup(*registry));
        INFO((candidate ? "" : candidate.error().message));
        REQUIRE(candidate);
        compareScenes(**device, *parent, *candidate, output, skip);
        (*device)->waitIdle();
        std::cout << "PASS " << id << "\n" << std::flush;
    }
}

//======================================================================================================================
TEST_CASE("directional grouping preserves existing document fields and references",
          "[.ux6-export][ux6-lights-group]") {
    const char* evidence = std::getenv("LMX_UX6_EXPORT_ROOT");
    REQUIRE(evidence);
    for (const auto id : {"sponza", "san-miguel", "visibility-lab"}) {
        const auto path = fs::path(evidence) / "source" / (std::string(id) + ".scene.gltf");
        const auto source = asset::readSceneDocument(path);
        REQUIRE(source);
        std::vector<uint32_t> roles;
        for (uint32_t n = 0; n < source->nodes.size(); ++n)
            if (source->nodes[n].role)
                roles.push_back(n);
        REQUIRE(roles.size() == 3);
        const auto grouped = groupDirectionalLights(*source);
        REQUIRE(grouped);
        REQUIRE(grouped->nodes.size() == source->nodes.size() + 1);
        const auto& lights = grouped->nodes.back();
        REQUIRE(lights.name == "Lights");
        REQUIRE(lights.children == roles);
        REQUIRE(lights.translation == glm::vec3(0));
        REQUIRE(lights.rotation == glm::quat(1, 0, 0, 0));
        REQUIRE(lights.scale == glm::vec3(1));
        REQUIRE(lights.enabled);
        auto expectedRoots = source->rootNodes;
        std::erase_if(expectedRoots,
                      [&](uint32_t n) { return std::ranges::find(roles, n) != roles.end(); });
        expectedRoots.push_back(static_cast<uint32_t>(source->nodes.size()));
        REQUIRE(grouped->rootNodes == expectedRoots);
        REQUIRE(grouped->rootNodes.back() == source->nodes.size());
        for (uint32_t role : roles)
            REQUIRE(std::ranges::find(grouped->rootNodes, role) == grouped->rootNodes.end());
        auto reversed = *grouped;
        reversed.nodes.pop_back();
        reversed.rootNodes = source->rootNodes;
        reversed.schemaVersion = source->schemaVersion;
        REQUIRE(asset::sceneDocumentJson(reversed, "compare.scene.bin") ==
                asset::sceneDocumentJson(*source, "compare.scene.bin"));
        REQUIRE(asset::sceneDocumentBuffer(*grouped) == asset::sceneDocumentBuffer(*source));
        const auto again = groupDirectionalLights(*grouped);
        REQUIRE(again);
        REQUIRE(asset::sceneDocumentJson(*again, "compare.scene.bin") ==
                asset::sceneDocumentJson(*grouped, "compare.scene.bin"));

        auto collision = *source;
        collision.nodes.push_back({.name = "Lights"});
        collision.rootNodes.push_back(static_cast<uint32_t>(collision.nodes.size() - 1));
        REQUIRE_FALSE(groupDirectionalLights(collision));
        auto disabled = *grouped;
        disabled.nodes.back().enabled = false;
        REQUIRE_FALSE(groupDirectionalLights(disabled));
    }
}

//======================================================================================================================
TEST_CASE("stage six canonical catalog documents with directional light groups",
          "[.ux6-stage-catalog]") {
    const char* evidence = std::getenv("LMX_UX6_EXPORT_ROOT");
    const char* destination = std::getenv("LMX_UX6_STAGE_ROOT");
    REQUIRE(evidence);
    REQUIRE(destination);
    const fs::path root(evidence), output(destination);
    REQUIRE_FALSE(fs::exists(output));
    fs::create_directories(output);
    for (const auto id :
         {"material-lab", "temporal-lab", "light-lab", "sponza", "san-miguel", "visibility-lab"}) {
        const std::string filename = std::string(id) + ".scene.gltf";
        const bool captured = std::string_view(id) == "material-lab" ||
                              std::string_view(id) == "temporal-lab" ||
                              std::string_view(id) == "light-lab";
        const auto sourcePath = captured ? root / id / filename : root / "source" / filename;
        const auto source = asset::readSceneDocument(sourcePath);
        INFO(id);
        INFO((source ? "" : source.error().message));
        REQUIRE(source);
        const auto grouped = groupDirectionalLights(*source);
        INFO((grouped ? "" : grouped.error().message));
        REQUIRE(grouped);
        const auto path = output / filename;
        const auto saved = asset::saveSceneDocument(*grouped, path);
        INFO((saved ? "" : saved.error().message));
        REQUIRE(saved);
        const auto restored = asset::readSceneDocument(path);
        INFO((restored ? "" : restored.error().message));
        REQUIRE(restored);
        const auto bufferUri = std::string(id) + ".scene.bin";
        const auto bytes = readWholeFile(path);
        REQUIRE(bytes);
        const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        REQUIRE(text == asset::sceneDocumentJson(*restored, bufferUri));
        REQUIRE(asset::sceneDocumentBuffer(*restored) == asset::sceneDocumentBuffer(*source));
        auto expectedRoots = source->rootNodes;
        if (!captured) {
            std::erase_if(expectedRoots,
                          [&](uint32_t n) { return source->nodes[n].role.has_value(); });
            expectedRoots.push_back(static_cast<uint32_t>(source->nodes.size()));
        }
        REQUIRE(restored->rootNodes == expectedRoots);
        auto reversed = *restored;
        if (!captured) {
            REQUIRE(reversed.nodes.size() == source->nodes.size() + 1);
            REQUIRE(reversed.nodes.back().name == "Lights");
            reversed.nodes.pop_back();
            reversed.rootNodes = source->rootNodes;
        }
        reversed.schemaVersion = source->schemaVersion;
        REQUIRE(asset::sceneDocumentJson(reversed, bufferUri) ==
                asset::sceneDocumentJson(*source, bufferUri));
        std::cout << "STAGED " << id
                  << " canonical JSON, unchanged original fields/references, "
                     "exact animation bytes\n"
                  << std::flush;
    }
}
