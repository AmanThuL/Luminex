#include "Core/Diagnostics/Log.h"
#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Image/HdrEnvironment.h"
#include "Engine/Asset/RepositoryAsset.h"
#include "Scenes/SceneLibrary.h"

#include <rojoRHI/Device.h>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <print>
#include <string>

namespace {

namespace fs = std::filesystem;
namespace asset = lmx::asset;
namespace engine = lmx::engine;
namespace scenes = lmx::scenes;

constexpr std::string_view kStudioUri = "Fetched/MaterialLab/studio_small_09_1k.hdr";
constexpr std::array<std::string_view, 3> kRoles{"key", "fill", "rim"};
constexpr std::array<std::string_view, 3> kLightNames{"Key", "Fill", "Rim"};

struct Inventory {
    size_t exact = 0;
    size_t unmatched = 0;
    size_t documents = 0;
    size_t failed = 0;
};

//======================================================================================================================
template <typename T>
std::string number(T value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

//======================================================================================================================
std::string bits(float value) {
    std::array<char, 8> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      std::bit_cast<uint32_t>(value), 16);
    return "0x" + std::string(8 - (result.ptr - buffer.data()), '0') +
           std::string(buffer.data(), result.ptr);
}

//======================================================================================================================
void component(std::ostream& report, std::string_view name, float original, float decoded) {
    report << ' ' << name << '=' << number(original) << '[' << bits(original) << "]->"
           << number(decoded) << '[' << bits(decoded)
           << "] delta=" << number(double(decoded) - double(original));
}

//======================================================================================================================
void quaternion(std::ostream& report, glm::quat value) {
    report << " quaternion_xyzw=" << number(value.x) << ',' << number(value.y) << ','
           << number(value.z) << ',' << number(value.w);
}

//======================================================================================================================
uint32_t floatRank(float value) {
    const auto raw = std::bit_cast<uint32_t>(value);
    return raw & 0x80000000u ? ~raw : raw | 0x80000000u;
}

//======================================================================================================================
uint64_t ulps(float left, float right) {
    const auto a = floatRank(left);
    const auto b = floatRank(right);
    return a > b ? uint64_t(a) - b : uint64_t(b) - a;
}

//======================================================================================================================
void cameraSearchDiagnostic(std::ostream& report, float yaw, float pitch, float previousYaw) {
    const auto seed = asset::rotationForCamera(yaw, pitch);
    std::array<std::array<float, 9>, 4> neighbours;
    for (int component = 0; component < 4; ++component) {
        neighbours[component][0] = seed[component];
        auto lower = seed[component];
        auto upper = seed[component];
        for (int distance = 1; distance <= 4; ++distance) {
            lower = std::nextafter(lower, -std::numeric_limits<float>::infinity());
            upper = std::nextafter(upper, std::numeric_limits<float>::infinity());
            neighbours[component][distance * 2 - 1] = lower;
            neighbours[component][distance * 2] = upper;
        }
    }
    uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
    glm::vec2 best{};
    size_t checked = 0;
    size_t matches = 0;
    for (const auto x : neighbours[0])
        for (const auto y : neighbours[1])
            for (const auto z : neighbours[2])
                for (const auto w : neighbours[3]) {
                    const auto decoded =
                        asset::cameraAnglesForRotation(glm::quat(w, x, y, z), previousYaw);
                    const auto distance = ulps(yaw, decoded.x) + ulps(pitch, decoded.y);
                    ++checked;
                    matches += distance == 0;
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = decoded;
                    }
                }
    report << " diagnosticCandidates=" << checked << " diagnosticExact=" << matches
           << " bestSumUlps=" << bestDistance;
    component(report, "bestYaw", yaw, best.x);
    component(report, "bestPitch", pitch, best.y);
}

