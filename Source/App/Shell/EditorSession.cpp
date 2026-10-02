//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSession.cpp
/// @brief Polls document changes and queues operator review actions before document work.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Rendering/Lighting/LightingHistory.h"
#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Rendering/Settings/RenderSettingCommands.h"
#include "App/Model/Scene/SceneDocumentSave.h"
#include "App/Model/Session/SessionCommands.h"
#include "App/Model/Session/SessionEdits.h"
#include "App/Model/Session/SessionProtocol.h"
#include "App/Model/Session/SessionQueries.h"
#include "Core/Diagnostics/Log.h"
#include "Core/IO/JsonWriter.h"
#include "Core/Util/String.h"
#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Asset/Document/SceneDocumentDiff.h"
#include "Render/Graph/GraphDump.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>

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
        if (const auto candidate = asset::sceneDocumentBufferPath(text, path);
            candidate && *candidate)
            buffer = **candidate;
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

//======================================================================================================================
std::string jsonString(std::string_view value) {
    JsonWriter writer;
    writer.string(value);
    return writer.take();
}

//======================================================================================================================
std::string_view playbackName(PlaybackState state) {
    switch (state) {
    case PlaybackState::Stopped:
        return "stopped";
    case PlaybackState::Playing:
        return "playing";
    case PlaybackState::Paused:
        return "paused";
    }
    return "stopped";
}

} // namespace

//======================================================================================================================
FileStamp EditorShell::currentDocumentStamp() const {
    return documentStamp(*m_session.loadedScene());
}

//======================================================================================================================
void EditorShell::recordSessionReview(std::string command, std::string arguments,
                                      std::string outcome, std::string client) {
    m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                      .actor = Actor::Operator,
                                      .client = std::move(client),
                                      .command = std::move(command),
                                      .arguments = std::move(arguments),
                                      .outcome = std::move(outcome)});
}

