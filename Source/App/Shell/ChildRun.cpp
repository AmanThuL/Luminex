//----------------------------------------------------------------------------------------------------------------------
/// @file ChildRun.cpp
/// @brief Spawns, polls, cancels and reaps headless App captures.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/ChildRun.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace lmx::app {
namespace {

//======================================================================================================================
std::optional<std::filesystem::path> logPath(const std::vector<std::string>& args) {
    for (size_t index = 0; index + 1 < args.size(); ++index) {
        if (args[index] != "--screenshot" && args[index] != "--capture-sequence")
            continue;
        const std::filesystem::path output(args[index + 1]);
        if (!output.is_absolute() || output.filename().empty())
            return std::nullopt;
        const auto name = args[index] == "--screenshot" ? output.stem() : output.filename();
        if (name.empty())
            return std::nullopt;
        return output.parent_path() / (name.string() + ".log");
    }
    return std::nullopt;
}

//======================================================================================================================
int exitCode(int status) {
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 127;
}

} // namespace

//======================================================================================================================
std::expected<ChildRun, std::string> ChildRun::spawn(std::vector<std::string> args) {
    const auto log = logPath(args);
    if (!log)
        return std::unexpected("A headless output path is required");
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable(size, '\0');
    if (_NSGetExecutablePath(executable.data(), &size) != 0)
        return std::unexpected("Cannot locate the App executable");
    executable.resize(std::strlen(executable.c_str()));

    posix_spawn_file_actions_t actions;
    int error = ::posix_spawn_file_actions_init(&actions);
    if (error != 0)
        return std::unexpected(std::format("Cannot prepare child log: {}", std::strerror(error)));
    error = ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log->c_str(),
                                               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (error == 0)
        error = ::posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    if (error != 0) {
        ::posix_spawn_file_actions_destroy(&actions);
        return std::unexpected(std::format("Cannot prepare child log: {}", std::strerror(error)));
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(executable.data());
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    error = ::posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (error != 0)
        return std::unexpected(std::format("Cannot start headless App: {}", std::strerror(error)));
    return ChildRun(pid);
}

//======================================================================================================================
std::optional<int> ChildRun::poll() {
    if (m_result)
        return m_result;
    if (m_pid < 0)
        return std::nullopt;
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(m_pid, &status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == 0)
        return std::nullopt;
    m_result = waited == m_pid ? exitCode(status) : 127;
    m_pid = -1;
    return m_result;
}

//======================================================================================================================
void ChildRun::kill() {
    if (m_pid < 0)
        return;
    ::kill(m_pid, SIGKILL);
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(m_pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    m_result = waited == m_pid ? exitCode(status) : 127;
    m_pid = -1;
}

//======================================================================================================================
ChildRun::~ChildRun() {
    kill();
}

//======================================================================================================================
ChildRun::ChildRun(ChildRun&& other) noexcept
    : m_pid(std::exchange(other.m_pid, -1)), m_result(std::move(other.m_result)) {}

//======================================================================================================================
ChildRun& ChildRun::operator=(ChildRun&& other) noexcept {
    if (this != &other) {
        kill();
        m_pid = std::exchange(other.m_pid, -1);
        m_result = std::move(other.m_result);
    }
    return *this;
}

} // namespace lmx::app
