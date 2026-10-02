//----------------------------------------------------------------------------------------------------------------------
/// @file SessionListener.cpp
/// @brief Runs the authenticated, nonblocking local session transport.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionListener.h"

#include "App/Model/Session/SessionProtocol.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace lmx::app {
namespace {

//======================================================================================================================
void nonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

//======================================================================================================================
void noSigpipe(int fd) {
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
}

//======================================================================================================================
std::string ioError(std::string_view action) {
    return std::format("Session socket {}: {}", action, std::strerror(errno));
}

//======================================================================================================================
void removeOwnedSocket(const std::filesystem::path& path, uint64_t device, uint64_t inode) {
    struct stat current{};
    if (lstat(path.c_str(), &current) == 0 && static_cast<uint64_t>(current.st_dev) == device &&
        static_cast<uint64_t>(current.st_ino) == inode && S_ISSOCK(current.st_mode))
        unlink(path.c_str());
}

//======================================================================================================================
// A socket file outlives a crashed editor. It is stale only when this user owns it and nothing
// accepts on it; any other entry, or any other connect result, stays untouched.
bool staleSocket(const sockaddr_un& address, const struct stat& entry) {
    if (!S_ISSOCK(entry.st_mode) || entry.st_uid != geteuid())
        return false;
    const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0)
        return false;
    nonblocking(probe);
    const bool refused =
        connect(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 &&
        errno == ECONNREFUSED;
    close(probe);
    return refused;
}

std::atomic_uint64_t nextConnection = 0;

} // namespace

//======================================================================================================================
std::filesystem::path defaultSessionSocket() {
    const char* temporary = std::getenv("TMPDIR");
    return std::filesystem::path(temporary && *temporary ? temporary : "/tmp") /
           std::format("luminex-session-{}.sock", getpid());
}

//======================================================================================================================
SessionListener::SessionListener(std::filesystem::path socket,
                                 std::shared_ptr<SessionMailbox> mailbox, int listenFd, int readFd,
                                 int writeFd, uint64_t device, uint64_t inode)
    : m_path(std::move(socket)), m_mailbox(std::move(mailbox)), m_listenFd(listenFd),
      m_readFd(readFd), m_writeFd(writeFd), m_device(device), m_inode(inode) {}

//======================================================================================================================
std::expected<std::unique_ptr<SessionListener>, std::string>
SessionListener::start(std::filesystem::path socket, std::shared_ptr<SessionMailbox> mailbox) {
    if (!mailbox)
        return std::unexpected("Session mailbox is required");
    const auto name = socket.string();
    sockaddr_un address{};
    if (name.empty() || name.size() >= sizeof(address.sun_path))
        return std::unexpected("Session socket path exceeds sun_path capacity");
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
    struct stat prior{};
    if (lstat(name.c_str(), &prior) == 0) {
        if (!staleSocket(address, prior))
            return std::unexpected("Session socket path already exists");
        removeOwnedSocket(socket, prior.st_dev, prior.st_ino);
        if (lstat(name.c_str(), &prior) == 0)
            return std::unexpected("Session socket path already exists");
    }
    if (errno != ENOENT)
        return std::unexpected(ioError("path check failed"));
    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0)
        return std::unexpected(ioError("creation failed"));
    nonblocking(listenFd);
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        auto error = ioError("bind failed");
        close(listenFd);
        return std::unexpected(std::move(error));
    }
    struct stat bound{};
    if (lstat(name.c_str(), &bound) != 0 || !S_ISSOCK(bound.st_mode) ||
        chmod(name.c_str(), 0600) != 0 || listen(listenFd, 4) != 0) {
        auto error = ioError("setup failed");
        if (bound.st_ino != 0)
            removeOwnedSocket(socket, bound.st_dev, bound.st_ino);
        close(listenFd);
        return std::unexpected(std::move(error));
    }
    int wakeFds[2];
    if (pipe(wakeFds) != 0) {
        auto error = ioError("wake pipe failed");
        removeOwnedSocket(socket, bound.st_dev, bound.st_ino);
        close(listenFd);
        return std::unexpected(std::move(error));
    }
    nonblocking(wakeFds[0]);
    nonblocking(wakeFds[1]);
    auto listener = std::unique_ptr<SessionListener>(
        new SessionListener(std::move(socket), std::move(mailbox), listenFd, wakeFds[0], wakeFds[1],
                            bound.st_dev, bound.st_ino));
    listener->m_thread = std::thread([self = listener.get()] { self->run(); });
    return listener;
}

//======================================================================================================================
void SessionListener::wake() {
    const char byte = 1;
    const auto ignored = write(m_writeFd, &byte, 1);
    (void)ignored;
}

//======================================================================================================================
const std::filesystem::path& SessionListener::path() const {
    return m_path;
}

//======================================================================================================================
SessionListener::~SessionListener() {
    m_stopping.store(true);
    wake();
    if (m_thread.joinable())
        m_thread.join();
    close(m_readFd);
    close(m_writeFd);
    close(m_listenFd);
    removeOwnedSocket(m_path, m_device, m_inode);
}

