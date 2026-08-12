//----------------------------------------------------------------------------------------------------------------------
/// @file StressCommon.h
/// @brief Declares StressCommon for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Shared, API-neutral helpers for M5.1 Stage 4's stress-case adapters: deterministic
/// content
///        generation for the hazard matrix (spec section 7), shared with both HazardRhi.cpp and
///        HazardNoApi.cpp so the two sides assert against byte-identical expectations.

#pragma once
#include "Bench/Metrics.h"
#include "Bench/StressRunner.h"
#include "Workload/StressCases.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lmx::experimental::noapi::bench {

/// RGBA8 content for `extent` x `extent` texels: byte b = hazardExpectedTexel(hazardCase,
/// texelIndex) replicated into R, G, and B with alpha fixed at 255, tightly packed row-major.
std::vector<uint8_t> hazardExpectedRgba(const workload::HazardCase& hazardCase, uint32_t extent);

/// The WAR/WAW "old" content every case's earlier write-or-read op establishes before the barrier:
/// the bitwise complement of hazardExpectedRgba's bytes (RGB channels only; alpha stays 255), so a
/// case that skips or misorders its barrier is distinguishable from one that honors it -- the
/// earlier and later values are never equal by construction.
std::vector<uint8_t> hazardOldRgba(const workload::HazardCase& hazardCase, uint32_t extent);

/// Compares two RGB(A) byte buffers, ignoring the alpha channel of every fourth byte (alpha is
/// fixed content, not part of the case's own oracle). Returns the byte offset of the first mismatch
/// or -1 when the buffers agree.
int64_t firstRgbMismatch(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual);

// Per-adapter case-group implementations. StressRunner.cpp's public dispatch functions
// (runHazardCases, runBindCases, runLifeCases, runIndirectCases) select between the Rhi/NoApi pair
// below by AdapterKind.
std::vector<CaseResult> runHazardCasesRhi(const std::string& caseId);
std::vector<CaseResult> runHazardCasesNoApi(const std::string& caseId);
std::vector<CaseResult> runBindCasesRhi(const std::string& caseId);
std::vector<CaseResult> runBindCasesNoApi(const std::string& caseId);
std::vector<CaseResult> runLifeCasesRhi(const std::string& caseId);
std::vector<CaseResult> runLifeCasesNoApi(const std::string& caseId);
std::vector<CaseResult> runIndirectCasesRhi(const std::string& caseId);
std::vector<CaseResult> runIndirectCasesNoApi(const std::string& caseId);
int runMisuseChildRhi(const std::string& caseId);
int runMisuseChildNoApi(const std::string& caseId);

// M5.1 Stage 4 measurement (plan Stage 4 item 4-5, spec section 8): S-BIND is also a timed workload
// ("its setup/per-frame split is documented" -- BindRhi.cpp/BindNoApi.cpp's header comments). Each
// runs `warmupFrames` untimed encode+submit+wait iterations followed by `measuredFrames` timed ones
// over the same frozen `drawCount`-draw scene, reusing exactly the one-time setup (textures,
// target, pipeline, sampler, and the per-draw param buffers) both header comments already document
// as outside any timed region.
MeasuredRun measureBindScaleRhi(uint32_t drawCount, uint32_t warmupFrames, uint32_t measuredFrames);
MeasuredRun measureBindScaleNoApi(uint32_t drawCount, uint32_t warmupFrames,
                                  uint32_t measuredFrames);

} // namespace lmx::experimental::noapi::bench
