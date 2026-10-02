//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProposal.h
/// @brief Declares sidecar metadata and the bounded session proposal queue.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionTypes.h"
#include "App/Model/Workspace/Provenance.h"
#include "Engine/Asset/Document/SceneDocument.h"
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
    uint64_t connection = 0;                      ///< Submitting connection; zero for File.
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

/// Formats the card's status line: the change count and state, or the state alone when the card
/// carries no change rows (an approval, or a file the reader rejected).
std::string proposalStatusLine(const SessionProposal& proposal);

/// Names the owner, stable index, optional name and property of one change row.
std::string proposalChangeLabel(const asset::DocumentChange& change);

/// Decides whether a propose.withdraw request may stale a proposal: a Bridge proposal still
/// awaiting review, asked for by the connection that submitted it, or, once that connection is no
/// longer the live one, by a client using the same name. A reconnecting client keeps control of
/// its own cards; while the submitting connection lives, no other connection can withdraw them.
bool proposalWithdrawable(const SessionProposal& proposal, uint64_t requestConnection,
                          uint64_t liveConnection, std::string_view requestClient);

/// Returns whether a loaded Hierarchy node owns a changed node, camera, or shared light definition.
bool proposalAffectsNode(const SessionProposal& proposal, const asset::SceneDocument& loaded,
                         uint32_t node);

/// Main-thread proposal history. Borrowed pointers and spans are invalidated by add; all methods
/// require serialized access. Resolved history is evicted oldest first beyond 64 retained
/// proposals; a proposal still awaiting review is never evicted, so the queue holds at most 64
/// pending Bridge proposals and one pending File proposal. Rejected hashes last until the file
/// context is reset.
class ProposalQueue {
public:
    /// Most Bridge proposals that may await review at once.
    static constexpr size_t kMaxPendingBridge = 64;

    /// Assigns an identity and retains a proposal; a new File proposal stales the previous one.
    /// Adding a pending Bridge proposal while bridgeFull() violates the caller contract.
    uint64_t add(SessionProposal proposal);
    /// Reports whether kMaxPendingBridge Bridge proposals await review; the caller refuses more.
    bool bridgeFull() const;
    /// Finds a retained proposal by identity, or returns null after eviction.
    SessionProposal* find(uint64_t id);
    /// Returns the newest File proposal in Proposed or Error state, or null.
    const SessionProposal* pendingFile() const;
    /// Changes a known proposal's state; applying a stale proposal violates the caller contract.
    void resolve(uint64_t id, SessionState state);
    /// Stales a known proposal and suppresses its File hash until the file context is reset.
    void reject(uint64_t id);
    /// Rejects only the hash the operator reviewed; a newer hash stales the card without
    /// suppressing either hash and must be discovered by the watcher.
    bool rejectIfCurrent(uint64_t id, std::string_view observedHash);
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
