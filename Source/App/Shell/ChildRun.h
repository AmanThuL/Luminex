//----------------------------------------------------------------------------------------------------------------------
/// @file ChildRun.h
/// @brief Owns one headless App child and its exclusive session log.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace lmx::app {

/// A move-only headless App process. The owner always reaps it, including on cancellation.
class ChildRun {
public:
    /// Spawns this executable in the current working directory with an exclusive sibling log,
    /// no inherited descriptor beyond that log and an environment without LMX_ variables.
    /// Arguments include one screenshot or sequence output flag and its absolute destination.
    static std::expected<ChildRun, std::string> spawn(std::vector<std::string> argv);
    /// Polls without blocking; returns an exit code, or 128 plus the terminating signal.
    std::optional<int> poll();
    /// Terminates and reaps a still-running child; repeated calls are harmless.
    void kill();
    /// Terminates and reaps on scope exit.
    ~ChildRun();
    /// Transfers unique process ownership.
    ChildRun(ChildRun&& other) noexcept;
    /// Reaps the current child before taking unique ownership from another run.
    ChildRun& operator=(ChildRun&& other) noexcept;
    ChildRun(const ChildRun&) = delete;
    ChildRun& operator=(const ChildRun&) = delete;
    /// Process identifier, or -1 after reaping.
    pid_t pid() const { return m_pid; }

private:
    explicit ChildRun(pid_t pid) : m_pid(pid) {}
    pid_t m_pid = -1;
    std::optional<int> m_result;
};

} // namespace lmx::app
