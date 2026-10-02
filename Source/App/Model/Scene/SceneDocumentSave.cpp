//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentSave.cpp
/// @brief Verifies completed document files before adopting metadata and reset defaults.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneDocumentSave.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Scenes/SceneDocumentExport.h"

#include <limits>

namespace lmx::app {
namespace {

//======================================================================================================================
bool samePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code error;
    if (std::filesystem::equivalent(first, second, error) && !error)
        return true;
    error.clear();
    const auto a = std::filesystem::weakly_canonical(first, error);
    if (error)
        return first.lexically_normal() == second.lexically_normal();
    const auto b = std::filesystem::weakly_canonical(second, error);
    return !error && a == b;
}

//======================================================================================================================
bool insideDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error)
        return false;
    const auto root = std::filesystem::weakly_canonical(directory, error);
    if (error)
        return false;
    auto value = canonical.begin();
    for (auto part = root.begin(); part != root.end(); ++part, ++value)
        if (value == canonical.end() || *value != *part)
            return false;
    return true;
}

//======================================================================================================================
std::optional<EditorSelection> selectionInReplacement(const EditorSelection& selected,
                                                      const engine::LoadedScene& before,
                                                      const engine::LoadedScene& after) {
    if (!canPreserveSessionSelection(selected, before, after.document))
        return std::nullopt;
    auto result = selected;
    if (selected.subject == EditorSubject::Object) {
        if (selected.index >= after.binding.objectNode.size() ||
            after.binding.objectNode[selected.index] != selected.node ||
            after.binding.objectImportedNode[selected.index] != selected.importedNode ||
            selected.index >= after.scene->objects.size() ||
            before.scene->objects[selected.index].name != after.scene->objects[selected.index].name)
            return std::nullopt;
    }
    if (selected.subject == EditorSubject::Group &&
        selected.importedNode != engine::kGeneratedNode) {
        if (selected.importedNode >= after.binding.importedNodes.size())
            return std::nullopt;
        const auto& old = before.binding.importedNodes[selected.importedNode];
        const auto& next = after.binding.importedNodes[selected.importedNode];
        if (old.assetRoot != next.assetRoot || old.sourceNode != next.sourceNode ||
            old.name != next.name)
            return std::nullopt;
    }
    if (selected.subject == EditorSubject::LocalLight) {
        if (selected.node >= after.binding.nodes.size() ||
            !after.binding.nodes[selected.node].light)
            return std::nullopt;
        result.lightId = *after.binding.nodes[selected.node].light;
    }
    if (selected.subject == EditorSubject::DirectionalLight) {
        if (selected.node >= after.binding.nodes.size() ||
            !after.binding.nodes[selected.node].directional)
            return std::nullopt;
        result.index = *after.binding.nodes[selected.node].directional;
    }
    if (resolveSelection(result, selected.sceneId, *after.scene).subject != selected.subject)
        return std::nullopt;
    return result;
}
} // namespace