//======================================================================================================================
bool EditorShell::acceptFileProposal(uint64_t id) {
    const auto* proposal = m_sessionProposals.find(id);
    if (proposal && proposal->source == ProposalSource::Bridge &&
        proposal->state == SessionState::Proposed) {
        if (m_playback.state() != PlaybackState::Stopped || m_measurement.active()) {
            m_notices.post(
                {ActionStatus::Unavailable, "Stop playback and measurement before Accept.", {}},
                ImGui::GetTime());
            recordSessionReview("proposal.accept", std::to_string(id), "unavailable",
                                proposal->client);
            return false;
        }
        const auto* loaded = m_session.loadedScene();
        if (!loaded || proposal->hash != loaded->hash) {
            m_sessionProposals.resolve(id, SessionState::Stale);
            recordSessionReview("proposal.accept", std::to_string(id), "stale document",
                                proposal->client);
            return false;
        }
        const auto tree =
            buildSceneTreeView(*loaded, m_session.documentState(), "", {}, &m_session);
        const auto changed = changedEditKeys(m_session, tree, proposal->edits);
        if (!changed) {
            m_sessionProposals.resolve(id, SessionState::Stale);
            recordSessionReview("proposal.accept", std::to_string(id), changed.error(),
                                proposal->client);
            return false;
        }
        auto result = applyEdits(m_session, tree, proposal->edits);
        if (!result) {
            m_sessionProposals.resolve(id, SessionState::Stale);
            recordSessionReview("proposal.accept", std::to_string(id), result.error().message,
                                proposal->client);
            return false;
        }
        for (const auto& key : *changed)
            m_sessionAttribution.mark(key, proposal->client);
        reconcileExposureLook(m_session.look(), m_exposureContext, m_exposureResetPending);
        const std::unordered_set<std::string> changedSet(changed->begin(), changed->end());
        std::vector<ProposalEdit> changedEdits;
        for (const auto& edit : proposal->edits)
            if (changedSet.contains(edit.subject + "/" + edit.field))
                changedEdits.push_back(edit);
        if (sessionEditNeedsCameraCut(tree, changedEdits))
            requestCameraCut(m_temporalState);
        m_sessionProposals.resolve(id, SessionState::Applied);
        recordSessionReview("proposal.accept", std::to_string(id), "applied", proposal->client);
        refreshDocumentDirty(true);
        return true;
    }
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
bool EditorShell::startSessionListener() {
    if (m_sessionListener)
        return true;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(defaultSessionSocket(), mailbox);
    if (!listener) {
        LMX_LOG_ERROR("Session Listen failed: {}", listener.error());
        m_sessionPathFeedback = listener.error();
        return false;
    }
    m_sessionMailbox = std::move(mailbox);
    m_sessionListener = std::move(*listener);
    if (m_sessionOutputName.empty())
        m_sessionOutputName =
            sessionDirectoryName(utcMilliseconds() / 1000, static_cast<uint32_t>(::getpid()));
    LMX_LOG_INFO("Session listening at {}", m_sessionListener->path().string());
    return true;
}

//======================================================================================================================
void EditorShell::drainSessionBridge() {
    if (!m_sessionMailbox)
        return;
    for (const auto& inbound : m_sessionMailbox->takeInbound()) {
        if (inbound.opened) {
            m_sessionConnection = inbound.connection;
            m_sessionHello = false;
            m_sessionTier = SessionTier::ReadOnly;
            m_sessionClient.clear();
            continue;
        }
        if (inbound.closed) {
            if (inbound.connection == m_sessionConnection) {
                m_sessionApprovals.cancelPending();
                m_sessionConnection = 0;
                m_sessionHello = false;
                m_sessionTier = SessionTier::ReadOnly;
                m_sessionClient.clear();
            }
            continue;
        }
        if (inbound.connection != m_sessionConnection)
            continue;
        const auto request = decodeRequestEnvelope(inbound.text);
        if (!request) {
            m_sessionMailbox->pushOutbound(inbound.connection,
                                           encodeError(0, SessionError::Protocol, request.error()));
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = "session.protocol",
                                              .outcome = "protocol"});
            continue;
        }
        const auto answerError = [&](SessionError code, std::string_view message) {
            m_sessionMailbox->pushOutbound(inbound.connection,
                                           encodeError(request->id, code, message));
        };
        if (!m_sessionHello) {
            if (request->command != "hello") {
                answerError(SessionError::Protocol, "hello must be the first request");
                m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                                  .actor = Actor::Agent,
                                                  .command = request->command,
                                                  .outcome = "protocol"});
                continue;
            }
            if (const auto valid = validateSessionArguments(SessionCommand::Hello, request->args);
                !valid) {
                answerError(SessionError::Invalid, valid.error());
                m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                                  .actor = Actor::Agent,
                                                  .command = "hello",
                                                  .outcome = "invalid"});
                continue;
            }
            const auto name = request->args.find("name");
            const auto protocol = request->args.find("protocol");
            const auto parsedName =
                name ? name->asString()
                     : std::expected<std::string, std::string>{std::unexpected("Missing name")};
            const auto parsedProtocol =
                protocol
                    ? protocol->asUInt()
                    : std::expected<uint64_t, std::string>{std::unexpected("Missing protocol")};
            if (!parsedName || parsedName->empty() || parsedName->size() > 128 || !parsedProtocol ||
                *parsedProtocol != kSessionProtocol) {
                answerError(SessionError::Protocol, "hello requires a name and protocol 1");
                m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                                  .actor = Actor::Agent,
                                                  .command = "hello",
                                                  .outcome = "protocol"});
                continue;
            }
            m_sessionClient = *parsedName;
            m_sessionRecordClient = m_sessionClient;
            m_sessionHello = true;
            m_sessionMailbox->pushOutbound(inbound.connection,
                                           encodeResult(request->id, "{\"protocol\":1}"));
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = "hello",
                                              .outcome = "connected"});
            continue;
        }
        const auto* spec = findCommand(request->command);
        if (!spec || spec->command == SessionCommand::Hello) {
            answerError(SessionError::Invalid, "Unknown command " + request->command);
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = request->command,
                                              .arguments = std::string(request->args.sourceJson()),
                                              .tier = m_sessionTier,
                                              .outcome = "invalid"});
            continue;
        }
        if (const auto refusal = tierRefusal(*spec, m_sessionTier)) {
            answerError(SessionError::Tier, *refusal);
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = request->command,
                                              .arguments = std::string(request->args.sourceJson()),
                                              .tier = m_sessionTier,
                                              .outcome = "tier"});
            continue;
        }
        if (spec->tier == SessionTier::Apply) {
            const auto parsed = parseApplyRequest(spec->command, request->args);
            if (!parsed) {
                answerError(SessionError::Invalid, parsed.error());
                m_sessionLog.record(
                    SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                  .actor = Actor::Agent,
                                  .client = m_sessionClient,
                                  .command = request->command,
                                  .arguments = std::string(request->args.sourceJson()),
                                  .tier = m_sessionTier,
                                  .outcome = "invalid"});
                continue;
            }
            auto submitted = m_sessionApprovals.submit(request->id, m_sessionClient,
                                                       parsed->summary, std::move(parsed->steps));
            if (!submitted) {
                answerError(SessionError::Busy, submitted.error());
                m_sessionLog.record(
                    SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                  .actor = Actor::Agent,
                                  .client = m_sessionClient,
                                  .command = request->command,
                                  .arguments = std::string(request->args.sourceJson()),
                                  .tier = m_sessionTier,
                                  .outcome = "busy"});
                continue;
            }
            m_sessionApprovalConnections[*submitted] = inbound.connection;
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = request->command,
                                              .arguments = std::string(request->args.sourceJson()),
                                              .tier = m_sessionTier,
                                              .outcome = "awaiting approval"});
            continue;
        }
        if (const auto valid = validateSessionArguments(spec->command, request->args); !valid) {
            answerError(SessionError::Invalid, valid.error());
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = request->command,
                                              .arguments = std::string(request->args.sourceJson()),
                                              .tier = m_sessionTier,
                                              .outcome = "invalid"});
            continue;
        }
        std::string result;
        std::string outcome = "answered";
        bool responseSent = false;
        const auto* loaded = m_session.loadedScene();
        const auto completeTree =
            loaded ? std::optional<SceneTreeView>(
                         buildSceneTreeView(*loaded, m_session.documentState(), "", {}, &m_session))
                   : std::nullopt;
        switch (spec->command) {
        case SessionCommand::ProposeEdits: {
            if (m_sessionProposals.bridgeFull()) {
                answerError(SessionError::Busy, "64 proposals already await review; retry later");
                outcome = "busy";
                responseSent = true;
                break;
            }
            const auto summaryNode = request->args.find("summary");
            const auto summary =
                summaryNode
                    ? summaryNode->asString()
                    : std::expected<std::string, std::string>{std::unexpected("Missing summary")};
            auto edits = parseEdits(request->args);
            std::vector<std::string> evidence;
            std::string refusal;
            if (!summary || summary->empty() || summary->size() > 1024)
                refusal = "summary must be a nonempty string of at most 1024 bytes";
            else if (!edits)
                refusal = edits.error();
            if (const auto names = request->args.find("evidence")) {
                if (!names->isArray())
                    refusal = "evidence must be an array";
                else
                    for (const auto& item : names->elements()) {
                        const auto name = item.asString();
                        if (!name || name->empty() || name->size() > 4096) {
                            refusal = "evidence entries must be nonempty strings";
                            break;
                        }
                        evidence.push_back(*name);
                    }
            }
            if (refusal.empty() && !completeTree)
                refusal = "No loaded scene is available";
            std::expected<std::vector<asset::DocumentChange>, std::string> preview =
                std::unexpected("No edits");
            if (refusal.empty()) {
                preview = previewEdits(m_session, *completeTree, *edits);
                if (!preview)
                    refusal = preview.error();
                else if (preview->empty())
                    refusal = "Edits make no document changes";
            }
            if (!refusal.empty()) {
                answerError(SessionError::Invalid, refusal);
                outcome = "invalid";
                responseSent = true;
                break;
            }
            SessionProposal proposal;
            proposal.source = ProposalSource::Bridge;
            proposal.actor = Actor::Agent;
            proposal.client = m_sessionClient;
            proposal.connection = inbound.connection;
            proposal.summary = *summary;
            proposal.hash = loaded->hash;
            proposal.evidence = std::move(evidence);
            proposal.changes = std::move(*preview);
            proposal.edits = std::move(*edits);
            proposal.state = SessionState::Proposed;
            const auto id = m_sessionProposals.add(std::move(proposal));
            result = std::format("{{\"proposal\":{}}}", id);
            outcome = "proposed";
            break;
        }
        case SessionCommand::ProposeWithdraw: {
            const auto idNode = request->args.find("proposal");
            const auto id =
                idNode ? idNode->asUInt()
                       : std::expected<uint64_t, std::string>{std::unexpected("Missing proposal")};
            const auto* proposal = id ? m_sessionProposals.find(*id) : nullptr;
            if (!proposal || proposal->source != ProposalSource::Bridge ||
                proposal->state != SessionState::Proposed ||
                proposal->connection != inbound.connection) {
                answerError(SessionError::Invalid, "No matching bridge proposal to withdraw");
                outcome = "invalid";
                responseSent = true;
                break;
            }
            m_sessionProposals.resolve(*id, SessionState::Stale);
            result = std::format("{{\"proposal\":{}}}", *id);
            outcome = "withdrawn";
            break;
        }
        case SessionCommand::QueryStatus: {
            result =
                statusJson({.documentPath = loaded ? loaded->path.string() : "",
                            .documentHash = loaded ? loaded->hash : "",
                            .dirty = m_documentDirty,
                            .playback = std::string(playbackName(m_playback.state())),
                            .measuring = m_measurement.active(),
                            .tier = m_sessionTier,
                            .pendingProposals = m_sessionProposals.pending(),
                            .job = m_sessionMeasurementApproval || m_pendingSessionMeasurementStart
                                       ? "measurement"
                                   : m_sessionCaptureApproval ? "gpu capture"
                                   : m_sessionChildApproval   ? "headless capture"
                                                              : "idle"});
            break;
        }
        case SessionCommand::QueryHierarchy:
            if (completeTree)
                result = hierarchyJson(*completeTree);
            break;
        case SessionCommand::QuerySelection:
            if (completeTree)
                result = selectionJson(m_selection, *completeTree);
            break;
        case SessionCommand::QueryCamera:
            if (m_session.activeScene())
                result = cameraJson(m_session.camera());
            break;
        case SessionCommand::QuerySettings:
            result = settingsJson(m_settings);
            break;
        case SessionCommand::QueryReadings:
            result = readingsJson(m_visibilityDisplay.readingsStatus(),
                                  m_lightingDisplay.readingsStatus());
            break;
        case SessionCommand::QueryPerformance:
            result = performanceJson(m_performanceModel.snapshot());
            break;
        case SessionCommand::QueryGraph:
            if (const auto* frame = m_renderGraphPanel.snapshot.displayed())
                result = jsonString(render::dumpCompiledFrame(frame->record));
            break;
        case SessionCommand::QueryConsole: {
            const auto after = request->args.find("afterSequence");
            const auto sequence = after ? after->asUInt() : std::expected<uint64_t, std::string>{0};
            if (!sequence) {
                answerError(SessionError::Invalid, "afterSequence must be uint64");
                outcome = "invalid";
                m_sessionLog.record(
                    SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                  .actor = Actor::Agent,
                                  .client = m_sessionClient,
                                  .command = request->command,
                                  .arguments = std::string(request->args.sourceJson()),
                                  .tier = m_sessionTier,
                                  .outcome = outcome});
                continue;
            }
            result = consoleJson(m_consoleModel.retainedSnapshot(), *sequence);
            break;
        }
        case SessionCommand::QueryProposals:
            result = proposalsJson(m_sessionProposals);
            break;
        case SessionCommand::QueryLog:
            result = logJson(m_sessionLog);
            break;
        default:
            answerError(SessionError::Unavailable, "Command is not available yet");
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = m_sessionClient,
                                              .command = request->command,
                                              .arguments = std::string(request->args.sourceJson()),
                                              .tier = m_sessionTier,
                                              .outcome = "unavailable"});
            continue;
        }
        if (!responseSent && result.empty()) {
            answerError(SessionError::Unavailable, "No scene or published frame is available");
            outcome = "unavailable";
        } else if (!responseSent && result.size() + 96 > kMaxLineBytes) {
            answerError(SessionError::Unavailable, "Query response exceeds 1 MiB");
            outcome = "unavailable";
        } else if (!responseSent) {
            m_sessionMailbox->pushOutbound(inbound.connection, encodeResult(request->id, result));
        }
        m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                          .actor = Actor::Agent,
                                          .client = m_sessionClient,
                                          .command = request->command,
                                          .arguments = std::string(request->args.sourceJson()),
                                          .tier = m_sessionTier,
                                          .outcome = outcome});
    }
    if (m_sessionListener)
        m_sessionListener->wake();
}

