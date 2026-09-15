//----------------------------------------------------------------------------------------------------------------------
/// @file Visibility.h
/// @brief Declares the conservative CPU frustum oracle and owned frame diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Bounds.h"
#include "Render/SceneTables.h"
#include <array>
#include <span>
#include <vector>

namespace lmx::render {
/// Candidate geometry and texture bindings supplied by SceneView.
struct DrawItem;
/// Outward distance added to each normalized clipping half-space, in world units.
constexpr float kVisibilityGuardWorldUnits = 1e-3f;
/// Device that classifies candidate bounds.
enum class ClassifyMode {
    Cpu, ///< Maintained CPU oracle and default.
    Gpu  ///< Fixed-slot GPU visibility and work generation.
};
/// Retired per-view counts; bypass index zero is unused.
struct VisibilityCounters {
    uint32_t candidates = 0;            ///< Number of input candidates.
    uint32_t visible = 0;               ///< Tested retained candidates.
    uint32_t rejected = 0;              ///< Tested rejected candidates.
    std::array<uint32_t, 5> bypassed{}; ///< Counts indexed by VisibilityReason.
    uint32_t emittedRows = 0;           ///< Rows that fit all capacities.
    uint32_t emittedCommands = 0;       ///< Nonempty commands that fit.
    uint32_t overflowedRows = 0;        ///< Retained rows dropped by capacity.
    uint32_t overflowedCommands = 0;    ///< Nonempty commands dropped by capacity.
};
/// CPU command preparation path; all paths consume the same visible-row shader contract.
enum class SubmissionMode {
    Direct,   ///< One direct indexed command per retained instance.
    Indirect, ///< One indirect indexed command per retained instance.
    Batched   ///< One indirect instanced command per compatible geometry/material run.
};
/// Classification retained even when no draw is submitted.
enum class VisibilityState {
    Visible,  ///< Bounds overlap every clipping half-space.
    Rejected, ///< Bounds lie fully outside at least one clipping half-space.
    Bypassed  ///< Candidate retained without a bounds rejection test.
};
/// Why the conservative oracle did not reject-test a candidate.
enum class VisibilityReason {
    None,              ///< Ordinary tested candidate.
    Disabled,          ///< User disabled camera culling.
    ViewUnculled,      ///< This view intentionally retains every candidate.
    UnreliableBounds,  ///< Bounds or clipping planes cannot be tested reliably.
    NonFiniteTransform ///< Object transform contains a nonfinite component.
};
/// Five inward normalized half-spaces; reversed infinite projection has no far plane.
struct FrustumPlanes {
    std::array<glm::vec4, 5> planes{}; ///< Left, right, bottom, top, near; dot(n,p)+w >= 0 inside.
    bool valid = false;                ///< Invalid projection conservatively bypasses candidates.
};
/// An owned candidate record, independent of future scene object order.
struct InstanceVisibility {
    uint32_t instanceRow = 0; ///< Stable table row in the retained scene generation.
    VisibilityState state = VisibilityState::Visible; ///< Classification for this frame.
    VisibilityReason reason = VisibilityReason::None; ///< Bypass cause, otherwise None.
    Aabb worldBounds;                                 ///< Exact bounds supplied to the oracle.
};
/// Counts and candidates share one frame and candidate ordering.
struct VisibilityResult {
    std::vector<InstanceVisibility> candidates; ///< Includes every candidate, even rejected.
    std::vector<uint32_t> visibleItems;         ///< Candidate indices retained for submission.
    uint32_t visible = 0;                       ///< Tested candidates that passed.
    uint32_t rejected = 0;                      ///< Tested candidates outside at least one plane.
    std::array<uint32_t, 5> bypassed{};         ///< Indexed by VisibilityReason; None stays zero.
};
/// Published submission diagnostics, with actual commands and bytes for both views.
struct SubmissionStats {
    uint32_t sceneCommands = 0;          ///< Geometry commands, excluding sky and editor passes.
    uint32_t shadowCommands = 0;         ///< Unculled shadow geometry commands.
    uint32_t instancedRuns = 0;          ///< Runs with more than one instance.
    uint64_t listBytes = 0;              ///< Bytes written to this frame's row list.
    uint64_t argumentBytes = 0;          ///< Bytes written to this frame's arguments.
    uint64_t allocatedListBytes = 0;     ///< All three active slot allocations.
    uint64_t allocatedArgumentBytes = 0; ///< All three active slot allocations.
    uint32_t pendingReleaseBuffers = 0;  ///< Replaced buffers awaiting retirement.
    uint64_t candidateBytes = 0;         ///< Logical candidate table bytes.
    uint64_t runBytes = 0;               ///< Logical run table bytes.
    uint64_t chunkBytes = 0;             ///< Logical chunk table bytes.
    uint64_t stateBytes = 0;             ///< State storage bytes.
    uint64_t counterBytes = 0;           ///< Counter storage bytes.
    uint32_t growthEvents = 0;           ///< Cumulative paired slot growth count.
};
/// Renderer-owned diagnostics for the last declared frame.
struct VisibilityStatus {
    ClassifyMode classifyMode = ClassifyMode::Cpu; ///< Requested classifier.
    bool isRetired = false;            ///< GPU results have completed and been read back.
    bool overflow = false;             ///< At least one output capacity dropped work.
    bool checkEnabled = false;         ///< Declaration captured CPU oracle expectations.
    uint32_t stateMismatches = 0;      ///< Candidate state or reason differences.
    uint32_t rowMismatches = 0;        ///< Valid row sequence differences.
    uint32_t argumentMismatches = 0;   ///< Defined argument word differences.
    uint32_t counterMismatches = 0;    ///< Counter reconciliation differences.
    VisibilityCounters sceneCounters;  ///< Retired scene-view counters.
    VisibilityCounters shadowCounters; ///< Retired shadow-view counters.
    /// Whether every enabled diagnostic comparison matched.
    bool checkPassed() const {
        return stateMismatches == 0 && rowMismatches == 0 && argumentMismatches == 0 &&
               counterMismatches == 0;
    }
    uint64_t frameNumber = 0;     ///< Device frame that owns the results.
    uint64_t sceneGeneration = 0; ///< Caller scene identity generation.
    VisibilityResult scene;       ///< Camera candidate classification.
    VisibilityResult shadow;      ///< Shadow bypass classification.
    SubmissionStats submission;   ///< Prepared command and byte counts.
    double classifyMs = 0;        ///< CPU classification scope only.
    double prepareMs = 0;         ///< CPU list/argument construction and upload scope.
};
/// Extracts the five planes rasterization clips against, with the fixed outward guard.
FrustumPlanes extractFrustumPlanes(const glm::mat4& viewProjection);
/// Classifies one shared row; nonfinite inputs are always retained conservatively.
InstanceVisibility classifyInstance(const FrustumPlanes& planes, const InstanceRow& row,
                                    uint32_t instanceRow, bool enabled = true,
                                    bool viewUnculled = false);
/// Classifies candidate order against canonical uploaded CPU rows.
VisibilityResult classifyView(const FrustumPlanes& planes, std::span<const DrawItem> items,
                              const SceneTables& tables, bool enabled = true,
                              bool viewUnculled = false);
} // namespace lmx::render
