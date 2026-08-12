//----------------------------------------------------------------------------------------------------------------------
/// @file StressRunner.h
/// @brief Declares StressRunner for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the M5.1 Stage 4 stress-case dispatch: `--run-stress=<caseId|all>
///        --adapter=<rhi|noapi>` over H01-H24, S-BIND, S-LIFE, I1-I4, and
///        `--run-misuse=<caseId|all>
///        --adapter=<rhi|noapi>` over M1-M6 (plan Stage 4 items 1-3, spec section 7).

#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::noapi::bench {

/// Which adapter a stress or misuse case ran against.
enum class AdapterKind { Rhi, NoApi };

/// One case's outcome: pass/fail plus a short human-readable diagnosis, and (for S-LIFE) the
/// allocation-counter trace the case's header comment requires recording.
struct CaseResult {
    std::string id;
    bool passed = false;
    std::string message; ///< Empty on a plain pass; failure reason, or S-LIFE's counter trace.
    /// Barrier/synchronization primitives this case's single command buffer emitted (spec section
    /// 9's barrier dimension: "count and kind of synchronization primitives each side needs for
    /// H01-H24 and the representative graph"). Populated for H01-H24; zero (the default) for case
    /// kinds this field does not apply to, which is itself accurate -- S-BIND and I1-I4 emit no
    /// barriers of their own, and a setup failure before any command buffer recorded a barrier
    /// truthfully reports zero. Counted at each side's own real choke point: HazardRhi.cpp wraps
    /// every `CommandList::textureBarrier` call site it makes; HazardNoApi.cpp reads
    /// `commandBufferStats().barrierCalls`, which `Source/CommandBuffer.cpp`'s `barrier()` free
    /// function already increments at its own choke point -- no new counting mechanism on either
    /// side, only new surfacing of counters that already existed.
    uint64_t barrierCalls = 0;
};

/// Runs every H01-H24 case (`caseId == "all"`) or exactly one, against `adapter`.
std::vector<CaseResult> runHazardCases(AdapterKind adapter, const std::string& caseId);

/// Runs S-BIND at both frozen scales (`caseId == "all"`) or exactly one (`caseId` in
/// {"S-BIND-1024", "S-BIND-4096"}).
std::vector<CaseResult> runBindCases(AdapterKind adapter, const std::string& caseId);

/// Runs the frozen S-LIFE 12-frame schedule. `caseId` must be "S-LIFE" or "all".
std::vector<CaseResult> runLifeCases(AdapterKind adapter, const std::string& caseId);

/// Runs every I1-I4 case (`caseId == "all"`) or exactly one, against `adapter`.
std::vector<CaseResult> runIndirectCases(AdapterKind adapter, const std::string& caseId);

/// Runs `caseId` (S-BIND/S-LIFE/H*/I* ids, or "all") against `adapter`, printing one
/// "<id> PASS"/"<id> FAIL: <message>" line per case. Returns 0 when every case passed, 1 otherwise.
int runStress(AdapterKind adapter, const std::string& caseId);

/// Runs M1-M6 (`caseId == "all"`) or exactly one as a subprocess death-test against `adapter`,
/// printing "<id> PASS: <captured assert message>" or "<id> FAIL: <reason>". Returns 0 when every
/// requested case died with the expected LMX_ASSERT, 1 otherwise. Recurses into the child process
/// path when invoked with the `LMX_NOAPI_MISUSE`/`LMX_NOAPI_MISUSE_ADAPTER` environment variables
/// already set (see MisuseCases.cpp's header comment for the re-exec protocol).
int runMisuse(AdapterKind adapter, const std::string& caseId);

/// Environment variable a misuse child process reads to select which case to trigger; set by the
/// parent before re-executing argv[0]. Absent in the parent's own process.
inline constexpr const char* kMisuseEnvCase = "LMX_NOAPI_MISUSE";
/// Environment variable naming which adapter ("rhi" or "noapi") the misuse child triggers against.
inline constexpr const char* kMisuseEnvAdapter = "LMX_NOAPI_MISUSE_ADAPTER";

/// Entry point a re-executed misuse child process runs instead of the normal CLI dispatch: reads
/// `kMisuseEnvCase`/`kMisuseEnvAdapter`, triggers exactly that misuse against that adapter, and is
/// expected never to return (the triggered LMX_ASSERT aborts the process). Returns a nonzero exit
/// code if the misuse somehow failed to abort, which the parent reports as a case failure.
int runMisuseChild(const std::string& caseId, AdapterKind adapter);

/// Records argv[0] so a later `runMisuse` call can re-exec this same binary as a misuse child.
/// Must be called once, early in main(), before any `runMisuse` call.
void setMisuseChildExecutablePath(std::string_view argv0);

} // namespace lmx::noapi::bench
