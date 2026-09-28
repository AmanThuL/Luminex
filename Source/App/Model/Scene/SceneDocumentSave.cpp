//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentSave.cpp
/// @brief Verifies completed document files before adopting metadata and reset defaults.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneDocumentSave.h"

#include "Core/Diagnostics/Assert.h"
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
} // namespace

//======================================================================================================================
asset::AssetResult<void> saveSessionDocument(scenes::SceneLibrary& library, SceneSession& session,
                                             scenes::SceneId& activeId,
                                             const std::filesystem::path& path, bool saveAs,
                                             const SceneDocumentSaveIO& io) {
    auto* loaded = session.loadedScene();
    LMX_ASSERT(loaded && library.loaded(activeId) == loaded,
               "save requires the active library snapshot");
    auto targetBin = path;
    targetBin.replace_extension(".bin");
    const auto sourceBuffer =
        loaded->document.sourceBufferUri
            ? std::optional(loaded->path.parent_path() / *loaded->document.sourceBufferUri)
            : std::nullopt;
    if (saveAs &&
        (samePath(path, loaded->path) || samePath(targetBin, loaded->path) ||
         (sourceBuffer && (samePath(path, *sourceBuffer) || samePath(targetBin, *sourceBuffer)))))
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::Io,
                                                 "Save As requires a different document and "
                                                 "companion path; use Save for the active file."});
    auto exported = scenes::exportSceneDocument(*loaded, session.scene(), session.documentState());
    if (!exported)
        return std::unexpected(exported.error());
    auto written = io.write ? io.write(*exported, path) : asset::saveSceneDocument(*exported, path);
    if (!written)
        return std::unexpected(written.error());
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
    session.adoptDocumentResetBaseline();
    activeId = destination;
    return {};
}

//======================================================================================================================
asset::AssetResult<void> replaceSessionDocument(scenes::SceneLibrary& library,
                                                SceneSession& session, scenes::SceneId& activeId,
                                                const scenes::SceneId& target,
                                                const std::function<void()>& beforeDeactivate) {
    const auto invalidate = [&](const engine::LoadedScene& old) {
        if (session.activeScene() == old.scene.get() && beforeDeactivate)
            beforeDeactivate();
        session.invalidate(*old.scene);
    };
    auto replacement = library.reload(target, invalidate);
    if (!replacement)
        return std::unexpected(replacement.error());
    if (session.activeScene())
        library.forget(activeId, invalidate);
    session.activate(**replacement, SceneActivationMotion::Reset);
    activeId = target;
    return {};
}
} // namespace lmx::app
