//----------------------------------------------------------------------------------------------------------------------
/// @file Provenance.h
/// @brief Classifies editor value sources and whether their changes are saved.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Identifies human input, editor policies, and software-attributed change sources.
enum class Actor {
    Operator, ///< Human input, including command-line overrides.
    System,   ///< Editor generation or an automatic policy.
    Agent,    ///< Software-attributed input with a separately recorded source.
};

/// Persistence and application status of an editor value, independent of selection.
enum class Provenance {
    Authored,      ///< Saved baseline; no visible mark is needed.
    Edited,        ///< Operator edits differ from the saved baseline.
    SessionOnly,   ///< Generated or overridden state that is not saved.
    SystemApplied, ///< An editor policy supplied the current value.
    Proposed,      ///< Unapplied software-attributed proposal.
    AgentApplied,  ///< Applied software-attributed change.
};

/// Owned attribution for a tooltip; copies remain valid after input strings are destroyed.
struct ProvenanceMark {
    Provenance kind;    ///< Persistence or application status.
    Actor actor;        ///< Initiator of the current change.
    std::string source; ///< Owned source description, retaining caller-spelled identifiers.
};

/// Returns operator Edited for a dirty document, naming its caller-spelled loaded path.
/// A clean document returns no mark. Path is borrowed only for this call; no file access occurs.
std::optional<ProvenanceMark> documentProvenance(bool dirty, std::string_view path);

/// Classifies a subject without changing it or its saved state; strings are borrowed for the call.
/// Generated and CLI-masked subjects stay SessionOnly even when edited; both sources are retained
/// when present. Edits or CLI masks attribute the mark to the operator, otherwise generation is
/// system-owned. A plain edited subject is Edited; a plain authored subject returns no mark.
std::optional<ProvenanceMark> subjectProvenance(std::optional<std::string_view> generatedBy,
                                                std::optional<std::string_view> cliFlag,
                                                bool edited);

/// Returns SystemApplied naming dynamic resolution's scale and GPU budget only while active.
/// Scale is the finite unitless render/output ratio in [0.5, 1]; budgetMs is finite and positive
/// in milliseconds. This describes caller-supplied state without driving or clamping the policy.
std::optional<ProvenanceMark> resolutionProvenance(bool controllerOn, float scale, float budgetMs);

} // namespace lmx::app