//======================================================================================================================
void EditorShell::pumpSession(double now) {
    if (m_sessionPanelAction) {
        const auto action = *m_sessionPanelAction;
        if (action.action == SessionPanelAction::ToggleListen) {
            m_sessionPanelAction.reset();
            if (m_sessionListener) {
                stopSessionWork();
                m_sessionListener.reset();
                m_sessionMailbox.reset();
                m_sessionConnection = 0;
                m_sessionHello = false;
                m_sessionTier = SessionTier::ReadOnly;
                m_sessionClient.clear();
            } else {
                startSessionListener();
            }
        } else if (action.action == SessionPanelAction::SetTier) {
            m_sessionPanelAction.reset();
            if (m_sessionConnection && m_sessionHello) {
                const bool lowered = action.tier < m_sessionTier;
                m_sessionTier = action.tier;
                m_sessionLog.record(
                    sessionTierAction(m_sessionClient, m_sessionTier, utcMilliseconds()));
                // A lowered ceiling withdraws what the higher one let the client queue; a plan
                // the operator already approved keeps running.
                if (lowered) {
                    cancelAwaitingSessionApprovals("cancelled: ceiling lowered");
                    if (m_sessionTier < SessionTier::Propose)
                        m_sessionProposals.markStale(ProposalSource::Bridge);
                }
            }
        }
    }
    if (m_sessionPanelAction && m_sessionPanelAction->action == SessionPanelAction::Export) {
        m_sessionPanelAction.reset();
        const auto output = sessionOutputPath("session.json", true);
        recordSessionReview("export.request", output ? output->string() : "session.json",
                            "requested", m_sessionRecordClient);
        ActionResult result{.status = ActionStatus::Failed, .actor = Actor::Operator};
        if (!output) {
            result.message = "Session Export failed: " + output.error();
        } else {
            m_consoleModel.refresh();
            const auto* loaded = m_session.loadedScene();
            const auto contents = sessionRecordJson(
                m_sessionLog, m_consoleModel.snapshot(), loaded ? loaded->path.string() : "",
                loaded ? loaded->hash : "", m_sessionRecordClient, m_consoleModel.filter);
            result.path = output->string();
            if (writeSessionFile(*output, contents)) {
                result.status = ActionStatus::Succeeded;
                result.message = "Exported session record";
            } else {
                result.message = "Session Export failed: cannot write the session directory";
            }
        }
        recordSessionReview("export.result", result.path,
                            result.status == ActionStatus::Succeeded ? "exported" : result.message,
                            m_sessionRecordClient);
        m_notices.post(std::move(result), now);
    }
    drainSessionBridge();
    if (m_sessionPanelAction && (m_sessionPanelAction->action == SessionPanelAction::Approve ||
                                 m_sessionPanelAction->action == SessionPanelAction::Deny)) {
        const auto action = *m_sessionPanelAction;
        m_sessionPanelAction.reset();
        const auto* approval = m_sessionApprovals.active();
        if (approval && approval->id == action.id && approval->state == SessionState::Awaiting) {
            const auto client = approval->client;
            const auto request = approval->request;
            if (action.action == SessionPanelAction::Approve)
                m_sessionApprovals.approve(action.id);
            else
                m_sessionApprovals.deny(action.id);
            recordSessionReview(
                action.action == SessionPanelAction::Approve ? "approval.approve" : "approval.deny",
                std::to_string(request),
                action.action == SessionPanelAction::Approve ? "approved" : "denied", client);
        }
    }
    runSessionApprovals();
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
                proposal && proposal->source == ProposalSource::Bridge &&
                proposal->state == SessionState::Proposed) {
                m_sessionProposals.reject(action.id);
                recordSessionReview("proposal.reject", std::to_string(action.id), "rejected",
                                    proposal->client);
            }
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

