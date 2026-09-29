//----------------------------------------------------------------------------------------------------------------------
/// @file EditorDocuments.cpp
/// @brief Connects document workflow, native dialogs and transactional session changes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Scene/DocumentWorkPump.h"
#include "App/Model/Scene/SceneDocumentSave.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "Core/Diagnostics/Log.h"
#include "Scenes/SceneDocumentExport.h"

#include <SDL3/SDL.h>
#include <imgui.h>

namespace lmx::app {
namespace {
constexpr SDL_DialogFileFilter kDocumentFilters[] = {{"Scene documents", "gltf"}};
struct DocumentDialogRequest {
    std::shared_ptr<DocumentDialogMailbox> mailbox;
    std::string location;
};

//======================================================================================================================
void SDLCALL documentPathCallback(void* userdata, const char* const* files, int) {
    // The callback owns this handle; its mailbox survives even fatal shell teardown. SDL may
    // invoke us off-thread, so neither the shell nor its window/scene is borrowed here.
    std::unique_ptr<DocumentDialogRequest> retained(static_cast<DocumentDialogRequest*>(userdata));
    DocumentDialogResult result;
    if (!files) {
        result.error = SDL_GetError();
        if (result.error.empty())
            result.error = "The native file dialog did not return a result.";
    } else if (files[0])
        result.path = std::filesystem::path(files[0]);
    retained->mailbox->post(std::move(result));
}
} // namespace

//======================================================================================================================
void EditorShell::refreshDocumentDirty(bool force) {
    if (!m_session.loadedScene())
        return;
    const auto generation = m_session.editGeneration();
    if (force || m_dirtyScene != m_session.activeScene() || m_dirtyGeneration != generation) {
        const auto exported = scenes::exportSceneDocument(
            *m_session.loadedScene(), m_session.scene(), m_session.documentState());
        m_documentDirty =
            !exported || scenes::documentDirty(m_session.loadedScene()->document, *exported);
        const std::string error = exported ? "" : exported.error().message;
        if (!error.empty() && error != m_documentExportError)
            m_notices.post({ActionStatus::Failed, "Scene cannot be saved: " + error, {}},
                           ImGui::GetTime());
        m_documentExportError = error;
        m_dirtyScene = m_session.activeScene();
        m_dirtyGeneration = generation;
        SDL_SetWindowTitle(
            m_window,
            (m_session.scene().name + (m_documentDirty ? "*" : "") + " — Luminex").c_str());
    }
    m_documentWorkflow.setContext(m_documentDirty, m_playback.state() == PlaybackState::Stopped,
                                  m_measurement.active());
}

//======================================================================================================================
void EditorShell::requestDocumentAction(DocumentAction action,
                                        std::optional<scenes::SceneId> target) {
    refreshDocumentDirty();
    if (const auto reason = DocumentWorkflow::unavailableReason(
            action, m_playback.state() == PlaybackState::Stopped, m_measurement.active())) {
        m_notices.post({ActionStatus::Unavailable, *reason, {}}, ImGui::GetTime());
        return;
    }
    if (m_measurement.active() &&
        (action == DocumentAction::Open || action == DocumentAction::OpenCatalog)) {
        m_notices.post({ActionStatus::Unavailable, "Stop measurement before opening a scene.", {}},
                       ImGui::GetTime());
        return;
    }
    if (m_documentWorkflow.request(action, std::move(target)))
        endMouseLook();
}

//======================================================================================================================
void EditorShell::requestQuit() {
    requestDocumentAction(DocumentAction::Quit);
}

//======================================================================================================================
void EditorShell::setSceneCamera() {
    if (m_playback.state() != PlaybackState::Stopped || m_measurement.active())
        return;
    if (const auto result = m_session.setSceneCamera(); !result)
        m_notices.post({ActionStatus::Failed, result.error().message, {}}, ImGui::GetTime());
    else
        m_notices.post({ActionStatus::Succeeded,
                        "Scene camera set from the current view. Save to keep it.",
                        {}},
                       ImGui::GetTime());
}

//======================================================================================================================
bool EditorShell::saveDocument(const std::filesystem::path& path, bool saveAs) {
    if (const auto reason = DocumentWorkflow::unavailableReason(
            DocumentAction::Save, m_playback.state() == PlaybackState::Stopped,
            m_measurement.active())) {
        m_notices.post({ActionStatus::Unavailable, *reason, {}}, ImGui::GetTime());
        return false;
    }
    const auto result = saveSessionDocument(m_library, m_session, m_activeSceneId, path, saveAs);
    if (!result) {
        m_notices.post(
            {ActionStatus::Failed, "Scene save failed: " + result.error().message, path.string()},
            ImGui::GetTime());
        refreshDocumentDirty(true);
        return false;
    }
    // Rekeying a live scene changes only the selection's document identity, never its subject.
    m_selection.sceneId = m_activeSceneId;
    m_exposureContext.sceneId = m_activeSceneId;
    refreshDocumentDirty(true);
    m_notices.post({ActionStatus::Succeeded, "Scene saved.", path.string()}, ImGui::GetTime());
    return true;
}

//======================================================================================================================
bool EditorShell::selectScene(scenes::SceneId id) {
    const auto result =
        replaceSessionDocument(m_library, m_session, m_activeSceneId, id, [&] { stopPlayback(); });
    if (!result) {
        LMX_LOG_ERROR("scene '{}' failed to load: {}", id.key, result.error().message);
        m_sceneLoading.fail(id, result.error().message);
        m_notices.post(
            {ActionStatus::Failed, "Scene open failed: " + result.error().message, id.key},
            ImGui::GetTime());
        return false;
    }
    m_sceneLoading = {};
    m_selection = initialSelection(id);
    m_sceneFilter.clear();
    m_visibilityDisplay.clear();
    m_lightingDisplay.clear();
    m_lightingFailureLogged = false;
    m_visibilityFailureLogged = false;
    onSceneSelected(m_temporalState, m_settings, id);
    activateExposureLook(m_exposureContext, m_exposureResetPending, id, m_session.look());
    refreshDocumentDirty(true);
    LMX_LOG_INFO("scene opened '{}' ({} objects)", m_session.scene().name,
                 m_session.scene().objects.size());
    return true;
}

//======================================================================================================================
void EditorShell::pumpDocuments() {
    refreshDocumentDirty();
    if (pumpDocumentWork(
            m_documentWorkflow, *m_documentDialog,
            [&](const PendingDocumentWork& work) {
                const bool succeeded = executeDocumentWork(work);
                refreshDocumentDirty();
                return succeeded;
            },
            [&](const std::string& error) {
                m_notices.post({ActionStatus::Failed, "File dialog failed: " + error, {}},
                               ImGui::GetTime());
            }))
        m_actions.requestQuit();
}

//======================================================================================================================
bool EditorShell::executeDocumentWork(const PendingDocumentWork& work) {
    if (work.saveFirst && !saveDocument(m_session.loadedScene()->path, false))
        return false;
    switch (work.action) {
    case DocumentAction::Save:
        return saveDocument(m_session.loadedScene()->path, false);
    case DocumentAction::SaveAs:
        return saveDocument(*work.path, true);
    case DocumentAction::Open:
        return selectScene(scenes::sceneIdFromPath(*work.path));
    case DocumentAction::OpenCatalog:
        return work.target && selectScene(*work.target);
    case DocumentAction::Revert:
        return selectScene(m_activeSceneId);
    case DocumentAction::Quit:
        return true;
    }
    return false;
}

//======================================================================================================================
void EditorShell::startDocumentDialog() {
    if (!m_documentDialog->begin())
        return;
    auto* retained =
        new DocumentDialogRequest{m_documentDialog, m_session.loadedScene()->path.string()};
    if (m_documentWorkflow.action() == DocumentAction::SaveAs)
        SDL_ShowSaveFileDialog(documentPathCallback, retained, m_window, kDocumentFilters, 1,
                               retained->location.c_str());
    else
        SDL_ShowOpenFileDialog(documentPathCallback, retained, m_window, kDocumentFilters, 1,
                               retained->location.c_str(), false);
}

//======================================================================================================================
void EditorShell::buildDocumentWorkflow() {
    if (m_documentWorkflow.step() == WorkflowStep::Confirm &&
        !ImGui::IsPopupOpen("Unsaved scene changes"))
        ImGui::OpenPopup("Unsaved scene changes");
    if (ImGui::BeginPopupModal("Unsaved scene changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(420));
        ImGui::TextUnformatted(m_session.scene().name.c_str());
        ImGui::PopTextWrapPos();
        const auto reason = DocumentWorkflow::unavailableReason(
            DocumentAction::Save, m_playback.state() == PlaybackState::Stopped,
            m_measurement.active());
        const auto pending = m_documentWorkflow.action();
        if (!pending || DocumentWorkflow::offersSave(*pending)) {
            ImGui::BeginDisabled(reason.has_value());
            if (ImGui::Button("Save")) {
                m_documentWorkflow.confirm(ConfirmChoice::Save);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (reason)
                editorTooltip(reason->c_str());
            ImGui::SameLine();
        }
        if (ImGui::Button("Discard")) {
            m_documentWorkflow.confirm(ConfirmChoice::Discard);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_documentWorkflow.confirm(ConfirmChoice::Cancel);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        if (!m_documentExportError.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(420));
            ImGui::TextUnformatted(m_documentExportError.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndPopup();
    }
    if (m_documentWorkflow.step() == WorkflowStep::ChoosePath && !m_documentDialog->pending())
        startDocumentDialog();
}
} // namespace lmx::app
