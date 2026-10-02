//----------------------------------------------------------------------------------------------------------------------
/// @file SessionListener.h
/// @brief Declares the local single-client session socket listener.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionMailbox.h"

#include <atomic>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace lmx::app {

/// Returns the process-specific socket path under TMPDIR (or /tmp).
std::filesystem::path defaultSessionSocket();

/// Owns a Unix socket, one client, and a poll-based transport thread.
class SessionListener {
public:
    /// Binds a new socket without replacing any existing filesystem entry.
    static std::expected<std::unique_ptr<SessionListener>, std::string>
    start(std::filesystem::path socket, std::shared_ptr<SessionMailbox> mailbox);
    /// Interrupts poll after the editor queues outbound responses.
    void wake();
    /// Returns the bound socket pathname.
    const std::filesystem::path& path() const;
    /// Stops the thread, closes descriptors, and removes only this listener's socket.
    ~SessionListener();

    /// Listener ownership cannot be copied.
    SessionListener(const SessionListener&) = delete;
    /// Listener ownership cannot be assigned.
    SessionListener& operator=(const SessionListener&) = delete;

private:
    SessionListener(std::filesystem::path socket, std::shared_ptr<SessionMailbox> mailbox,
                    int listenFd, int readFd, int writeFd, uint64_t device, uint64_t inode);
    void run();

    std::filesystem::path m_path;
    std::shared_ptr<SessionMailbox> m_mailbox;
    int m_listenFd;
    int m_readFd;
    int m_writeFd;
    uint64_t m_device;
    uint64_t m_inode;
    std::atomic_bool m_stopping = false;
    std::thread m_thread;
};

} // namespace lmx::app