//======================================================================================================================
std::string EditorShell::captureOutputPath(std::string_view ordinaryPath) const {
    return m_sessionCaptureApproval ? m_sessionJobOutput.string() : std::string(ordinaryPath);
}

//======================================================================================================================
bool EditorShell::sessionCaptureTargetAvailable() const {
    if (!m_sessionCaptureApproval)
        return true;
    std::error_code error;
    const auto root = std::filesystem::symlink_status("session", error);
    if (error || !std::filesystem::is_directory(root) || std::filesystem::is_symlink(root))
        return false;
    const auto run = std::filesystem::symlink_status(m_sessionOutputDirectory, error);
    if (error || !std::filesystem::is_directory(run) || std::filesystem::is_symlink(run))
        return false;
    return sessionCapturePathsAvailable(m_sessionJobOutput);
}

//======================================================================================================================
std::expected<std::filesystem::path, std::string>
EditorShell::sessionOutputPath(std::string_view name, bool exporting) {
    if (toLowerAscii(name) == "session.json" && !exporting)
        return std::unexpected("session.json is reserved for Session Export");
    const auto safe = evidenceName(name);
    if (!safe)
        return std::unexpected(safe.error());
    std::error_code error;
    const std::filesystem::path root("session");
    const auto rootStatus = std::filesystem::symlink_status(root, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return std::unexpected("Cannot inspect session output root: " + error.message());
    if (!error && std::filesystem::exists(rootStatus) &&
        (!std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus)))
        return std::unexpected("Session output root is not a real directory");
    error.clear();
    if (!std::filesystem::exists(root) && !std::filesystem::create_directory(root, error))
        return std::unexpected("Cannot create session output root: " + error.message());
    if (m_sessionOutputDirectory.empty()) {
        const auto candidate =
            m_sessionOutputName.empty()
                ? std::filesystem::path(sessionDirectoryName(utcMilliseconds() / 1000,
                                                             static_cast<uint32_t>(::getpid())))
                : m_sessionOutputName;
        error.clear();
        if (!std::filesystem::create_directory(candidate, error))
            return std::unexpected("Cannot create a new session output directory: " +
                                   error.message());
        m_sessionOutputDirectory = candidate;
    }
    const auto runStatus = std::filesystem::symlink_status(m_sessionOutputDirectory, error);
    if (error || !std::filesystem::is_directory(runStatus) ||
        std::filesystem::is_symlink(runStatus))
        return std::unexpected("Session output directory is unavailable");
    const auto target = m_sessionOutputDirectory / *safe;
    error.clear();
    const auto targetStatus = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return std::unexpected("Cannot inspect session output target: " + error.message());
    if (!exporting && ((!error && std::filesystem::exists(targetStatus)) ||
                       (!error && std::filesystem::is_symlink(targetStatus))))
        return std::unexpected("Session output already exists: " + target.string());
    return std::filesystem::absolute(target);
}