//======================================================================================================================
void SessionListener::run() {
    int client = -1;
    uint64_t connection = 0;
    std::string input;
    std::deque<std::string> output;
    size_t outputOffset = 0;
    size_t outputBytes = 0;
    bool closeAfterWrite = false;
    constexpr size_t kOutputCapacity = 4 * kMaxLineBytes;
    const auto disconnect = [&] {
        if (client < 0)
            return;
        close(client);
        client = -1;
        if (!m_mailbox->pushInbound({connection, {}, false, true}))
            m_mailbox->retainClose(connection);
        input.clear();
        output.clear();
        outputOffset = 0;
        outputBytes = 0;
        closeAfterWrite = false;
    };
    const auto queueError = [&](SessionError code, std::string_view message, bool finish) {
        auto error = encodeError(0, code, message);
        if (outputBytes + error.size() > kOutputCapacity)
            return false;
        outputBytes += error.size();
        output.push_back(std::move(error));
        closeAfterWrite |= finish;
        return true;
    };
    while (!m_stopping.load()) {
        for (auto& [target, line] : m_mailbox->takeOutbound()) {
            if (client >= 0 && target == connection && !closeAfterWrite) {
                if (line.size() <= kMaxLineBytes && outputBytes + line.size() <= kOutputCapacity) {
                    outputBytes += line.size();
                    output.push_back(std::move(line));
                } else {
                    disconnect();
                }
            }
        }
        for (const auto target : m_mailbox->takeOutboundClosures()) {
            if (client >= 0 && (target == 0 || target == connection))
                disconnect();
        }
        const bool mayAccept = client >= 0 || m_mailbox->hasInboundCapacity();
        std::array<pollfd, 3> fds{
            {{m_listenFd, static_cast<short>(mayAccept ? POLLIN : 0), 0},
             {m_readFd, POLLIN, 0},
             {client,
              static_cast<short>((closeAfterWrite ? 0 : POLLIN) | (output.empty() ? 0 : POLLOUT)),
              0}}};
        const int count = poll(fds.data(), client < 0 ? 2 : 3, mayAccept ? -1 : 20);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (m_stopping.load())
            break;
        if (fds[1].revents & POLLIN) {
            std::array<char, 128> drained{};
            const auto ignored = read(m_readFd, drained.data(), drained.size());
            (void)ignored;
        }
        if (fds[0].revents & POLLIN) {
            for (int accepted = 0; accepted < 8 && !m_stopping.load(); ++accepted) {
                int arriving = accept(m_listenFd, nullptr, nullptr);
                if (arriving < 0)
                    break;
                // Close-on-exec comes first so no child started meanwhile inherits the client.
                fcntl(arriving, F_SETFD, FD_CLOEXEC);
                nonblocking(arriving);
                noSigpipe(arriving);
                uid_t peer = 0;
                gid_t group = 0;
                if (getpeereid(arriving, &peer, &group) != 0 || peer != geteuid()) {
                    close(arriving);
                    continue;
                }
                if (client >= 0) {
                    const auto busy =
                        encodeError(0, SessionError::Busy, "A session client is connected");
                    const auto ignored = send(arriving, busy.data(), busy.size(), 0);
                    (void)ignored;
                    close(arriving);
                    continue;
                }
                client = arriving;
                connection = nextConnection.fetch_add(1) + 1;
                if (!m_mailbox->pushInbound({connection, {}, true, false}))
                    disconnect();
            }
        }
        if (client < 0)
            continue;
        if (fds[2].revents & POLLIN && !closeAfterWrite) {
            std::array<char, 8192> bytes{};
            for (int received = 0; received < 8 && !m_stopping.load(); ++received) {
                const auto size = recv(client, bytes.data(), bytes.size(), 0);
                if (size == 0) {
                    disconnect();
                    break;
                }
                if (size < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        disconnect();
                    break;
                }
                for (ssize_t i = 0; i < size; ++i) {
                    if (bytes[i] == '\n') {
                        if (!m_mailbox->pushInbound({connection, std::move(input), false, false}) &&
                            !queueError(SessionError::Busy, "Session inbox is full", true))
                            disconnect();
                        input.clear();
                    } else if (input.size() == kMaxLineBytes) {
                        if (!queueError(SessionError::Protocol, "Session line exceeds 1 MiB", true))
                            disconnect();
                        input.clear();
                        break;
                    } else {
                        input.push_back(bytes[i]);
                    }
                    if (client < 0 || closeAfterWrite)
                        break;
                }
                if (closeAfterWrite || client < 0)
                    break;
            }
        }
        if (client >= 0 && !output.empty() && (fds[2].revents & POLLOUT)) {
            for (int sentChunks = 0; sentChunks < 8 && !output.empty() && !m_stopping.load();
                 ++sentChunks) {
                auto& line = output.front();
                const auto sent =
                    send(client, line.data() + outputOffset, line.size() - outputOffset, 0);
                if (sent <= 0) {
                    if (sent == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                        disconnect();
                    break;
                }
                outputOffset += static_cast<size_t>(sent);
                outputBytes -= static_cast<size_t>(sent);
                if (outputOffset == line.size()) {
                    output.pop_front();
                    outputOffset = 0;
                }
            }
        }
        if (client >= 0 && closeAfterWrite && output.empty())
            disconnect();
        if (client >= 0 && (fds[2].revents & (POLLERR | POLLHUP | POLLNVAL)))
            disconnect();
    }
    disconnect();
}

} // namespace lmx::app