//======================================================================================================================
asset::AssetResult<void> saveSessionDocument(scenes::SceneLibrary& library, SceneSession& session,
                                             scenes::SceneId& activeId,
                                             const std::filesystem::path& path, bool saveAs,
                                             const SceneDocumentSaveIO& io,
                                             SceneDocumentWrite* completedWrite) {
    if (completedWrite)
        *completedWrite = {};
    auto* loaded = session.loadedScene();
    LMX_ASSERT(loaded && library.loaded(activeId) == loaded,
               "save requires the active library snapshot");
    auto targetBin = path;
    targetBin.replace_extension(".bin");
    const auto sourceBuffer =
        loaded->document.sourceBufferUri
            ? std::optional(loaded->path.parent_path() / *loaded->document.sourceBufferUri)
            : std::nullopt;
    std::vector<std::filesystem::path> sources{loaded->path};
    std::vector<std::filesystem::path> targets{path, targetBin};
    if (sourceBuffer)
        sources.push_back(*sourceBuffer);
    if (loaded->document.content) {
        const auto sourceStem = loaded->path.parent_path() / loaded->path.stem();
        const auto targetStem = path.parent_path() / path.stem();
        sources.emplace_back(sourceStem.string() + ".geometry.bin");
        sources.emplace_back(sourceStem.string() + ".textures");
        targets.emplace_back(targetStem.string() + ".geometry.bin");
        targets.emplace_back(targetStem.string() + ".textures");
        for (const auto& image : loaded->document.content->images) {
            sources.emplace_back(sourceStem.string() + ".textures/" + image.name + ".png");
            targets.emplace_back(targetStem.string() + ".textures/" + image.name + ".png");
        }
    }
    if (saveAs)
        for (const auto& target : targets)
            for (const auto& source : sources)
                if (samePath(target, source) ||
                    (loaded->document.content &&
                     insideDirectory(target, loaded->path.parent_path() /
                                                 (loaded->path.stem().string() + ".textures"))))
                    return std::unexpected(
                        asset::AssetError{asset::AssetErrorCode::Io,
                                          "Save As requires a different document and "
                                          "companion path; use Save for the active file."});
    scenes::ExportReport report;
    auto exported =
        scenes::exportSceneDocument(*loaded, session.scene(), session.documentState(), &report);
    if (!exported)
        return std::unexpected(exported.error());
    for (const auto& approximation : report.approximations)
        LMX_LOG_INFO("save: the {} at /nodes/{}/rotation has no exact glTF quaternion; saved the "
                     "nearest one",
                     approximation.what, approximation.node);
    auto written = io.write ? io.write(*exported, path) : asset::saveSceneDocument(*exported, path);
    if (!written)
        return std::unexpected(written.error());
    const auto json = asset::sceneDocumentJson(*exported, targetBin.filename().string());
    const auto buffer = asset::sceneDocumentBuffer(*exported);
    std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(json.data()),
                                 reinterpret_cast<const std::byte*>(json.data() + json.size()));
    bytes.insert(bytes.end(), buffer.begin(), buffer.end());
    const SceneDocumentWrite receipt{.path = path, .hash = sha256Hex(bytes)};
    if (completedWrite)
        *completedWrite = receipt;
    auto canonical = io.read ? io.read(path) : asset::readSceneDocument(path);
    if (!canonical)
        return std::unexpected(canonical.error());
    if (scenes::documentDirty(*exported, *canonical))
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io,
                                                 "The completed save differs from the exported "
                                                 "scene; the live document was not adopted."});
    auto hash = io.hash ? io.hash(path) : asset::sceneDocumentHash(path);
    if (!hash)
        return std::unexpected(hash.error());
    if (*hash != receipt.hash)
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Io,
            "The saved document changed during verification; the live document was not adopted."});
    const auto destination = saveAs ? scenes::sceneIdFromPath(path) : activeId;
    const auto& cameraNode = canonical->nodes.at(canonical->camera);
    const auto& lens = canonical->cameras.at(*cameraNode.camera);
    const auto angles = asset::cameraAnglesForRotation(cameraNode.rotation, 0);
    const engine::SceneCamera camera{cameraNode.translation,
                                     angles.x,
                                     angles.y,
                                     lens.fovY,
                                     lens.nearZ,
                                     lens.farZ.value_or(std::numeric_limits<float>::infinity())};
    auto& saved =
        library.adoptSaved(*loaded, destination, std::move(*canonical), path, std::move(*hash),
                           [&](const engine::LoadedScene& old) { session.invalidate(*old.scene); });
    LMX_ASSERT(&saved == loaded, "save adoption must retain the live snapshot address");
    saved.scene->initialCamera = camera;
    session.adoptDocumentCamera(camera);
    // Approximated orientations become the live values the file decodes to, so the saved document
    // is clean; exact ones already match.
    for (const auto& approximation : report.approximations) {
        const auto& binding = saved.binding.nodes.at(approximation.node);
        const auto direction =
            asset::directionForRotation(saved.document.nodes.at(approximation.node).rotation);
        if (binding.directional) {
            session.scene().lights[*binding.directional].direction = direction;
        } else if (binding.light) {
            const auto* live = session.scene().light(*binding.light);
            LMX_ASSERT(live, "an approximated bound light must retain its live identity");
            auto light = *live;
            light.direction = direction;
            const auto updated = session.scene().updateLight(*binding.light, light);
            LMX_ASSERT(updated.has_value(), "a decoded spot direction must remain valid");
        }
    }
    session.adoptDocumentResetBaseline();
    activeId = destination;
    return {};
}

//======================================================================================================================
bool adoptEquivalentDocument(engine::LoadedScene& loaded, const asset::SceneDocument& onDisk,
                             std::string hash) {
    if (scenes::documentDirty(loaded.document, onDisk))
        return false;
    loaded.document.sourceBufferUri = onDisk.sourceBufferUri;
    loaded.hash = std::move(hash);
    return true;
}

