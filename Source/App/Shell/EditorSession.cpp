//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSession.cpp
/// @brief Polls document changes and queues operator review actions before document work.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Scene/SceneDocumentSave.h"
#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Document/SceneDocumentDiff.h"

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace lmx::app {
namespace {

//======================================================================================================================
FileStamp documentStamp(const engine::LoadedScene& loaded) {
    const auto& path = loaded.path;
    auto buffer = path;
    buffer.replace_extension(".bin");
    std::ifstream gltf(path, std::ios::binary);
    if (gltf) {
        const std::string text(std::istreambuf_iterator<char>{gltf}, {});
        if (const auto uri = documentBufferUri(text))
            buffer = path.parent_path() / *uri;
    } else if (loaded.document.sourceBufferUri) {
        buffer = path.parent_path() / *loaded.document.sourceBufferUri;
    }
    const auto size = [](const std::filesystem::path& file) {
        std::error_code error;
        const auto result = std::filesystem::file_size(file, error);
        return error ? uintmax_t{0} : result;
    };
    const auto time = [](const std::filesystem::path& file) {
        std::error_code error;
        const auto value = std::filesystem::last_write_time(file, error);
        return error ? int64_t{0} : static_cast<int64_t>(value.time_since_epoch().count());
    };
    bool saving = false;
    std::error_code error;
    for (std::filesystem::directory_iterator it(path.parent_path(), error), end;
         !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (it->is_directory(error) && name.starts_with(".lmx-save-") && name.ends_with(".tmp")) {
            saving = true;
            break;
        }
    }
    return FileStamp{size(path), size(buffer), time(path), time(buffer), saving};
}

//======================================================================================================================
std::optional<Sidecar> matchingSidecar(const std::filesystem::path& document,
                                       std::string_view hash) {
    std::ifstream file(sidecarPath(document), std::ios::binary);
    if (!file)
        return std::nullopt;
    std::string text(std::istreambuf_iterator<char>{file}, {});
    auto parsed = parseSidecar(std::move(text));
    if (!parsed || parsed->documentSha256 != hash)
        return std::nullopt;
    return *parsed;
}

//======================================================================================================================
int64_t utcMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

//======================================================================================================================
FileStamp EditorShell::currentDocumentStamp() const {
    return documentStamp(*m_session.loadedScene());
}

//======================================================================================================================
void EditorShell::recordSessionReview(std::string command, std::string arguments,
                                      std::string outcome) {
    m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                      .actor = Actor::Operator,
                                      .command = std::move(command),
                                      .arguments = std::move(arguments),
                                      .outcome = std::move(outcome)});
}

//======================================================================================================================
bool EditorShell::acceptFileProposal(uint64_t id) {
    const auto* proposal = m_sessionProposals.find(id);
    if (!proposal || proposal->source != ProposalSource::File ||
        proposal->state != SessionState::Proposed ||
        m_documentWorkflow.step() != WorkflowStep::Idle)
        return false;
    if (m_playback.state() != PlaybackState::Stopped || m_measurement.active()) {
        m_notices.post(
            {ActionStatus::Unavailable, "Stop playback and measurement before Accept.", {}},
            ImGui::GetTime());
        recordSessionReview("proposal.accept", std::to_string(id), "unavailable");
        return false;
    }
    const auto* loaded = m_session.loadedScene();
    auto hash = asset::sceneDocumentHash(loaded->path);
    if (!hash || *hash != proposal->hash) {
        m_sessionProposals.resolve(id, SessionState::Stale);
        recordSessionReview("proposal.accept", std::to_string(id), "stale file hash");
        return false;
    }
    auto document = asset::readSceneDocument(loaded->path);
    if (!document || !canPreserveSessionSelection(m_selection, *loaded, *document)) {
        m_notices.post(
            {ActionStatus::Unavailable,
             "The selected subject would change; choose another selection before Accept.",
             {}},
            ImGui::GetTime());
        recordSessionReview("proposal.accept", std::to_string(id), "selection unavailable");
        return false;
    }
    m_fileAcceptId = id;
    requestDocumentAction(DocumentAction::Revert);
    if (m_documentWorkflow.step() == WorkflowStep::Idle) {
        m_fileAcceptId.reset();
        return false;
    }
    recordSessionReview("proposal.accept", std::to_string(id), "queued");
    return true;
}

