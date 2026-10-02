//----------------------------------------------------------------------------------------------------------------------
/// @file SessionPanel.cpp
/// @brief Draws proposal cards, evidence actions and ordered session history.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Session/SessionPanel.h"

#include "App/Model/Session/SessionApply.h"
#include "App/Model/Session/SessionCommands.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {
namespace {

//======================================================================================================================
std::filesystem::path evidencePath(const SessionPanelContext& context, std::string_view entry) {
    const std::filesystem::path path(entry);
    return path.is_absolute() ? path : context.evidenceDirectory / path;
}

//======================================================================================================================
void drawEvidence(const SessionPanelContext& context, std::string_view entry) {
    const auto path = evidencePath(context, entry);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error) && !error;
    ImGui::TextWrapped("Evidence: %s%s", path.string().c_str(), exists ? "" : " (missing)");
    if (editor_style::primaryButton("Copy path")) {
        context.pathFeedback = SDL_SetClipboardText(path.string().c_str())
                                   ? "Copied evidence path."
                                   : std::format("Copy path failed: {}", SDL_GetError());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!exists);
    if (editor_style::primaryButton("Reveal")) {
        const std::string absolute = std::filesystem::absolute(path, error).string();
        if (error) {
            context.pathFeedback = "Reveal failed: " + error.message();
        } else {
            const char* args[] = {"/usr/bin/open", "-R", absolute.c_str(), nullptr};
            SDL_Process* process = SDL_CreateProcess(args, false);
            if (process) {
                int exitCode = 0;
                const bool finished = SDL_WaitProcess(process, true, &exitCode);
                SDL_DestroyProcess(process);
                context.pathFeedback =
                    !finished       ? std::format("Reveal failed: {}", SDL_GetError())
                    : exitCode != 0 ? std::format("Reveal failed (open exited {}).", exitCode)
                                    : "Revealed evidence in Finder.";
            } else {
                context.pathFeedback = std::format("Reveal failed: {}", SDL_GetError());
            }
        }
    }
    ImGui::EndDisabled();
}

} // namespace

