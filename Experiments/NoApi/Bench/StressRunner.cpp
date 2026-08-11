//----------------------------------------------------------------------------------------------------------------------
/// @file StressRunner.cpp
/// @brief Implements StressRunner.h's dispatch and the M1-M6 subprocess death-test protocol.
///
/// Parent/child protocol for `runMisuse`: the parent re-execs this same NoApiBench binary
/// (`kMisuseChildExecutablePath`, captured from argv[0] at process start by
/// `setMisuseChildExecutablePath`) with `kMisuseEnvCase`/`kMisuseEnvAdapter` set in the child's
/// environment and its stderr redirected to a pipe. `main()` (Tests/NoApiBenchMain.cpp) checks
/// those two variables before any normal argument parsing and, when both are set, calls
/// `runMisuseChild` instead -- which triggers exactly the named misuse and is expected to never
/// return (the LMX_ASSERT it hits calls std::abort(), raising SIGABRT). The parent waits for the
/// child, reads the captured stderr, and treats "the child died on SIGABRT and its stderr contains
/// an LMX_ASSERT line naming the case's contract" as a pass.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressRunner.h"
#include "Bench/StressCommon.h"
#include "Workload/StressCases.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace lmx::noapi::bench {

namespace {
std::string g_executablePath;
} // namespace

//======================================================================================================================
void setMisuseChildExecutablePath(std::string_view argv0) {
    g_executablePath = std::string(argv0);
}

//======================================================================================================================
std::vector<CaseResult> runHazardCases(AdapterKind adapter, const std::string& caseId) {
    return adapter == AdapterKind::Rhi ? runHazardCasesRhi(caseId) : runHazardCasesNoApi(caseId);
}
std::vector<CaseResult> runBindCases(AdapterKind adapter, const std::string& caseId) {
    return adapter == AdapterKind::Rhi ? runBindCasesRhi(caseId) : runBindCasesNoApi(caseId);
}
std::vector<CaseResult> runLifeCases(AdapterKind adapter, const std::string& caseId) {
    return adapter == AdapterKind::Rhi ? runLifeCasesRhi(caseId) : runLifeCasesNoApi(caseId);
}
std::vector<CaseResult> runIndirectCases(AdapterKind adapter, const std::string& caseId) {
    return adapter == AdapterKind::Rhi ? runIndirectCasesRhi(caseId)
                                       : runIndirectCasesNoApi(caseId);
}

//======================================================================================================================
namespace {
bool idIsHazard(const std::string& id) {
    return !id.empty() && id[0] == 'H';
}
bool idIsIndirect(const std::string& id) {
    return !id.empty() && id[0] == 'I' && id != "all";
}
} // namespace

//======================================================================================================================
int runStress(AdapterKind adapter, const std::string& caseId) {
    std::vector<CaseResult> results;
    if (caseId == "all") {
        std::vector<CaseResult> hazard = runHazardCases(adapter, "all");
        std::vector<CaseResult> bind = runBindCases(adapter, "all");
        std::vector<CaseResult> life = runLifeCases(adapter, "all");
        std::vector<CaseResult> indirect = runIndirectCases(adapter, "all");
        results.insert(results.end(), hazard.begin(), hazard.end());
        results.insert(results.end(), bind.begin(), bind.end());
        results.insert(results.end(), life.begin(), life.end());
        results.insert(results.end(), indirect.begin(), indirect.end());
    } else if (idIsHazard(caseId)) {
        results = runHazardCases(adapter, caseId);
    } else if (caseId.starts_with("S-BIND")) {
        results = runBindCases(adapter, caseId);
    } else if (caseId == "S-LIFE") {
        results = runLifeCases(adapter, caseId);
    } else if (idIsIndirect(caseId)) {
        results = runIndirectCases(adapter, caseId);
    } else {
        std::cerr << "runStress: unknown case id '" << caseId << "'\n";
        return 1;
    }

    if (results.empty()) {
        std::cerr << "runStress: no case matched '" << caseId << "'\n";
        return 1;
    }

    bool allPassed = true;
    for (const CaseResult& result : results) {
        if (result.passed) {
            std::cout << result.id << " PASS";
            if (!result.message.empty()) {
                std::cout << " (" << result.message << ")";
            }
            std::cout << "\n";
        } else {
            std::cout << result.id << " FAIL: " << result.message << "\n";
            allPassed = false;
        }
    }
    return allPassed ? 0 : 1;
}

