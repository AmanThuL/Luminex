//----------------------------------------------------------------------------------------------------------------------
/// @file SessionEdits.h
/// @brief Declares validated scene-edit proposals and field attribution.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Scene/SceneSession.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Model/Session/SessionProposal.h"
#include "App/Model/Workspace/Provenance.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lmx::app {

/// Parses a nonempty array of typed values; malformed or unknown edit members fail the request.
std::expected<std::vector<ProposalEdit>, std::string> parseEdits(const asset::JsonNode& args);

/// Validates the entire batch against current identities and combined field constraints without
/// changing the scene, document, camera, edit generation, or GPU state. Returns review rows.
std::expected<std::vector<asset::DocumentChange>, std::string>
previewEdits(const SceneSession& session, const SceneTreeView& tree,
             std::span<const ProposalEdit> edits);

/// Refuses a batch that sets the position of a local light an orbit track drives while playback is
/// not Stopped. Its before value would be the orbit's sampled position, Accept needs Stopped, and
/// Stop restores the captured position, so the reviewed rows could never be current at Accept.
/// Returns the message naming the subject; every other edit may be proposed during playback.
std::optional<std::string> animationOwnedEditRefusal(const SceneSession& session,
                                                     const SceneTreeView& tree,
                                                     std::span<const ProposalEdit> edits,
                                                     bool playbackStopped);

/// Re-runs the preview against the live scene and reports why the reviewed rows no longer describe
/// what applying the edits would change: a named subject is gone or no longer editable, or a later
/// edit altered a before or after value (exposure and bloom edits merge onto the current look).
/// Success means Accept applies exactly the rows the operator was shown. The document's file hash
/// takes no part, so Save and Save As leave a reviewed proposal current.
std::expected<void, std::string>
reviewedEditsCurrent(const SceneSession& session, const SceneTreeView& tree,
                     std::span<const ProposalEdit> edits,
                     std::span<const asset::DocumentChange> reviewed);

/// Returns only fields whose final value differs from the current persistent value. Duplicate
/// edits to one field yield one key; no-op requests yield none.
std::expected<std::vector<std::string>, std::string>
changedEditKeys(const SceneSession& session, const SceneTreeView& tree,
                std::span<const ProposalEdit> edits);

/// Reports whether the edited rendered content needs the Inspector's temporal camera-cut latch.
/// A saved scene-camera request alone leaves the editor viewport and its temporal state untouched.
bool sessionEditNeedsCameraCut(const SceneTreeView& tree, std::span<const ProposalEdit> edits);

/// Revalidates the entire batch, then applies it on the editor thread. A failed validation changes
/// nothing; the caller requires stopped playback and no active measurement.
rojoRHI::Result<void> applyEdits(SceneSession& session, const SceneTreeView& tree,
                                 std::span<const ProposalEdit> edits);

/// Tracks which saved fields came from a reviewed session proposal until Save or Revert.
class SessionAttribution {
public:
    /// Associates one subject/field or setting/name key with the submitting client.
    void mark(std::string key, std::string client = {});
    /// Reports whether a key was applied by a session client.
    bool has(std::string_view key) const;
    /// Returns the client's owned name for a key, or empty when absent.
    std::string_view client(std::string_view key) const;
    /// Removes one mark after the operator edits or resets that field.
    void erase(std::string_view key);
    /// Removes all marks after Save, Revert, or scene replacement.
    void clear();

private:
    std::unordered_map<std::string, std::string> m_clients;
};

/// Formats a selected subject using query.hierarchy's identifier precedence.
std::string sessionSubjectId(const EditorSelection& selection);

} // namespace lmx::app