//======================================================================================================================
SessionPanelResult drawSessionPanel(bool& open, SessionPanelContext& context) {
    SessionPanelResult result;
    if (!ImGui::Begin(kSessionWindowName, &open)) {
        ImGui::End();
        return result;
    }

    if (editor_style::primaryButton(context.listening ? "Stop listening" : "Listen"))
        result.action = SessionPanelAction::ToggleListen;
    if (context.listening) {
        const auto path = context.socketPath.string();
        ImGui::SameLine();
        if (editor_style::primaryButton("Copy socket path"))
            context.pathFeedback = SDL_SetClipboardText(path.c_str())
                                       ? "Copied socket path."
                                       : std::format("Copy path failed: {}", SDL_GetError());
        ImGui::TextWrapped("Socket: %s", path.c_str());
    }
    ImGui::Text("Client: %s",
                context.client.empty() ? "None" : std::string(context.client).c_str());
    constexpr const char* tierNames[] = {"Read-only", "Propose", "Apply with approval"};
    int tier = static_cast<int>(context.tier);
    ImGui::BeginDisabled(context.client.empty());
    if (ImGui::Combo("Ceiling", &tier, tierNames, 3))
        result = {SessionPanelAction::SetTier, 0, static_cast<SessionTier>(tier)};
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::TextUnformatted("Approvals");
    const auto* approval = context.approvals.active();
    if (approval && approval->state != SessionState::Awaiting)
        approval = nullptr;
    // A card that replaced another under the pointer ignores clicks until it has been shown.
    const bool acceptsClicks =
        context.approvalGuard.accepts(approval ? approval->id : 0, ImGui::GetTime());
    if (approval) {
        SessionProposal card;
        card.id = approval->id;
        card.source = ProposalSource::Bridge;
        card.actor = Actor::Agent;
        card.client = approval->client;
        card.summary = approval->summary;
        card.state = SessionState::Awaiting;
        editor_style::CardLabels labels;
        labels.accept = "Approve";
        labels.reject = "Deny";
        labels.show = false;
        ImGui::PushID("approval");
        switch (editor_style::proposalCard(card, labels)) {
        case editor_style::CardAction::Accept:
            if (acceptsClicks)
                result = {SessionPanelAction::Approve, approval->id};
            break;
        case editor_style::CardAction::Reject:
            if (acceptsClicks)
                result = {SessionPanelAction::Deny, approval->id};
            break;
        default:
            break;
        }
        for (size_t index = 0; index < approval->steps.size(); ++index) {
            const auto& step = approval->steps[index];
            std::string_view name = "unknown";
            for (const auto& spec : sessionCommands())
                if (spec.command == step.command)
                    name = spec.name;
            editor_style::proposedValue(std::format("{}. {}", index + 1, name), "", step.arguments);
            if (const auto output = sessionStepOutputName(step, approval->id, index);
                !output.empty())
                ImGui::TextWrapped("Output: %s",
                                   (context.outputDirectory / output).string().c_str());
        }
        ImGui::PopID();
    } else {
        ImGui::TextUnformatted("No approval awaiting review");
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Proposals");
    if (context.proposals.pending() == 0)
        ImGui::TextUnformatted("No proposals");
    const auto drawn = [](const SessionProposal& proposal) {
        return proposal.state == SessionState::Proposed ||
               proposal.state == SessionState::Awaiting || proposal.state == SessionState::Error;
    };
    // A card that left moves the ones below it under the pointer; so does the approval card.
    std::vector<uint64_t> shownCards{approval ? approval->id : 0};
    for (const auto& proposal : context.proposals.all())
        if (drawn(proposal))
            shownCards.push_back(proposal.id);
    const bool proposalsAcceptClicks = context.proposalGuard.accepts(shownCards, ImGui::GetTime());
    for (const auto& proposal : context.proposals.all()) {
        if (!drawn(proposal))
            continue;
        ImGui::PushID(static_cast<int>(proposal.id));
        editor_style::CardLabels labels;
        labels.acceptDisabledReason = proposal.state == SessionState::Error
                                          ? "This proposal failed and cannot be accepted."
                                          : "";
        switch (editor_style::proposalCard(proposal, labels)) {
        case editor_style::CardAction::Show:
            context.expandedId = context.expandedId == proposal.id ? 0 : proposal.id;
            break;
        case editor_style::CardAction::Accept:
            if (proposalsAcceptClicks)
                result = {SessionPanelAction::Accept, proposal.id};
            break;
        case editor_style::CardAction::Reject:
            if (proposalsAcceptClicks)
                result = {SessionPanelAction::Reject, proposal.id};
            break;
        case editor_style::CardAction::None:
            break;
        }
        const auto details = proposalReviewDetails(proposal, context.expandedId == proposal.id);
        for (const auto& change : details.changes)
            editor_style::proposedValue(proposalChangeLabel(change), change.before, change.after);
        for (const auto& evidence : details.evidence) {
            ImGui::PushID(evidence.c_str());
            drawEvidence(context, evidence);
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    if (!context.pathFeedback.empty())
        ImGui::TextWrapped("%s", context.pathFeedback.c_str());
    ImGui::Separator();
    if (editor_style::primaryButton("Export"))
        result.action = SessionPanelAction::Export;
    editorTooltip("Export ordered actions, evidence hashes and the held Console view using "
                  "operator and session client messages.");
    ImGui::TextUnformatted("Session log");
    if (context.log.actions().empty())
        ImGui::TextUnformatted("No session actions");
    for (const auto& action : context.log.actions()) {
        ImGui::PushID(static_cast<int>(action.sequence));
        editor_style::actorChip(action.actor, action.client.empty() ? "Session" : action.client);
        ImGui::SameLine();
        ImGui::TextWrapped("%s · %s", action.command.c_str(), action.outcome.c_str());
        ImGui::PopID();
    }
    ImGui::End();
    return result;
}

} // namespace lmx::app
