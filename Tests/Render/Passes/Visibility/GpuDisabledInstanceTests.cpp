#include "Core/Util/Sha256.h"
#include "Engine/Asset/Image/BmpImage.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"
#include <algorithm>
#include <fstream>

namespace {
using namespace lmx;
struct ToggleFrame {
    std::string digest;
    render::VisibilityStatus visibility;
};

//======================================================================================================================
ToggleFrame toggleFrame(rojoRHI::Device& device, engine::Scene& scene, render::Renderer& renderer,
                        const engine::Camera& camera, render::ClassifyMode classifier,
                        render::SubmissionMode submission, bool occlusion, std::string_view label) {
    auto& commands = device.beginFrame();
    REQUIRE(scene.prepareFrame(device.frameNumber()));
    std::vector<engine::DrawItem> items;
    auto view = render::buildSceneView(scene, items, false);
    view.temporal.enabled = false;
    view.temporal.sceneGeneration = 13;
    view.classifyMode = classifier;
    view.classifyCheck = classifier == render::ClassifyMode::Gpu;
    view.submission = submission;
    view.occlusionEnabled = occlusion;
    view.occlusionCheck = occlusion;
    renderer.render(commands, camera, view, false);
    device.endFrame(nullptr);
    scene.commitFrame();
    device.waitIdle();
    ToggleFrame result;
    result.visibility = renderer.visibilityStatus();
    if (classifier == render::ClassifyMode::Gpu) {
        renderer.drainVisibilityAfterIdle();
        auto retired = renderer.takeRetiredVisibility();
        REQUIRE(retired.size() == 1);
        result.visibility = std::move(retired.front());
        CAPTURE(label, result.visibility.stateMismatches, result.visibility.rowMismatches,
                result.visibility.argumentMismatches, result.visibility.counterMismatches);
        REQUIRE(result.visibility.checkPassed());
    }
    if (occlusion) {
        const auto& check = result.visibility.occlusionCheck;
        CAPTURE(label, check.falselyRejectedInstances, check.falselyRejectedPixels,
                check.invalidReferencePixels, check.unmatchedCandidates);
        REQUIRE(check.enabled);
        REQUIRE(check.passed());
        REQUIRE(check.falselyRejectedInstances == 0);
    }
    auto& target = renderer.colorTarget();
    std::vector<uint8_t> bytes(size_t{target.width()} * target.height() * 4);
    target.readback(bytes.data(), bytes.size());
    result.digest = sha256Hex(std::as_bytes(std::span(bytes)));
    if (const char* directory = std::getenv("LMX_DISABLED_EVIDENCE_DIR")) {
        const auto path = std::filesystem::path(directory);
        std::filesystem::create_directories(path);
        REQUIRE(asset::writeBmp(path / (std::string(label) + ".bmp"), bytes, target.width(),
                                target.height()));
        std::ofstream detail(path / "row-details.tsv", std::ios::app);
        const auto submissionRows = render::buildDrawSubmission(
            view, result.visibility.scene, result.visibility.shadow, submission);
        std::vector<engine::InstanceRow> uploaded(view.tables.instanceCapacity);
        view.tables.instances->readback(uploaded.data(), uploaded.size() * sizeof(uploaded[0]));
        const auto vp = camera.projectionMatrix(16.0f / 9.0f) * camera.viewMatrix();
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            if (scene.objects[i].materialQualifier != "column_a")
                continue;
            const auto slot = items[i].instanceRow;
            const auto& row = view.tables.instanceRows[slot];
            const auto& state = result.visibility.scene.candidates[i];
            detail << label << " object=" << i << " row=" << slot
                   << " enabled=" << scene.objects[i].enabled << " cpuFlags=" << row.flags
                   << " gpuFlags=" << uploaded[slot].flags
                   << " state=" << static_cast<int>(state.state)
                   << " reason=" << static_cast<int>(state.reason)
                   << " submittedRows=" << std::ranges::count(submissionRows.rows, slot)
                   << " sceneCommands=" << result.visibility.sceneCounters.emittedCommands
                   << " shadowCommands=" << result.visibility.shadowCounters.emittedCommands
                   << " bounds=" << row.worldBoundsMin.x << ',' << row.worldBoundsMin.y << ','
                   << row.worldBoundsMin.z << ':' << row.worldBoundsMax.x << ','
                   << row.worldBoundsMax.y << ',' << row.worldBoundsMax.z << " screenCorners=";
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec3 point{(corner & 1) ? row.worldBoundsMax.x : row.worldBoundsMin.x,
                                      (corner & 2) ? row.worldBoundsMax.y : row.worldBoundsMin.y,
                                      (corner & 4) ? row.worldBoundsMax.z : row.worldBoundsMin.z};
                const auto clip = vp * glm::vec4(point, 1);
                detail << (clip.x / clip.w * 0.5f + 0.5f) * target.width() << ','
                       << (0.5f - clip.y / clip.w * 0.5f) * target.height() << ',' << clip.w << ';';
            }
            detail << '\n';
        }
        std::ofstream log(path / "frames.tsv", std::ios::app);
        log << label << '\t' << result.visibility.frameNumber << '\t' << result.digest << '\t'
            << result.visibility.sceneCounters.candidates << '\t'
            << result.visibility.sceneCounters.disabled << '\t'
            << result.visibility.occlusionCheck.falselyRejectedInstances << '\n';
    }
    return result;
}
} // namespace