//======================================================================================================================
int runMisuseChild(const std::string& caseId, AdapterKind adapter) {
    return adapter == AdapterKind::Rhi ? runMisuseChildRhi(caseId) : runMisuseChildNoApi(caseId);
}

//======================================================================================================================
namespace {

// Spawns a fresh child process running this same binary with kMisuseEnvCase/kMisuseEnvAdapter set,
// captures its stderr, and returns {exitedViaSignal, signalNumber, capturedStderr}.
struct ChildOutcome {
    bool exitedNormally = false;
    int exitCode = 0;
    bool killedBySignal = false;
    int signalNumber = 0;
    std::string stderrText;
};

ChildOutcome spawnMisuseChild(const std::string& caseId, const std::string& adapterName) {
    ChildOutcome outcome;
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        outcome.stderrText = "pipe() failed";
        return outcome;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    // Core/Log.cpp's default logger is spdlog::stdout_color_mt: LMX_ASSERT's "ASSERT FAILED" line
    // (Source/Core/Assert.h's LMX_LOG_ERROR) lands on stdout, not stderr, so both are captured
    // here.
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    const std::string caseEnv = std::string(kMisuseEnvCase) + "=" + caseId;
    const std::string adapterEnv = std::string(kMisuseEnvAdapter) + "=" + adapterName;
    std::vector<std::string> envStorage;
    for (char** env = environ; *env != nullptr; ++env) {
        envStorage.emplace_back(*env);
    }
    envStorage.push_back(caseEnv);
    envStorage.push_back(adapterEnv);
    std::vector<char*> envp;
    envp.reserve(envStorage.size() + 1);
    for (std::string& entry : envStorage) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    std::array<char*, 2> argv{g_executablePath.data(), nullptr};

    pid_t pid = 0;
    const int spawnResult =
        posix_spawn(&pid, g_executablePath.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pipeFds[1]);
    if (spawnResult != 0) {
        close(pipeFds[0]);
        outcome.stderrText = "posix_spawn failed";
        return outcome;
    }

    std::string captured;
    char buffer[4096];
    ssize_t bytesRead = 0;
    while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
        captured.append(buffer, static_cast<size_t>(bytesRead));
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    outcome.stderrText = captured;
    if (WIFSIGNALED(status)) {
        outcome.killedBySignal = true;
        outcome.signalNumber = WTERMSIG(status);
    } else if (WIFEXITED(status)) {
        outcome.exitedNormally = true;
        outcome.exitCode = WEXITSTATUS(status);
    }
    return outcome;
}

} // namespace

//======================================================================================================================
int runMisuse(AdapterKind adapter, const std::string& caseId) {
    const std::string adapterName = adapter == AdapterKind::Rhi ? "rhi" : "noapi";
    std::vector<std::string> ids;
    if (caseId == "all") {
        for (const workload::MisuseCase& misuseCase : workload::misuseCases()) {
            ids.push_back(misuseCase.id);
        }
    } else {
        ids.push_back(caseId);
    }

    bool allPassed = true;
    for (const std::string& id : ids) {
        const ChildOutcome outcome = spawnMisuseChild(id, adapterName);
        const bool diedOnAbort = outcome.killedBySignal && (outcome.signalNumber == SIGABRT ||
                                                            outcome.signalNumber == SIGTRAP);
        const bool sawAssert = outcome.stderrText.find("ASSERT FAILED") != std::string::npos;
        if (diedOnAbort && sawAssert) {
            std::cout << id << " PASS: " << outcome.stderrText << "\n";
        } else {
            allPassed = false;
            std::cout << id << " FAIL: process did not die on an LMX_ASSERT ("
                      << (outcome.killedBySignal
                              ? ("signal " + std::to_string(outcome.signalNumber))
                              : (outcome.exitedNormally
                                     ? ("exit code " + std::to_string(outcome.exitCode))
                                     : "unknown"))
                      << "); captured stderr: " << outcome.stderrText << "\n";
        }
    }
    return allPassed ? 0 : 1;
}

} // namespace lmx::noapi::bench
