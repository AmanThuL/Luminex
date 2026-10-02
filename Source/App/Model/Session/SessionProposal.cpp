//----------------------------------------------------------------------------------------------------------------------
/// @file SessionProposal.cpp
/// @brief Parses proposal sidecars and retains a bounded review history.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionProposal.h"

#include "App/Model/Session/SessionProtocol.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::app {
namespace {

constexpr size_t kMaxProposals = 64;

//======================================================================================================================
std::expected<std::string, std::string> requiredString(const asset::JsonNode& root,
                                                       std::string_view key) {
    const auto field = root.find(key);
    if (!field)
        return std::unexpected("Missing " + std::string(key));
    if (!field->isString())
        return std::unexpected("Invalid " + std::string(key) + ": expected string");
    return field->asString();
}

//======================================================================================================================
bool isPending(SessionState state) {
    return state == SessionState::Proposed || state == SessionState::Error;
}

} // namespace

//======================================================================================================================
std::expected<Sidecar, std::string> parseSidecar(std::string text) {
    if (!validSessionUtf8(text))
        return std::unexpected("Invalid sidecar UTF-8");
    const auto parsed = asset::JsonTokens::parse(std::move(text));
    if (!parsed)
        return std::unexpected("Invalid JSON: " + parsed.error().message);
    const auto root = parsed->root();
    if (!root.isObject())
        return std::unexpected("Sidecar must be an object");
    const auto schema = root.find("schema");
    if (!schema)
        return std::unexpected("Missing schema");
    const auto version = schema->asUInt();
    if (!version || *version != 1)
        return std::unexpected("Invalid schema: expected 1");

    Sidecar result;
    auto actor = requiredString(root, "actor");
    if (!actor)
        return std::unexpected(actor.error());
    result.actor = std::move(*actor);
    auto summary = requiredString(root, "summary");
    if (!summary)
        return std::unexpected(summary.error());
    result.summary = std::move(*summary);
    auto hash = requiredString(root, "documentSha256");
    if (!hash)
        return std::unexpected(hash.error());
    result.documentSha256 = std::move(*hash);

    if (const auto evidence = root.find("evidence")) {
        if (!evidence->isArray())
            return std::unexpected("Invalid evidence: expected array");
        result.evidence.reserve(evidence->size());
        for (size_t i = 0; i < evidence->size(); ++i) {
            const auto value = evidence->at(i);
            if (!value.isString())
                return std::unexpected("Invalid evidence: expected path string");
            auto path = value.asString();
            if (!path)
                return std::unexpected("Invalid evidence: " + path.error());
            result.evidence.push_back(std::move(*path));
        }
    }
    return result;
}

//======================================================================================================================
std::filesystem::path sidecarPath(const std::filesystem::path& document) {
    const auto filename = document.filename().string();
    constexpr std::string_view suffix = ".scene.gltf";
    if (filename.ends_with(suffix))
        return document.parent_path() /
               (filename.substr(0, filename.size() - suffix.size()) + ".scene.proposal.json");
    return document.parent_path() / (filename + ".proposal.json");
}

//======================================================================================================================
uint64_t ProposalQueue::add(SessionProposal proposal) {
    if (proposal.source == ProposalSource::File) {
        if (proposal.client.empty()) {
            proposal.actor = Actor::System;
            proposal.client = "Unknown external change";
        }
        for (auto& existing : m_proposals)
            if (existing.source == ProposalSource::File && isPending(existing.state))
                existing.state = SessionState::Stale;
    }
    LMX_ASSERT(proposal.source != ProposalSource::Bridge || !isPending(proposal.state) ||
                   !bridgeFull(),
               "Pending bridge proposals are at capacity");
    LMX_ASSERT(m_nextId != 0, "Proposal identity exhausted");
    proposal.id = m_nextId++;
    const uint64_t id = proposal.id;
    m_proposals.push_back(std::move(proposal));
    // Only resolved history is evicted; a proposal awaiting review stays until it is resolved.
    while (m_proposals.size() > kMaxProposals) {
        const auto evictable =
            std::find_if(m_proposals.begin(), m_proposals.end(),
                         [](const SessionProposal& item) { return !isPending(item.state); });
        if (evictable == m_proposals.end())
            break;
        m_proposals.erase(evictable);
    }
    return id;
}

//======================================================================================================================
bool ProposalQueue::bridgeFull() const {
    return static_cast<size_t>(std::count_if(
               m_proposals.begin(), m_proposals.end(), [](const SessionProposal& item) {
                   return item.source == ProposalSource::Bridge && isPending(item.state);
               })) >= kMaxPendingBridge;
}

//======================================================================================================================
SessionProposal* ProposalQueue::find(uint64_t id) {
    const auto it = std::find_if(m_proposals.begin(), m_proposals.end(),
                                 [id](const SessionProposal& item) { return item.id == id; });
    return it == m_proposals.end() ? nullptr : &*it;
}