//======================================================================================================================
TEST_CASE("Sponza column toggles restore exact temporal-off pixels and invalidate HZB coverage",
          "[gpu][visibility][disabled-instance][sponza-toggle]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto loaded = test::loadCatalogScene(**device, "sponza");
    INFO(errorOf(loaded));
    REQUIRE(loaded);
    auto& scene = **loaded;
    const auto pillar =
        std::ranges::find(scene.objects, "column_a", &engine::SceneObject::materialQualifier);
    REQUIRE(pillar != scene.objects.end());
    const size_t index = static_cast<size_t>(pillar - scene.objects.begin());
    const auto identity = pillar->id;
    const auto population = scene.objects.size();
    auto camera = engine::cameraFromScene(scene.initialCamera);
    camera.position = {1.0f, 1.2f, 0.0f};
    camera.yaw = 0;
    camera.pitch = 0;
    for (auto classifier : {render::ClassifyMode::Cpu, render::ClassifyMode::Gpu}) {
        for (auto mode : {render::SubmissionMode::Direct, render::SubmissionMode::Indirect,
                          render::SubmissionMode::Batched}) {
            if (classifier == render::ClassifyMode::Gpu && mode == render::SubmissionMode::Direct)
                continue;
            const auto label = std::to_string(static_cast<int>(classifier)) + "-" +
                               std::to_string(static_cast<int>(mode));
            auto renderer = render::Renderer::create(**device, 320, 180, true);
            REQUIRE(renderer);
            const auto before = toggleFrame(**device, scene, **renderer, camera, classifier, mode,
                                            false, label + "-on");
            scene.setObjectEnabled(index, false);
            const auto off = toggleFrame(**device, scene, **renderer, camera, classifier, mode,
                                         false, label + "-off");
            REQUIRE(off.digest != before.digest);
            REQUIRE(off.visibility.sceneCounters.disabled == 1);
            REQUIRE(off.visibility.shadowCounters.disabled == 1);
            REQUIRE(off.visibility.sceneCounters.candidates == population - 1);
            REQUIRE(off.visibility.shadowCounters.emittedRows == population - 1);
            REQUIRE(off.visibility.scene.candidates[index].reason ==
                    render::VisibilityReason::AuthoredOff);
            scene.setObjectEnabled(index, true);
            const auto restored = toggleFrame(**device, scene, **renderer, camera, classifier, mode,
                                              false, label + "-restored");
            CAPTURE(label, before.digest, off.digest, restored.digest);
            REQUIRE(restored.digest == before.digest);
            REQUIRE(scene.objects.size() == population);
            REQUIRE(scene.objects[index].id == identity);
        }
    }
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        auto renderer = render::Renderer::create(**device, 320, 180, true);
        REQUIRE(renderer);
        const auto label = "hzb-" + std::to_string(static_cast<int>(mode));
        for (uint32_t frame = 0; frame < 8; ++frame) {
            const bool enabled = frame < 2 || frame >= 4;
            scene.setObjectEnabled(index, enabled);
            const auto result =
                toggleFrame(**device, scene, **renderer, camera, render::ClassifyMode::Gpu, mode,
                            true, label + "-" + std::to_string(frame));
            REQUIRE(result.visibility.sceneCounters.disabled == (enabled ? 0 : 1));
            if (frame == 2 || frame == 4)
                REQUIRE(result.visibility.occlusionInvalidReason ==
                        render::OcclusionInvalidReason::CoverageChanged);
            REQUIRE(scene.objects[index].id == identity);
        }
    }
}

//======================================================================================================================
TEST_CASE("document instantiation applies initial object effective flags before the first frame",
          "[gpu][scene-doc][disabled-instance]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    for (std::string_view id : {"sponza", "visibility-lab"}) {
        auto document = scenes::readCatalogDocument(id);
        REQUIRE(document);
        for (auto& node : document->nodes) {
            if (node.asset || node.generator)
                node.enabled = false;
            if (node.generator)
                node.generator->params = {{"instances", 3}, {"occluders", 0}};
        }
        const auto path = std::filesystem::current_path() / "SceneDocuments" /
                          (std::string(id) + "-disabled.scene.gltf");
        std::filesystem::create_directories(path.parent_path());
        REQUIRE(asset::saveSceneDocument(*document, path));
        auto loaded = scenes::loadSceneDocument(**device, path);
        INFO(errorOf(loaded));
        REQUIRE(loaded);
        auto& scene = *loaded->scene;
        REQUIRE_FALSE(scene.objects.empty());
        REQUIRE(loaded->binding.objectEffective.size() == scene.objects.size());
        (*device)->beginFrame();
        REQUIRE(scene.prepareFrame((*device)->frameNumber()));
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            REQUIRE_FALSE(loaded->binding.objectEffective[i]);
            REQUIRE_FALSE(scene.objects[i].enabled);
            REQUIRE((scene.tables().instanceRows[scene.objects[i].id.slot].flags &
                     engine::kInstanceDisabled) != 0);
        }
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
    }
}