//======================================================================================================================
glm::quat cameraRotation(std::ostream& report, Inventory& inventory, std::string_view subject,
                         float yaw, float pitch, float previousYaw) {
    const auto exact = asset::exactRotationForCamera(yaw, pitch, previousYaw);
    const auto rotation = exact.value_or(asset::rotationForCamera(yaw, pitch));
    const auto decoded = asset::cameraAnglesForRotation(rotation, previousYaw);
    inventory.exact += exact.has_value();
    inventory.unmatched += !exact.has_value();
    report << subject << (exact ? " EXACT" : " UNMATCHED") << " previousYaw=" << number(previousYaw)
           << '[' << bits(previousYaw) << ']';
    component(report, "yaw", yaw, decoded.x);
    component(report, "pitch", pitch, decoded.y);
    quaternion(report, rotation);
    if (!exact) {
        report << " fallback=rotationForCamera-authored-yaw-pitch";
        cameraSearchDiagnostic(report, yaw, pitch, previousYaw);
    }
    report << '\n';
    return rotation;
}

//======================================================================================================================
glm::quat directionRotation(std::ostream& report, Inventory& inventory, std::string_view subject,
                            glm::vec3 direction) {
    const auto exact = asset::exactRotationForDirection(direction);
    const auto rotation = exact.value_or(asset::rotationForDirection(glm::normalize(direction)));
    const auto decoded = asset::directionForRotation(rotation);
    inventory.exact += exact.has_value();
    inventory.unmatched += !exact.has_value();
    report << subject << (exact ? " EXACT" : " UNMATCHED");
    component(report, "x", direction.x, decoded.x);
    component(report, "y", direction.y, decoded.y);
    component(report, "z", direction.z, decoded.z);
    quaternion(report, rotation);
    if (!exact) {
        report << " fallback=rotationForDirection-normalized-authored-direction"
               << (std::abs(glm::dot(direction, direction) - 1.0f) > 1e-5f
                       ? " reason=authored-direction-is-not-unit-length"
                       : " reason=no-exact-quaternion-in-bounded-neighbourhood");
    }
    report << '\n';
    return rotation;
}

//======================================================================================================================
asset::AssetResult<asset::DocAsset> reference(std::string_view uri) {
    const auto path = asset::findRepositoryAsset("Assets/" + std::string(uri));
    if (!path)
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::NotFound,
                                                 "Required asset is missing: " + std::string(uri)});
    const auto bytes = lmx::readWholeFile(*path);
    if (!bytes)
        return std::unexpected(
            asset::AssetError{asset::AssetErrorCode::Io, "Cannot read asset: " + path->string()});
    return asset::DocAsset{std::string(uri), lmx::sha256Hex(*bytes)};
}

//======================================================================================================================
std::string_view assetUri(std::string_view id) {
    if (id == "sponza")
        return "Fetched/Sponza/Sponza.gltf";
    if (id == "damaged-helmet")
        return "Fetched/DamagedHelmet/DamagedHelmet.glb";
    if (id == "milk-truck")
        return "Fetched/CesiumMilkTruck/CesiumMilkTruck.glb";
    if (id == "san-miguel")
        return "Fetched/SanMiguel/SanMiguel.gltf";
    return {};
}

//======================================================================================================================
uint32_t appendRoot(asset::SceneDocument& document, asset::DocNode node) {
    const auto index = static_cast<uint32_t>(document.nodes.size());
    document.rootNodes.push_back(index);
    document.nodes.push_back(std::move(node));
    return index;
}