//======================================================================================================================
const SessionProposal* ProposalQueue::pendingFile() const {
    const auto it =
        std::find_if(m_proposals.rbegin(), m_proposals.rend(), [](const SessionProposal& item) {
            return item.source == ProposalSource::File && isPending(item.state);
        });
    return it == m_proposals.rend() ? nullptr : &*it;
}

//======================================================================================================================
void ProposalQueue::resolve(uint64_t id, SessionState state) {
    auto* proposal = find(id);
    LMX_ASSERT(proposal, "Unknown proposal identity");
    LMX_ASSERT(proposal->state != SessionState::Stale || state == SessionState::Stale,
               "Cannot revive a stale proposal");
    LMX_ASSERT(state != SessionState::Applied || proposal->state == SessionState::Proposed,
               "Cannot apply a stale or unreadable proposal");
    proposal->state = state;
}

//======================================================================================================================
void ProposalQueue::reject(uint64_t id) {
    auto* proposal = find(id);
    LMX_ASSERT(proposal && isPending(proposal->state), "Unknown or resolved proposal identity");
    if (proposal->source == ProposalSource::File && !proposal->hash.empty() &&
        !rejected(proposal->hash)) {
        m_rejectedHashes.push_back(proposal->hash);
    }
    proposal->state = SessionState::Stale;
}

//======================================================================================================================
bool ProposalQueue::rejectIfCurrent(uint64_t id, std::string_view observedHash) {
    auto* proposal = find(id);
    LMX_ASSERT(proposal && proposal->source == ProposalSource::File,
               "File rejection requires a known file proposal");
    if (proposal->hash != observedHash) {
        resolve(id, SessionState::Stale);
        return false;
    }
    reject(id);
    return true;
}

//======================================================================================================================
bool ProposalQueue::rejected(std::string_view hash) const {
    return std::find(m_rejectedHashes.begin(), m_rejectedHashes.end(), hash) !=
           m_rejectedHashes.end();
}

//======================================================================================================================
void ProposalQueue::markStale(ProposalSource source) {
    for (auto& proposal : m_proposals)
        if (proposal.source == source && isPending(proposal.state))
            proposal.state = SessionState::Stale;
    if (source == ProposalSource::File)
        m_rejectedHashes.clear();
}

//======================================================================================================================
std::span<const SessionProposal> ProposalQueue::all() const {
    return m_proposals;
}

//======================================================================================================================
size_t ProposalQueue::pending() const {
    return static_cast<size_t>(
        std::count_if(m_proposals.begin(), m_proposals.end(),
                      [](const SessionProposal& item) { return isPending(item.state); }));
}

//======================================================================================================================
ProposalReviewDetails proposalReviewDetails(const SessionProposal& proposal, bool expanded) {
    return {.changes = expanded ? std::span<const asset::DocumentChange>(proposal.changes)
                                : std::span<const asset::DocumentChange>{},
            .evidence = proposal.evidence};
}

//======================================================================================================================
std::string proposalStatusLine(const SessionProposal& proposal) {
    const auto state = sessionStateLabel(proposal.state);
    return proposal.changes.empty()
               ? std::string(state)
               : std::format("{} changes · {}", proposal.changes.size(), state);
}

//======================================================================================================================
std::string proposalChangeLabel(const asset::DocumentChange& change) {
    std::string_view owner;
    switch (change.owner) {
    case asset::DocumentChangeOwner::Document:
        owner = "Document";
        break;
    case asset::DocumentChangeOwner::Node:
        owner = "Node";
        break;
    case asset::DocumentChangeOwner::Camera:
        owner = "Camera";
        break;
    case asset::DocumentChangeOwner::Light:
        owner = "Light";
        break;
    case asset::DocumentChangeOwner::Look:
        owner = "Look";
        break;
    case asset::DocumentChangeOwner::Animation:
        owner = "Animation";
        break;
    }
    const std::string identity = change.name.empty()
                                     ? std::format("{} {}", owner, change.index)
                                     : std::format("{} {} ({})", owner, change.index, change.name);
    return std::format("{}: {}", identity, change.property.empty() ? "value" : change.property);
}

//======================================================================================================================
bool proposalAffectsNode(const SessionProposal& proposal, const asset::SceneDocument& loaded,
                         uint32_t node) {
    if (node >= loaded.nodes.size())
        return false;
    const auto& subject = loaded.nodes[node];
    return std::any_of(proposal.changes.begin(), proposal.changes.end(),
                       [&](const asset::DocumentChange& change) {
                           switch (change.owner) {
                           case asset::DocumentChangeOwner::Node:
                               return change.index == node;
                           case asset::DocumentChangeOwner::Light:
                               return subject.light && *subject.light == change.index;
                           case asset::DocumentChangeOwner::Camera:
                               return subject.camera && *subject.camera == change.index;
                           default:
                               return false;
                           }
                       });
}

} // namespace lmx::app