//======================================================================================================================
void EditorShell::pumpSession(double now) {
    const auto* loaded = m_session.loadedScene();
    if (!loaded)
        return;
    if (m_watchedPath != loaded->path) {
        m_watchedPath = loaded->path;
        m_watchedStamp = currentDocumentStamp();
        m_loadedStamp = m_watchedStamp;
        m_documentWatch.reset(m_watchedStamp);
    }

    if (m_sessionPanelAction) {
        const auto action = *m_sessionPanelAction;
        m_sessionPanelAction.reset();
        if (action.action == SessionPanelAction::Accept)
            acceptFileProposal(action.id);
        else if (action.action == SessionPanelAction::Reject) {
            if (const auto* proposal = m_sessionProposals.find(action.id);
                proposal && proposal->source == ProposalSource::File &&
                (proposal->state == SessionState::Proposed ||
                 proposal->state == SessionState::Error)) {
                const auto observedStamp = currentDocumentStamp();
                const auto observedHash = asset::sceneDocumentHash(loaded->path);
                const std::string_view hash =
                    observedHash ? std::string_view(*observedHash) : std::string_view{};
                if (observedStamp != m_watchedStamp || (proposal->hash.empty() && observedHash)) {
                    m_sessionProposals.resolve(action.id, SessionState::Stale);
                    m_documentWatch.reset(FileStamp{});
                    recordSessionReview("proposal.reject", std::to_string(action.id),
                                        "stale file hash");
                } else if (m_sessionProposals.rejectIfCurrent(action.id, hash)) {
                    recordSessionReview("proposal.reject", std::to_string(action.id), "rejected");
                    m_loadedStamp = observedStamp;
                    m_documentWatch.reset(m_loadedStamp);
                } else {
                    m_documentWatch.reset(FileStamp{});
                    recordSessionReview("proposal.reject", std::to_string(action.id),
                                        "stale file hash");
                }
            }
        }
    }

    if (!m_documentWatch.due(now))
        return;
    const auto stamp = currentDocumentStamp();
    if (stamp != m_watchedStamp) {
        const bool pairChanged = stamp.gltfSize != m_watchedStamp.gltfSize ||
                                 stamp.bufferSize != m_watchedStamp.bufferSize ||
                                 stamp.gltfTime != m_watchedStamp.gltfTime ||
                                 stamp.bufferTime != m_watchedStamp.bufferTime;
        if (const auto* pending = m_sessionProposals.pendingFile(); pending && pairChanged)
            m_sessionProposals.resolve(pending->id, SessionState::Stale);
        m_watchedStamp = stamp;
    }
    const auto decision = m_documentWatch.poll(stamp, now);
    if (decision == WatchDecision::Wait)
        return;
    if (decision == WatchDecision::Hash) {
        auto hash = asset::sceneDocumentHash(loaded->path);
        m_watchReadError = hash ? "" : hash.error().message;
        m_watchedHash = hash ? *hash : "";
        if (hash && (*hash == loaded->hash || m_sessionProposals.rejected(*hash))) {
            m_loadedStamp = stamp;
            m_documentWatch.reset(stamp);
            return;
        }
    }
    const auto sidecar = matchingSidecar(loaded->path, m_watchedHash);
    m_documentWatch.hashed(m_watchedHash, sidecar.has_value(), now);
    if (!m_documentWatch.ready())
        return;

    if (const auto* pending = m_sessionProposals.pendingFile();
        pending && !m_watchedHash.empty() && pending->hash == m_watchedHash)
        return;

    if (!m_watchedHash.empty()) {
        const auto beforeRead = asset::sceneDocumentHash(loaded->path);
        if (!beforeRead || *beforeRead != m_watchedHash) {
            m_documentWatch.reset(m_loadedStamp);
            return;
        }
    }

    SessionProposal proposal;
    proposal.source = ProposalSource::File;
    proposal.actor = sidecar ? Actor::Agent : Actor::System;
    proposal.client = sidecar ? sidecar->actor : "Unknown external change";
    proposal.summary = sidecar ? sidecar->summary : "External scene change";
    proposal.hash = m_watchedHash;
    if (sidecar)
        proposal.evidence = sidecar->evidence;
    auto document = asset::readSceneDocument(loaded->path);
    if (!m_watchedHash.empty()) {
        const auto afterRead = asset::sceneDocumentHash(loaded->path);
        if (!afterRead || *afterRead != m_watchedHash) {
            m_documentWatch.reset(m_loadedStamp);
            return;
        }
    }
    if (!document || !m_watchReadError.empty()) {
        proposal.state = SessionState::Error;
        proposal.error = document ? m_watchReadError : document.error().message;
    } else {
        proposal.state = SessionState::Proposed;
        proposal.changes = asset::diffSceneDocuments(loaded->document, *document);
    }
    const auto id = m_sessionProposals.add(std::move(proposal));
    m_sessionLog.record(
        SessionAction{.timestampMilliseconds = utcMilliseconds(),
                      .actor = sidecar ? Actor::Agent : Actor::System,
                      .client = sidecar ? sidecar->actor : "Unknown external change",
                      .command = "proposal.arrive",
                      .arguments = std::to_string(id),
                      .outcome = document && m_watchReadError.empty() ? "proposed" : "error"});
}

} // namespace lmx::app
