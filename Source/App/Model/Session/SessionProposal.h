//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProposal.h
/// @brief Declares sidecar metadata and the bounded session proposal queue.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionTypes.h"
#include "App/Model/Workspace/Provenance.h"
#include "Engine/Asset/Document/SceneDocumentDiff.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// Metadata written beside a scene document to attribute an external change.
struct Sidecar {
    std::string actor;                 ///< Session client name supplied by the writer.
    std::string summary;               ///< Human-readable description of the change.
    std::string documentSha256;        ///< Hash of the glTF and buffer pair.
    std::vector<std::string> evidence; ///< Paths relative to the sidecar file.
};

/// Parses a schema-one sidecar, ignoring unknown keys and reporting malformed or missing fields.
std::expected<Sidecar, std::string> parseSidecar(std::string text);

/// Returns the adjacent proposal path; replaces a .scene.gltf suffix when present.
std::filesystem::path sidecarPath(const std::filesystem::path& document);

/// Origin of a proposed scene change.
enum class ProposalSource {
    File,  ///< Change found in the loaded document pair on disk.
    Bridge ///< Change submitted through a session connection.
};

/// One requested field edit for a bridge proposal.
struct ProposalEdit {
    std::string subject; ///< Scene subject identifier.
    std::string field;   ///< Editable field name.
    std::string value;   ///< JSON value text.
};

/// Owned proposal data retained after review until queue history evicts it.
struct SessionProposal {
    uint64_t id = 0;                              ///< Queue-assigned identity.
    ProposalSource source = ProposalSource::File; ///< File or bridge origin.
    Actor actor = Actor::System;                  ///< Attributed source actor.
    std::string client;                           ///< Client name or external-change fallback.
    std::string summary;                          ///< Short review description.
    std::string error;                            ///< Reader failure shown in Error state.
    std::string hash;                             ///< File pair hash for rejection suppression.
    std::vector<std::string> evidence;            ///< Evidence paths supplied by the client.
    std::vector<asset::DocumentChange> changes;   ///< Canonical document diff rows.
    std::vector<ProposalEdit> edits;              ///< Bridge edits awaiting review.
    SessionState state = SessionState::Idle;      ///< Current lifecycle state.
};

/// Detail rows displayed by the panel; evidence stays visible when changes are collapsed.
struct ProposalReviewDetails {
    std::span<const asset::DocumentChange> changes; ///< Expanded canonical change rows.
    std::span<const std::string> evidence;          ///< Evidence rows at every expansion state.
};

/// Returns change rows only when expanded and evidence rows even for an Error proposal.
ProposalReviewDetails proposalReviewDetails(const SessionProposal& proposal, bool expanded);

/// Names the owner, stable index, optional name and property of one change row.
std::string proposalChangeLabel(const asset::DocumentChange& change);

/// Main-thread proposal history. Borrowed pointers and spans are invalidated by add; all methods
/// require serialized access. At most 64 proposals are retained; rejected hashes last until the
/// file context is reset.
class ProposalQueue {
public:
    /// Assigns an identity and retains a proposal; a new File proposal stales the previous one.
    uint64_t add(SessionProposal proposal);
    /// Finds a retained proposal by identity, or returns null after eviction.
    SessionProposal* find(uint64_t id);
    /// Returns the newest File proposal in Proposed or Error state, or null.
    const SessionProposal* pendingFile() const;
    /// Changes a known proposal's state; applying a stale proposal violates the caller contract.
    void resolve(uint64_t id, SessionState state);
    /// Stales a known proposal and suppresses its File hash until the file context is reset.
    void reject(uint64_t id);
    /// Reports whether a File hash was rejected in the current file context.
    bool rejected(std::string_view hash) const;
    /// Stales unresolved proposals from a source; File also resets rejected hashes.
    void markStale(ProposalSource source);
    /// Borrows retained proposals in arrival order until the next add or destruction.
    std::span<const SessionProposal> all() const;
    /// Counts proposals ready for review, including reader errors.
    size_t pending() const;

private:
    std::vector<SessionProposal> m_proposals;
    std::vector<std::string> m_rejectedHashes;
    uint64_t m_nextId = 1;
};

} // namespace lmx::app