//======================================================================================================================
bool EditorShell::writeSessionFile(const std::filesystem::path& path, std::string_view contents) {
    if (m_sessionOutputDirectory.empty() ||
        path.parent_path() != std::filesystem::absolute(m_sessionOutputDirectory) ||
        !evidenceName(path.filename().string()))
        return false;
    const int root = ::open("session", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0)
        return false;
    const int run = ::openat(root, m_sessionOutputDirectory.filename().c_str(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(root);
    if (run < 0)
        return false;
    const bool replace = path.filename() == "session.json";
    const int output = ::openat(
        run, path.filename().c_str(),
        O_WRONLY | O_CREAT | (replace ? 0 : O_EXCL) | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (output < 0) {
        ::close(run);
        return false;
    }
    struct stat status{};
    if (::fstat(output, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        (replace && ::ftruncate(output, 0) != 0)) {
        ::close(output);
        ::close(run);
        return false;
    }
    bool written = true;
    while (!contents.empty()) {
        const auto count = ::write(output, contents.data(), contents.size());
        if (count <= 0) {
            written = false;
            break;
        }
        contents.remove_prefix(static_cast<size_t>(count));
    }
    if (::close(output) != 0)
        written = false;
    if (!written)
        ::unlinkat(run, path.filename().c_str(), 0);
    ::close(run);
    return written;
}

//======================================================================================================================
void EditorShell::finishSessionStep(uint64_t approvalId, bool ok, SessionError error,
                                    std::string message) {
    const auto* approval = m_sessionApprovals.active();
    if (!approval || approval->id != approvalId)
        return;
    const auto id = approval->id;
    const auto command = approval->steps[approval->cursor].command;
    std::string name;
    for (const auto& spec : sessionCommands())
        if (spec.command == command)
            name = spec.name;
    const auto evidence = sessionOutputEvidence(id, ok);
    if (!evidence) {
        const auto certification = "Evidence certification failed: " + evidence.error();
        message = message.empty() ? certification : message + "; " + certification;
        if (ok)
            error = SessionError::Failed;
        ok = false;
    }
    if (!ok)
        m_sessionApprovalFailures[id] = {error, message};
    const auto sequence = m_sessionLog.record(SessionAction{
        .timestampMilliseconds = utcMilliseconds(),
        .actor = Actor::Agent,
        .client = approval->client,
        .command = name,
        .arguments = approval->steps[approval->cursor].arguments,
        .tier = SessionTier::Apply,
        .plan = approval->steps.size() > 1 ? std::optional<uint64_t>(id) : std::nullopt,
        .outcome = ok ? "applied" : message});
    if (evidence)
        for (const auto& file : *evidence)
            m_sessionLog.attach(sequence, file);

    m_sessionApprovals.finishStep(approvalId, ok);
}

//======================================================================================================================
std::expected<std::vector<SessionEvidence>, std::string>
EditorShell::sessionOutputEvidence(uint64_t approval, bool required) {
    const auto outputs = std::exchange(m_sessionStepOutputs[approval], {});
    return hashSessionOutputs(outputs, required);
}

//======================================================================================================================
void EditorShell::stopSessionWork() {
    for (const auto& approval : m_sessionApprovals.pending()) {
        if (approval.state != SessionState::Awaiting && approval.state != SessionState::Working)
            continue;
        recordSessionReview("approval.cancel", std::to_string(approval.request), "cancelled",
                            approval.client);
    }
    if (m_sessionMeasurementApproval) {
        m_measurement.cancel("Cancelled by operator");
        m_sessionJobCancelled = true;
    }
    if (m_sessionChild) {
        m_sessionChild->kill();
        if (m_sessionChildApproval) {
            auto& outputs = m_sessionStepOutputs[*m_sessionChildApproval];
            outputs.push_back((m_sessionChildSequence ? m_sessionChildOutput / "manifest.json"
                                                      : m_sessionChildOutput)
                                  .string());
        }
        m_sessionChild.reset();
        m_sessionChildApproval.reset();
        m_sessionChildOutput.clear();
        m_sessionChildLog.clear();
        m_sessionJobCancelled = true;
    }
    if (m_sessionCaptureApproval) {
        m_actions.consumeCapture();
        auto& result = m_actions.captureResult();
        result.status = ActionStatus::Failed;
        result.message = "Cancelled by operator";
        m_actions.releaseSessionCapture();
        m_sessionJobCancelled = true;
    }
    if (m_pendingSessionMeasurementStart) {
        m_pendingSessionMeasurementStart.reset();
        m_sessionJobCancelled = true;
    }
    m_sessionApprovals.cancelAll();
    m_sessionMeasurementApproval.reset();
    m_sessionCaptureApproval.reset();
    m_pendingSessionMeasurementStart.reset();
    m_sessionJobOutput.clear();
    m_sessionJobCancelled = false;
}

//======================================================================================================================
void EditorShell::cancelAwaitingSessionApprovals(std::string_view reason) {
    for (const auto& approval : m_sessionApprovals.pending()) {
        if (approval.state == SessionState::Awaiting)
            recordSessionReview("approval.cancel", std::to_string(approval.request),
                                std::string(reason), approval.client);
    }
    m_sessionApprovals.cancelPending();
}

//======================================================================================================================
std::expected<bool, std::pair<SessionError, std::string>>
EditorShell::executeSessionStep(const ApprovalStep& step, uint64_t approval) {
    const auto parsed = asset::JsonTokens::parse(step.arguments);
    if (!parsed)
        return std::unexpected(std::pair{SessionError::Invalid, parsed.error().message});
    const auto args = parsed->root();
    const auto refusal =
        [](SessionError code,
           std::string message) -> std::expected<bool, std::pair<SessionError, std::string>> {
        return std::unexpected(std::pair{code, std::move(message)});
    };
    if (m_documentWorkflow.step() != WorkflowStep::Idle)
        return refusal(SessionError::Unavailable, "Finish the current document operation first.");
    if ((step.command == SessionCommand::SettingsSet ||
         step.command == SessionCommand::DebugViewSet ||
         step.command == SessionCommand::SceneOpen) &&
        m_measurement.active())
        return refusal(SessionError::Unavailable, "Stop measurement before changing editor state");
    switch (step.command) {
    case SessionCommand::SettingsSet: {
        const auto name = args.memberName(0);
        const auto value = args.memberValue(0).asString();
        if (!value)
            return refusal(SessionError::Invalid, "Setting value must be a string");
        if (name == "local-light-rig") {
            if (*value != "on" && *value != "off")
                return refusal(SessionError::Invalid, "local-light-rig must be on or off");
            if (!m_session.localLightRigAvailable())
                return refusal(SessionError::Unavailable,
                               "The current scene has no local light rig");
            const bool enabled = *value == "on";
            const auto before = m_session.localLightRigOverride();
            if (auto result = m_session.setLocalLightRig(enabled); !result)
                return refusal(SessionError::Unavailable, result.error().message);
            if (before != m_session.localLightRigOverride())
                m_settingAttribution.mark("setting/local-light-rig",
                                          m_sessionApprovals.active()->client);
            return false;
        }
        const auto key = parseRenderSettingName(name);
        if (!key)
            return refusal(SessionError::Invalid, "Unknown rendering setting");
        const auto before = m_settings;
        if (const auto result = applyRenderSetting(m_settings, *key, *value); !result)
            return refusal(SessionError::Unavailable, result.error());
        constexpr std::array<RenderSettingKey, 10> keys{
            RenderSettingKey::Temporal,       RenderSettingKey::RenderScale,
            RenderSettingKey::Visibility,     RenderSettingKey::Classify,
            RenderSettingKey::ClassifyCheck,  RenderSettingKey::Occlusion,
            RenderSettingKey::OcclusionCheck, RenderSettingKey::Submission,
            RenderSettingKey::LocalLights,    RenderSettingKey::LightCheck};
        for (const auto candidate : keys)
            if (renderSettingValue(before, candidate) != renderSettingValue(m_settings, candidate))
                m_settingAttribution.mark("setting/" + std::string(renderSettingName(candidate)),
                                          m_sessionApprovals.active()->client);
        if (lightingChangeNeedsHistoryReset(before.localLightMode, m_settings.localLightMode,
                                            m_session.scene().enabledLightCount(), false))
            requestCameraCut(m_temporalState);
        return false;
    }
    case SessionCommand::DebugViewSet: {
        const auto view = sessionDebugView(args);
        if (!view)
            return refusal(SessionError::Invalid, view.error());
        if (*view) {
            const auto entries =
                debugViewEntries(m_settings, m_sessionHzbLevels, m_sessionEffectiveReconstruction);
            const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
                return entry.view.topic == (*view)->topic && entry.view.value == (*view)->value;
            });
            if (found == entries.end())
                return refusal(SessionError::Invalid, "Unknown diagnostic view");
            if (!found->available)
                return refusal(SessionError::Unavailable, found->reason);
        }
        selectDebugView(m_settings, *view);
        return false;
    }
    case SessionCommand::SceneOpen: {
        refreshDocumentDirty(true);
        if (const auto reason = DocumentWorkflow::unavailableReason(
                DocumentAction::OpenCatalog, m_playback.state() == PlaybackState::Stopped,
                m_measurement.active(), m_sessionProposals.pendingFile() != nullptr))
            return refusal(SessionError::Unavailable, *reason);
        if (m_sessionProposals.pendingFile())
            return refusal(SessionError::Unavailable, "Review the pending proposal first");
        if (!sessionSceneOpenAllowed(
                m_selection.subject, m_documentDirty, m_playback.state() == PlaybackState::Stopped,
                m_documentWorkflow.step() == WorkflowStep::Idle, m_measurement.active()))
            return refusal(SessionError::Unavailable,
                           "Scene open requires a clean stopped document and stable selection");
        const auto value = args.find("scene")->asString();
        if (!value)
            return refusal(SessionError::Invalid, "scene must be a string");
        const auto id = scenes::parseSceneId(*value).value_or(scenes::sceneIdFromPath(*value));
        if (id == m_activeSceneId)
            return false;
        if (!selectSessionScene(id))
            return refusal(SessionError::Failed, std::string(m_sceneLoading.failureMessage()));
        return false;
    }
    case SessionCommand::MeasureRun: {
        if (m_sessionChild)
            return refusal(SessionError::Unavailable,
                           "Stop the headless capture before starting a measurement");
        if (m_measurement.active() || m_sessionMeasurementApproval || m_sessionCaptureApproval)
            return refusal(SessionError::Busy, "A session job is already running");
        const auto name = args.find("name")->asString();
        const auto output = sessionOutputPath(*name);
        if (!output)
            return refusal(SessionError::Unavailable, output.error());
        m_measurementWarmup = static_cast<uint32_t>(*args.find("warmup")->asUInt());
        m_measurementFrames = static_cast<uint32_t>(*args.find("frames")->asUInt());
        m_sessionJobOutput = *output;
        m_sessionApprovalOutputs[approval].push_back(output->string());
        m_sessionStepOutputs[approval].push_back(output->string());
        m_pendingSessionMeasurementStart = approval;
        return true;
    }
    case SessionCommand::CaptureGpu: {
        if (m_measurement.active() || m_sessionMeasurementApproval || m_sessionCaptureApproval ||
            m_sessionChild || m_actions.capturePending())
            return refusal(SessionError::Busy, "A capture or measurement is already running");
        if (!m_actions.captureAvailable())
            return refusal(SessionError::Unavailable, m_actions.captureResult().message);
        const auto output =
            sessionOutputPath(captureGpuOutputName(approval, m_sessionApprovals.active()->cursor));
        if (!output)
            return refusal(SessionError::Unavailable, output.error());
        if (!sessionCapturePathsAvailable(*output))
            return refusal(SessionError::Unavailable,
                           "Session capture trace or schema output already exists");
        m_sessionJobOutput = *output;
        m_sessionApprovalOutputs[approval].push_back(output->string());
        m_sessionStepOutputs[approval].push_back(output->string());
        m_sessionCaptureApproval = approval;
        m_actions.requestCapture();
        m_actions.reserveSessionCapture();
        m_actions.captureResult().actor = Actor::Agent;
        return true;
    }
    case SessionCommand::GraphDump: {
        const auto* frame = m_renderGraphPanel.snapshot.displayed();
        if (!frame)
            return refusal(SessionError::Unavailable, "No graph frame is published");
        const auto name = args.find("name")->asString();
        const auto output = sessionOutputPath(*name);
        if (!output)
            return refusal(SessionError::Unavailable, output.error());
        if (!writeSessionFile(*output, render::dumpCompiledFrame(frame->record)))
            return refusal(SessionError::Failed, "Could not write the graph dump");
        m_sessionApprovalOutputs[approval].push_back(output->string());
        m_sessionStepOutputs[approval].push_back(output->string());
        return false;
    }
    case SessionCommand::CaptureScreenshot:
    case SessionCommand::CaptureSequence: {
        if (m_measurement.active() || m_sessionMeasurementApproval ||
            m_pendingSessionMeasurementStart || m_sessionCaptureApproval || m_sessionChild ||
            m_actions.capturePending() || m_actions.captureResult().status == ActionStatus::Pending)
            return refusal(SessionError::Busy, "A capture or measurement is already running");
        refreshDocumentDirty(true);
        if (m_documentDirty)
            return refusal(SessionError::Unavailable, "Save the document before headless capture");
        if (m_sessionProposals.pendingFile())
            return refusal(SessionError::Unavailable, "Review the pending file proposal first");
        const auto* loaded = m_session.loadedScene();
        if (!loaded)
            return refusal(SessionError::Unavailable, "No loaded document is available");
        const auto hash = asset::sceneDocumentHash(loaded->path);
        if (!hash || *hash != loaded->hash)
            return refusal(SessionError::Unavailable,
                           "The document on disk differs from the loaded scene");
        const auto name = args.find("name")->asString();
        if (!name || !evidenceName(*name))
            return refusal(SessionError::Invalid, "Capture needs a safe output name");
        const bool sequence = step.command == SessionCommand::CaptureSequence;
        const auto output = sessionOutputPath(sequence ? *name : *name + ".png");
        if (!output)
            return refusal(SessionError::Unavailable, output.error());
        const auto log = sessionOutputPath(*name + ".log");
        if (!log)
            return refusal(SessionError::Unavailable, log.error());
        AppOptions startup;
        startup.generatorOverrides = m_sessionGeneratorOverrides;
        std::vector<std::string> argv{"--scene", loaded->path.string()};
        auto settings = settingsToArguments(m_settings, startup, m_session.localLightRigEnabled());
        argv.insert(argv.end(), std::make_move_iterator(settings.begin()),
                    std::make_move_iterator(settings.end()));
        argv.push_back(sequence ? "--capture-sequence" : "--screenshot");
        argv.push_back(output->string());
        argv.push_back("--frames");
        argv.push_back(std::to_string(*args.find("frames")->asUInt()));
        if (sequence) {
            argv.push_back("--warmup");
            argv.push_back(std::to_string(*args.find("warmup")->asUInt()));
        }
        m_sessionStepOutputs[approval].push_back(log->string());
        auto child = ChildRun::spawn(std::move(argv));
        if (!child)
            return refusal(SessionError::Failed, child.error() + "; log: " + log->string());
        m_sessionChild.emplace(std::move(*child));
        m_sessionChildApproval = approval;
        m_sessionChildOutput = *output;
        m_sessionChildLog = *log;
        m_sessionChildSequence = sequence;
        return true;
    }
    default:
        return refusal(SessionError::Invalid, "Command is not an Apply step");
    }
}

//======================================================================================================================
void EditorShell::runSessionApprovals() {
    if (m_sessionChild && m_sessionChildApproval) {
        if (const auto result = m_sessionChild->poll()) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(m_sessionChildOutput, error);
            const bool outputReady =
                !error && !std::filesystem::is_symlink(status) &&
                (m_sessionChildSequence ? std::filesystem::is_directory(status)
                                        : std::filesystem::is_regular_file(status));
            const auto approval = *m_sessionChildApproval;
            if (*result == 0 && outputReady)
                m_sessionApprovalOutputs[approval].push_back(m_sessionChildOutput.string());
            if (outputReady) {
                m_sessionStepOutputs[approval].push_back(
                    (m_sessionChildSequence ? m_sessionChildOutput / "manifest.json"
                                            : m_sessionChildOutput)
                        .string());
            }
            finishSessionStep(approval, *result == 0 && outputReady, SessionError::Failed,
                              std::format("Headless capture exited {} (log: {})", *result,
                                          m_sessionChildLog.string()));
            m_sessionChild.reset();
            m_sessionChildApproval.reset();
            m_sessionChildOutput.clear();
            m_sessionChildLog.clear();
        }
    }
    if (m_sessionMeasurementApproval && !m_pendingSessionMeasurementStart &&
        !m_measurement.active()) {
        const bool completed = m_measurement.state() == MeasurementState::Complete;
        if (completed) {
            m_measurementExportPath = m_sessionJobOutput.string();
            exportMeasurement();
        }
        finishSessionStep(*m_sessionMeasurementApproval,
                          completed && m_measurementFeedback.starts_with("Exported ") &&
                              !m_sessionJobCancelled,
                          m_sessionJobCancelled ? SessionError::Cancelled : SessionError::Failed,
                          m_sessionJobCancelled ? "Cancelled by operator"
                          : completed           ? m_measurementFeedback
                                                : m_measurement.failure());
        m_sessionMeasurementApproval.reset();
        m_sessionJobCancelled = false;
    }
    if (m_sessionCaptureApproval && m_actions.captureResult().status != ActionStatus::Pending) {
        const auto& result = m_actions.captureResult();
        finishSessionStep(*m_sessionCaptureApproval,
                          result.status == ActionStatus::Succeeded && !m_sessionJobCancelled,
                          m_sessionJobCancelled ? SessionError::Cancelled : SessionError::Failed,
                          m_sessionJobCancelled ? "Cancelled by operator" : result.message);
        m_sessionCaptureApproval.reset();
        m_actions.releaseSessionCapture();
        m_sessionJobCancelled = false;
    }
    if (const auto* step = m_sessionApprovals.next()) {
        const auto approval = m_sessionApprovals.active()->id;
        auto execution = executeSessionStep(*step, approval);
        if (!execution)
            finishSessionStep(approval, false, execution.error().first, execution.error().second);
        else if (!*execution)
            finishSessionStep(approval, true);
    }
    for (const auto& approval : m_sessionApprovals.takeTerminalResults()) {
        const auto reply = m_sessionApprovalConnections.find(approval.id);

        const auto evidence =
            sessionOutputEvidence(approval.id, approval.state == SessionState::Applied);
        std::string outcome = approval.state == SessionState::Applied             ? "applied"
                              : approval.terminalError == SessionError::Cancelled ? "cancelled"
                              : approval.terminalError == SessionError::Denied    ? "denied"
                                                                                  : "failed";
        if (!evidence)
            outcome += "; Evidence certification failed: " + evidence.error();
        const auto sequence =
            m_sessionLog.record(SessionAction{.timestampMilliseconds = utcMilliseconds(),
                                              .actor = Actor::Agent,
                                              .client = approval.client,
                                              .command = "approval.result",
                                              .arguments = std::to_string(approval.request),
                                              .tier = SessionTier::Apply,
                                              .outcome = std::move(outcome)});
        if (evidence)
            for (const auto& file : *evidence)
                m_sessionLog.attach(sequence, file);
        if (reply != m_sessionApprovalConnections.end() && m_sessionMailbox &&
            reply->second == m_sessionConnection) {
            if (approval.state == SessionState::Applied) {
                JsonWriter result;
                result.beginObject();
                result.key("approval");
                result.integer(approval.id);
                result.key("steps");
                result.integer(approval.steps.size());
                result.key("outputs");
                result.beginArray();
                for (const auto& path : m_sessionApprovalOutputs[approval.id])
                    result.string(path);
                result.endArray();
                result.endObject();
                m_sessionMailbox->pushOutbound(reply->second,
                                               encodeResult(approval.request, result.take()));
            } else {
                const auto failure = m_sessionApprovalFailures.find(approval.id);
                const auto code = failure != m_sessionApprovalFailures.end()
                                      ? failure->second.first
                                      : approval.terminalError.value_or(SessionError::Failed);
                const auto message = failure != m_sessionApprovalFailures.end()
                                         ? failure->second.second
                                     : code == SessionError::Denied ? "Denied by operator"
                                                                    : "Approval cancelled";
                m_sessionMailbox->pushOutbound(reply->second,
                                               encodeError(approval.request, code, message));
            }
            m_sessionListener->wake();
        }
        if (reply != m_sessionApprovalConnections.end())
            m_sessionApprovalConnections.erase(reply);
        m_sessionApprovalFailures.erase(approval.id);
        m_sessionApprovalOutputs.erase(approval.id);
        m_sessionStepOutputs.erase(approval.id);
    }
}

} // namespace lmx::app
