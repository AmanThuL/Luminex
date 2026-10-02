//----------------------------------------------------------------------------------------------------------------------
/// @file SessionQueries.h
/// @brief Declares read-only JSON views of editor session state and stable subject lookup.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Console/ConsoleLog.h"
#include "App/Model/Performance/PerformanceModel.h"
#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Model/Session/SessionLog.h"
#include "App/Model/Session/SessionProposal.h"
#include "App/Model/Session/SessionProtocol.h"
#include "Core/IO/JsonWriter.h"
#include "Engine/View/Camera.h"
#include "Render/Passes/LocalLights/LightingStatus.h"
#include "Render/Passes/Visibility/Visibility.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Largest query.log result, leaving room for the response envelope within one line.
inline constexpr size_t kMaxLogReplyBytes = kMaxLineBytes - 4096;

/// Writes a finite value as a JSON number, and "infinite", "-infinite" or "nan" as a string, so
/// a non-finite editor value reaches the client instead of failing the writer's finite contract.
void writeSessionNumber(JsonWriter& writer, float value);
/// Double overload of writeSessionNumber, with the same non-finite strings.
void writeSessionNumber(JsonWriter& writer, double value);

/// Immutable values observed at the editor safe point for a status reply.
struct SessionStatus {
    std::string documentPath; ///< Loaded document path, or empty before scene activation.
    std::string documentHash; ///< Loaded canonical pair hash, excluding unsaved edits.
    bool dirty = false;       ///< Current in-memory document differs from its loaded baseline.
    std::string playback;     ///< Stopped, playing or paused in lowercase.
    bool measuring = false;   ///< Interactive measurement is active.
    SessionTier tier = SessionTier::ReadOnly; ///< Current connection ceiling.
    size_t pendingProposals = 0;              ///< Proposals awaiting operator action.
    std::string job = "idle";                 ///< Current session work label.
};

/// Serializes every supplied subject row, excluding the structural root. Call with a tree built
/// using empty search and no collapsed groups for a complete scene view.
std::string hierarchyJson(const SceneTreeView& tree);
/// Returns the exact identifier exposed by query.hierarchy for a row, or empty for its root.
std::string sceneTreeSubjectId(const SceneTreeRow& row);
/// Finds an exact subject identifier in the supplied complete tree, retaining full light identity.
/// Missing and stale identifiers return no selection; the caller supplies the active scene id.
std::optional<EditorSelection> parseSubjectId(std::string_view id, const SceneTreeView& tree);
/// Serializes the selected subject using the same identifiers as hierarchyJson.
std::string selectionJson(const EditorSelection& selection, const SceneTreeView& tree);
/// Serializes the current editor viewport camera without changing it.
std::string cameraJson(const engine::Camera& camera);
/// Serializes CLI-named settings and their current requested values.
std::string settingsJson(const EditorRenderSettings& settings);
/// Serializes the coherent published performance snapshot and all retained pass summaries.
std::string performanceJson(const PerformanceSnapshot& snapshot);
/// Serializes retained Console entries whose sequence exceeds afterSequence.
std::string consoleJson(const ConsoleSnapshot& snapshot, uint64_t afterSequence);
/// Serializes retained proposals and their lifecycle states.
std::string proposalsJson(const ProposalQueue& proposals);
/// Serializes retained session actions whose sequence exceeds afterSequence, oldest first, with
/// nextSequence and the log's dropped count. When the rows exceed maxBytes the newest that fit are
/// kept and omitted counts the older matching rows left out.
std::string logJson(const SessionLog& log, uint64_t afterSequence = 0,
                    size_t maxBytes = kMaxLogReplyBytes);
/// Serializes the current editor and connection state without reading mutable owners.
std::string statusJson(const SessionStatus& status);
/// Composes the existing visibility and lighting diagnostics in one query result.
std::string readingsJson(const render::VisibilityStatus& visibility,
                         const render::LightingStatus& lighting);
/// Builds the operator's tier-change audit row with the connected client and new ceiling.
SessionAction sessionTierAction(std::string client, SessionTier ceiling,
                                int64_t timestampMilliseconds);

} // namespace lmx::app