//======================================================================================================================
asset::AssetResult<void> replaceSessionDocument(
    scenes::SceneLibrary& library, SceneSession& session, scenes::SceneId& activeId,
    const scenes::SceneId& target, const std::function<void()>& beforeDeactivate,
    const std::function<asset::AssetResult<void>(const engine::LoadedScene&)>& validate) {
    const auto invalidate = [&](const engine::LoadedScene& old) {
        if (session.activeScene() == old.scene.get() && beforeDeactivate)
            beforeDeactivate();
        session.invalidate(*old.scene);
    };
    auto replacement = library.reload(target, invalidate, validate);
    if (!replacement)
        return std::unexpected(replacement.error());
    if (session.activeScene())
        library.forget(activeId, invalidate);
    session.activate(**replacement, SceneActivationMotion::Reset);
    activeId = target;
    return {};
}

//======================================================================================================================
bool canPreserveSessionSelection(const EditorSelection& selection,
                                 const engine::LoadedScene& loaded,
                                 const asset::SceneDocument& proposed) {
    if (selection.subject == EditorSubject::None || selection.subject == EditorSubject::Camera ||
        selection.subject == EditorSubject::Environment)
        return true;
    const auto node = selection.node;
    if (node >= loaded.document.nodes.size() || node >= proposed.nodes.size())
        return false;
    const auto& before = loaded.document.nodes[node];
    const auto& after = proposed.nodes[node];
    if (before.name != after.name || before.asset.has_value() != after.asset.has_value() ||
        before.generator.has_value() != after.generator.has_value())
        return false;
    if (before.asset &&
        (before.asset->uri != after.asset->uri || before.asset->sha256 != after.asset->sha256))
        return false;
    if (before.generator && before.generator->name != after.generator->name)
        return false;
    switch (selection.subject) {
    case EditorSubject::Group:
        if (selection.importedNode == engine::kGeneratedNode)
            return true;
        if (selection.importedNode >= loaded.binding.importedNodes.size())
            return false;
        return after.asset.has_value();
    case EditorSubject::Object:
        return selection.index < loaded.binding.objectNode.size() &&
               loaded.binding.objectNode[selection.index] == node &&
               selection.index < loaded.scene->objects.size() &&
               (after.asset.has_value() || (before.mesh && after.mesh == before.mesh &&
                                            selection.importedNode == engine::kGeneratedNode));
    case EditorSubject::DirectionalLight:
        return before.light && after.light && *after.light < proposed.lights.size() &&
               proposed.lights[*after.light].type == asset::DocLightType::Directional;
    case EditorSubject::LocalLight:
        return before.light && after.light && *after.light < proposed.lights.size() &&
               proposed.lights[*after.light].type != asset::DocLightType::Directional;
    case EditorSubject::None:
    case EditorSubject::Camera:
    case EditorSubject::Environment:
        return true;
    }
    return false;
}

//======================================================================================================================
asset::AssetResult<void> replaceSessionDocumentPreservingView(
    scenes::SceneLibrary& library, SceneSession& session, scenes::SceneId& activeId,
    EditorSelection& selection, const std::optional<asset::SceneDocument>& proposal,
    std::string_view expectedHash,
    const std::function<asset::AssetResult<std::string>(const std::filesystem::path&)>&
        verifyHash) {
    auto* loaded = session.loadedScene();
    LMX_ASSERT(loaded, "session reload requires a loaded document");
    if (proposal && !canPreserveSessionSelection(selection, *loaded, *proposal))
        return std::unexpected(asset::AssetError{
            asset::AssetErrorCode::Io, "The selected subject would be removed or changed; "
                                       "change selection before accepting."});
    const auto camera = session.camera();
    std::optional<EditorSelection> replacementSelection;
    const auto validate = [&](const engine::LoadedScene& replacement) -> asset::AssetResult<void> {
        if (!expectedHash.empty() && replacement.hash != expectedHash)
            return std::unexpected(asset::AssetError{
                asset::AssetErrorCode::Io, "The file changed during reload; review it again."});
        if (!expectedHash.empty()) {
            auto current = verifyHash ? verifyHash(replacement.path)
                                      : asset::sceneDocumentHash(replacement.path);
            if (!current || *current != expectedHash)
                return std::unexpected(asset::AssetError{
                    asset::AssetErrorCode::Io, "The file changed during reload; review it again."});
        }
        replacementSelection = selectionInReplacement(selection, *loaded, replacement);
        if (!replacementSelection)
            return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io,
                                                     "The selected subject would change; change "
                                                     "selection before accepting."});
        return {};
    };
    auto result = replaceSessionDocument(library, session, activeId, activeId, {}, validate);
    if (!result)
        return result;
    session.camera() = camera;
    selection = *replacementSelection;
    return {};
}
} // namespace lmx::app