//======================================================================================================================
void appendCamera(asset::SceneDocument& document, const engine::Scene& scene, std::string_view id,
                  std::ostream& report, Inventory& inventory) {
    const auto& camera = scene.initialCamera;
    const std::string name = scene.animation.cameraTrack.empty() ? "Scene Camera" : "Tour Camera";
    document.camera = appendRoot(
        document, {.name = name,
                   .translation = camera.position,
                   .rotation = cameraRotation(report, inventory, std::string(id) + "/camera",
                                              camera.yaw, camera.pitch, 0.0f),
                   .camera = 0});
    document.cameras.push_back(
        {.name = name, .fovY = camera.fovY, .nearZ = camera.nearZ, .farZ = camera.farZ});
    const auto& track = scene.animation.cameraTrack;
    if (track.empty())
        return;
    asset::DocAnimation animation{.name = "Camera Rail",
                                  .sampleRate = asset::kAnimationBakeRate,
                                  .keyCount = static_cast<uint32_t>(track.size())};
    asset::DocChannel positions{.node = document.camera,
                                .path = asset::DocChannelPath::Translation};
    asset::DocChannel rotations{.node = document.camera, .path = asset::DocChannelPath::Rotation};
    float previousYaw = 0.0f;
    for (size_t key = 0; key < track.size(); ++key) {
        const auto& original = track[key];
        const auto rotation =
            cameraRotation(report, inventory, std::string(id) + "/rail/key/" + number(key),
                           original.yaw, original.pitch, previousYaw);
        const auto authoredPrevious = key == 0 ? 0.0f : track[key - 1].yaw;
        if (std::bit_cast<uint32_t>(previousYaw) != std::bit_cast<uint32_t>(authoredPrevious)) {
            report << id << "/rail/key/" << key << " PREVIOUS_DECODE_DRIFT";
            component(report, "previousYaw", authoredPrevious, previousYaw);
            report << " exactUsingAuthoredPrevious="
                   << (asset::exactRotationForCamera(original.yaw, original.pitch, authoredPrevious)
                           ? "yes"
                           : "no")
                   << '\n';
        }
        previousYaw = asset::cameraAnglesForRotation(rotation, previousYaw).x;
        positions.values.emplace_back(original.position, 0.0f);
        rotations.values.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
    }
    animation.channels.push_back(std::move(positions));
    animation.channels.push_back(std::move(rotations));
    document.animations.push_back(std::move(animation));
}

//======================================================================================================================
void appendDirectionalRig(asset::SceneDocument& document, const engine::Scene& scene,
                          const std::array<engine::DirectionalLight, 3>& neutralRig,
                          std::string_view id, std::ostream& report, Inventory& inventory) {
    for (size_t index = 0; index < kRoles.size(); ++index) {
        const auto strength = asset::encodeStrength(
            id == "material-lab" ? neutralRig[index].strength : scene.lights[index].strength);
        const auto lightIndex = static_cast<uint32_t>(document.lights.size());
        document.lights.push_back({.name = std::string(kLightNames[index]),
                                   .colour = strength.colour,
                                   .intensity = strength.intensity});
        appendRoot(document, {.name = std::string(kLightNames[index]),
                              .rotation = directionRotation(report, inventory,
                                                            std::string(id) + "/directional/" +
                                                                std::string(kRoles[index]),
                                                            scene.lights[index].direction),
                              .light = lightIndex,
                              .enabled = id != "material-lab",
                              .role = std::string(kRoles[index]),
                              .castsShadow = index == 0});
    }
}

//======================================================================================================================
void appendLocalRig(asset::SceneDocument& document, const engine::Scene& scene, std::string_view id,
                    std::ostream& report, Inventory& inventory) {
    if (id != "sponza")
        return;
    const auto group = appendRoot(document, {.name = "Local Lights"});
    for (size_t index = 0; index < scene.rigLightIds().size(); ++index) {
        const auto& light = *scene.light(scene.rigLightIds()[index]);
        const auto name = "Local Light " + number(index + 1);
        const bool spot = light.type == engine::LocalLightType::Spot;
        const auto rotation =
            spot ? directionRotation(report, inventory,
                                     std::string(id) + "/local-light/" + number(index),
                                     light.direction)
                 : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        document.nodes[group].children.push_back(static_cast<uint32_t>(document.nodes.size()));
        document.nodes.push_back({.name = name,
                                  .translation = light.position,
                                  .rotation = rotation,
                                  .light = static_cast<uint32_t>(document.lights.size()),
                                  .enabled = light.enabled});
        document.lights.push_back(
            {.name = name,
             .type = spot ? asset::DocLightType::Spot : asset::DocLightType::Point,
             .colour = glm::dvec3(light.colour),
             .intensity = double(light.intensity),
             .range = light.range,
             .innerCone = light.innerCone,
             .outerCone = light.outerCone});
    }
}

