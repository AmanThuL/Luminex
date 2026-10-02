//----------------------------------------------------------------------------------------------------------------------
/// @file SessionApply.h
/// @brief Declares validated, immutable Apply requests and run-local output naming.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Rendering/Settings/DebugView.h"
#include "App/Model/Scene/DocumentWorkflow.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Session/SessionApprovals.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lmx::app {

/// Owned command steps copied into an operator approval before any execution begins.
struct ParsedApplyRequest {
    std::string summary;             ///< Operator-facing request or plan summary.
    std::vector<ApprovalStep> steps; ///< Validated immutable steps in execution order.
};

/// Validates the shape of one Apply command or plan, including exact member names, evidence names
/// and integer bounds.
/// Runtime preconditions are checked again immediately before each approved step executes.
std::expected<ParsedApplyRequest, std::string> parseApplyRequest(SessionCommand command,
                                                                 const asset::JsonNode& args);

/// Parses a command's diagnostic request; empty args select Final.
std::expected<std::optional<DebugView>, std::string> sessionDebugView(const asset::JsonNode& args);

/// Names a run-local directory relative to the build directory using a UTC start and process id.
std::string sessionDirectoryName(int64_t utcSeconds, uint32_t processId);

/// Gives each GPU-capture step in an approved plan a distinct confined output name.
std::string captureGpuOutputName(uint64_t approval, size_t zeroBasedStep);

/// Names the run-local output an approved step writes, as the approval card shows it: the name
/// argument, with the `.png` suffix a screenshot receives, or the generated GPU-capture name.
/// Returns empty for a step that writes no named output or whose arguments carry no name.
std::string sessionStepOutputName(const ApprovalStep& step, uint64_t approval,
                                  size_t zeroBasedStep);

/// Checks the trace bundle and both sidecar paths that the capture backend may write.
/// An existing file, directory or dangling symlink at any path is a collision.
bool sessionCapturePathsAvailable(const std::filesystem::path& trace);

/// Allows a bridge scene switch only when the semantic selection, camera and stopped transport
/// can survive it; detailed document and scene availability checks remain shell-owned.
bool sessionSceneOpenAllowed(EditorSubject subject, bool dirty, bool stopped, bool documentIdle,
                             bool measuring);

/// Refuses the operator's Open, catalog open and Revert while an approved plan is Working: its
/// remaining steps were approved for the loaded scene and would otherwise run against the new one.
/// Returns the notice text, which names the control that ends the plan; Save, Save As and Quit are
/// never refused here.
std::optional<std::string> runningPlanRefusal(DocumentAction action, bool planWorking);

} // namespace lmx::app