//======================================================================================================================
asset::AssetResult<asset::SceneDocument>
documentForScene(const scenes::SceneEntry& entry, const engine::Scene& scene,
                 const std::array<engine::DirectionalLight, 3>& neutralRig,
                 const asset::DocAsset& studio, std::ostream& report, Inventory& inventory) {
    asset::SceneDocument document;
    document.name = entry.displayName;
    document.loop = scene.animation.loop;
    for (size_t key = 0; key < scene.animation.cameraTrack.size(); ++key) {
        if (scene.animation.cameraTrack[key].time != double(key) / asset::kAnimationBakeRate)
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Malformed,
                                                     std::string(entry.stableId) + "/rail/key/" +
                                                         number(key) +
                                                         ": live key time is not exactly key/60"});
    }
    const auto uri = assetUri(entry.stableId);
    asset::DocNode content{.name = std::string(entry.displayName)};
    if (!uri.empty()) {
        auto asset = reference(uri);
        if (!asset)
            return std::unexpected(asset.error());
        content.asset = std::move(*asset);
    } else {
        content.generator = asset::DocGenerator{.name = std::string(entry.stableId)};
        if (entry.stableId == "visibility-lab")
            content.generator->params = {{"instances", 4096.0}, {"occluders", 0.0}};
        else if (entry.stableId == "light-lab")
            content.generator->params = {{"lights", 256.0}, {"pile", 0.0}};
    }
    appendRoot(document, std::move(content));
    if (entry.stableId == "material-lab") {
        document.look.environment.hdri =
            asset::SceneLook::Hdri{.uri = studio.uri, .sha256 = studio.sha256};
        report << "material-lab rig authored strengths source=live-sponza-neutral-rig"
                  " enabled=false inference=HDRI-live-rig-contributes-zero\n";
    }
    appendCamera(document, scene, entry.stableId, report, inventory);
    appendDirectionalRig(document, scene, neutralRig, entry.stableId, report, inventory);
    appendLocalRig(document, scene, entry.stableId, report, inventory);
    return document;
}

//======================================================================================================================
bool stableRewrite(const fs::path& original, const fs::path& directory, std::ostream& report) {
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        report << "FAIL rewrite directory " << error.message() << '\n';
        return false;
    }
    const auto loaded = asset::readSceneDocument(original);
    if (!loaded) {
        report << "FAIL read " << original.filename().string() << ' ' << loaded.error().message
               << '\n';
        return false;
    }
    const auto copy = directory / original.filename();
    const auto saved = asset::saveSceneDocument(*loaded, copy);
    if (!saved) {
        report << "FAIL rewrite " << copy.filename().string() << ' ' << saved.error().message
               << '\n';
        return false;
    }
    auto originalBin = original;
    originalBin.replace_extension(".bin");
    auto copyBin = copy;
    copyBin.replace_extension(".bin");
    const auto json = lmx::readWholeFile(original);
    const auto jsonCopy = lmx::readWholeFile(copy);
    const auto binary = lmx::readWholeFile(originalBin);
    const auto binaryCopy = lmx::readWholeFile(copyBin);
    const bool equal =
        json && jsonCopy && binary && binaryCopy && *json == *jsonCopy && *binary == *binaryCopy;
    report << original.filename().string() << " save-load-save=" << (equal ? "EXACT" : "FAILED")
           << " jsonBytes=" << (json ? json->size() : 0)
           << " bufferBytes=" << (binary ? binary->size() : 0) << '\n';
    return equal;
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "usage: SceneExport <catalog-directory> <report-path>");
        return 2;
    }
    const fs::path output = argv[1];
    const fs::path reportPath = argv[2];
    std::error_code error;
    fs::create_directories(output, error);
    if (!error)
        fs::create_directories(reportPath.parent_path(), error);
    if (error) {
        std::println(stderr, "Cannot create export directories: {}", error.message());
        return 1;
    }
    std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
    if (!report) {
        std::println(stderr, "Cannot write report: {}", reportPath.string());
        return 1;
    }
    report << "Live catalog exporter orientation report\n"
              "EXACT means every decoded float bit equals the live authored value.\n"
              "UNMATCHED is a failed exactness gate; deterministic seed fallback is explicit.\n"
              "Fallbacks belong only to this migration tool; ordinary export/save remains exact.\n"
              "Directional values are passed unnormalized to ComputeDirectionalLight; normalizing"
              " them changes actual shading. No image parity is inferred.\n";
    const auto studio = reference(kStudioUri);
    if (!studio) {
        report << "FAIL required HDRI " << studio.error().message << '\n';
        std::println(stderr, "{}", studio.error().message);
        return 1;
    }
    const auto studioPath = asset::findRepositoryAsset("Assets/" + studio->uri);
    const auto hdr = asset::loadRadianceHdr(studioPath->string());
    if (!hdr) {
        report << "FAIL required HDRI " << hdr.error().message << '\n';
        return 1;
    }
    lmx::log::init();
    auto device = rojoRHI::createDevice({.enableValidation = true});
    if (!device) {
        report << "FAIL device " << device.error().message << '\n';
        return 1;
    }
    scenes::SceneLibrary library(**device);
    const auto sponza = library.get(*scenes::parseSceneId("sponza"));
    if (!sponza) {
        report << "FAIL neutral rig source " << sponza.error().message << '\n';
        return 1;
    }
    const std::array<engine::DirectionalLight, 3> neutralRig{
        (*sponza)->lights[0], (*sponza)->lights[1], (*sponza)->lights[2]};
    Inventory inventory;
    for (const auto& entry : library.entries()) {
        std::println("Exporting {}", entry.stableId);
        const auto scene = library.get(entry.id);
        if (!scene) {
            report << entry.stableId << " FAIL load " << scene.error().message << '\n';
            ++inventory.failed;
            continue;
        }
        const auto exactBefore = inventory.exact;
        const auto unmatchedBefore = inventory.unmatched;
        auto document = documentForScene(entry, **scene, neutralRig, *studio, report, inventory);
        if (!document) {
            report << entry.stableId << " FAIL document " << document.error().message << '\n';
            ++inventory.failed;
            continue;
        }
        const auto path = output / (std::string(entry.stableId) + ".scene.gltf");
        const auto saved = asset::saveSceneDocument(*document, path);
        if (!saved) {
            report << entry.stableId << " FAIL save " << saved.error().message << '\n';
            ++inventory.failed;
            continue;
        }
        if (!stableRewrite(path, reportPath.parent_path() / "exporter-roundtrip", report))
            ++inventory.failed;
        ++inventory.documents;
        report << entry.stableId << " SUMMARY exact=" << inventory.exact - exactBefore
               << " unmatched=" << inventory.unmatched - unmatchedBefore
               << " railKeys=" << (*scene)->animation.cameraTrack.size()
               << " documentLocalLights=" << (*scene)->rigLightIds().size() << '\n';
        report.flush();
    }
    (*device)->waitIdle();
    report << "TOTAL documents=" << inventory.documents << " failed=" << inventory.failed
           << " exact=" << inventory.exact << " unmatched=" << inventory.unmatched << '\n';
    report.flush();
    std::println("Exported {} documents; {} exact / {} unmatched orientations; {} failures.",
                 inventory.documents, inventory.exact, inventory.unmatched, inventory.failed);
    return !report || inventory.failed != 0 || inventory.unmatched != 0 ? 1 : 0;
}
