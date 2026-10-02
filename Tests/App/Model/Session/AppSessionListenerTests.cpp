#include "App/Model/Session/SessionListener.h"
#include "App/Model/Session/SessionProtocol.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace lmx::app;
using namespace std::chrono_literals;

namespace {

struct SocketFixture {
    std::filesystem::path directory;
    SocketFixture();
    ~SocketFixture();
    std::filesystem::path path() const;
};

//======================================================================================================================
SocketFixture::SocketFixture() {
    std::array<char, 80> pattern{};
    const auto prefix = std::string("/tmp/lmx-session-test-XXXXXX");
    std::copy(prefix.begin(), prefix.end(), pattern.begin());
    directory = mkdtemp(pattern.data());
}

//======================================================================================================================
SocketFixture::~SocketFixture() {
    std::filesystem::remove_all(directory);
}

//======================================================================================================================
std::filesystem::path SocketFixture::path() const {
    return directory / "session.sock";
}

//======================================================================================================================
int connectTo(const std::filesystem::path& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return fd;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto name = path.string();
    std::copy(name.begin(), name.end(), address.sun_path);
    address.sun_path[name.size()] = '\0';
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    return fd;
}

//======================================================================================================================
std::vector<InboundLine> awaitInbound(const std::shared_ptr<SessionMailbox>& mailbox) {
    for (int i = 0; i < 200; ++i) {
        auto lines = mailbox->takeInbound();
        if (!lines.empty())
            return lines;
        std::this_thread::sleep_for(5ms);
    }
    return {};
}

//======================================================================================================================
std::string readLine(int fd) {
    std::string line;
    char byte;
    while (recv(fd, &byte, 1, 0) == 1) {
        line.push_back(byte);
        if (byte == '\n')
            break;
    }
    return line;
}

//======================================================================================================================
bool sendAll(int fd, const std::string& value) {
    for (size_t offset = 0; offset < value.size();) {
        const auto sent = send(fd, value.data() + offset, value.size() - offset, 0);
        if (sent <= 0)
            return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

//======================================================================================================================
size_t openDescriptors() {
    size_t count = 0;
    for (int fd = 0; fd < getdtablesize(); ++fd) {
        if (fcntl(fd, F_GETFD) != -1)
            ++count;
    }
    return count;
}

} // namespace

//======================================================================================================================
TEST_CASE("session listener exchanges framed lines and reports connection lifecycle",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].opened);
    const auto connection = opened[0].connection;
    const std::string request = "hello\n";
    REQUIRE(send(client, request.data(), request.size(), 0) == request.size());
    auto incoming = awaitInbound(mailbox);
    REQUIRE(incoming.size() == 1);
    CHECK(incoming[0].text == "hello");
    CHECK(incoming[0].connection == connection);
    const auto response = encodeError(1, SessionError::Busy, "busy");
    mailbox->pushOutbound(connection, response);
    (*listener)->wake();
    CHECK(readLine(client) == response);
    close(client);
    auto closed = awaitInbound(mailbox);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0].closed);
    CHECK(closed[0].connection == connection);
}

//======================================================================================================================
TEST_CASE("session listener limits clients and lines, then accepts another client",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int first = connectTo(fixture.path());
    REQUIRE(first >= 0);
    REQUIRE(awaitInbound(mailbox).size() == 1);
    int second = connectTo(fixture.path());
    REQUIRE(second >= 0);
    CHECK(readLine(second).find("\"busy\"") != std::string::npos);
    close(second);
    const std::string huge(2 << 20, 'x');
    for (size_t offset = 0; offset < huge.size(); offset += 16384) {
        if (send(first, huge.data() + offset, std::min<size_t>(16384, huge.size() - offset), 0) <=
            0)
            break;
    }
    CHECK(readLine(first).find("\"protocol\"") != std::string::npos);
    close(first);
    auto closed = awaitInbound(mailbox);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0].closed);
    int next = connectTo(fixture.path());
    REQUIRE(next >= 0);
    CHECK(awaitInbound(mailbox)[0].opened);
    close(next);
}

//======================================================================================================================
TEST_CASE("session mailbox enforces 64 total inbound entries", "[app][session-listener]") {
    SessionMailbox mailbox;
    for (int i = 0; i < 64; ++i)
        REQUIRE(mailbox.pushInbound({1, "line", false, false}));
    CHECK_FALSE(mailbox.pushInbound({1, "overflow", false, false}));
    CHECK_FALSE(mailbox.pushInbound({1, {}, false, true}));
    CHECK(mailbox.retainClose(1));
    CHECK_FALSE(mailbox.hasInboundCapacity());
    auto lines = mailbox.takeInbound();
    REQUIRE(lines.size() == 64);
    CHECK(mailbox.hasInboundCapacity());
    CHECK(mailbox.takeInbound()[0].closed);
}

//======================================================================================================================
TEST_CASE("session close survives a drain between failed push and retention",
          "[app][session-listener]") {
    SessionMailbox mailbox;
    for (int i = 0; i < 64; ++i)
        REQUIRE(mailbox.pushInbound({7, "held", false, false}));
    CHECK_FALSE(mailbox.pushInbound({7, {}, false, true}));
    REQUIRE(mailbox.takeInbound().size() == 64);
    REQUIRE(mailbox.retainClose(7));
    const auto closing = mailbox.takeInbound();
    REQUIRE(closing.size() == 1);
    CHECK(closing[0].closed);
    CHECK(closing[0].connection == 7);
}

//======================================================================================================================
TEST_CASE("session listener accepts exactly one MiB and answers a full inbox",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    const auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    const auto connection = opened[0].connection;
    REQUIRE(sendAll(client, std::string(kMaxLineBytes, 'a') + '\n'));
    auto accepted = awaitInbound(mailbox);
    REQUIRE(accepted.size() == 1);
    CHECK(accepted[0].text.size() == kMaxLineBytes);
    for (int i = 0; i < 64; ++i)
        REQUIRE(mailbox->pushInbound({connection, "held", false, false}));
    REQUIRE(sendAll(client, "overflow\n"));
    CHECK(readLine(client).find("\"busy\"") != std::string::npos);
    close(client);
    auto held = mailbox->takeInbound();
    CHECK(held.size() >= 64);
    if (held.back().closed)
        CHECK(held.back().connection == connection);
    else {
        auto tail = awaitInbound(mailbox);
        REQUIRE(tail.size() == 1);
        CHECK(tail[0].closed);
    }
    int next = connectTo(fixture.path());
    REQUIRE(next >= 0);
    const auto reopened = awaitInbound(mailbox);
    REQUIRE(reopened.size() == 1);
    CHECK(reopened[0].opened);
    CHECK(reopened[0].connection != connection);
    close(next);
}

//======================================================================================================================
TEST_CASE("session listener refuses existing paths and preserves their contents",
          "[app][session-listener]") {
    SocketFixture fixture;
    {
        std::ofstream file(fixture.path());
        file << "owner";
    }
    auto mailbox = std::make_shared<SessionMailbox>();
    CHECK_FALSE(SessionListener::start(fixture.path(), mailbox));
    std::ifstream file(fixture.path());
    std::string contents;
    file >> contents;
    CHECK(contents == "owner");
    CHECK_FALSE(SessionListener::start(fixture.directory / std::string(200, 'x'), mailbox));
    const auto alias = fixture.directory / "alias.sock";
    std::filesystem::create_symlink(fixture.path(), alias);
    CHECK_FALSE(SessionListener::start(alias, mailbox));
    CHECK(std::filesystem::is_symlink(alias));
    const auto existing = fixture.directory / "existing.sock";
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto name = existing.string();
    std::copy(name.begin(), name.end(), address.sun_path);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    CHECK_FALSE(SessionListener::start(existing, mailbox));
    CHECK(std::filesystem::exists(existing));
    close(fd);
}

//======================================================================================================================
TEST_CASE("session listener cleans up 100 sockets without descriptor growth",
          "[app][session-listener]") {
    SocketFixture fixture;
    const auto before = openDescriptors();
    for (int i = 0; i < 100; ++i) {
        auto mailbox = std::make_shared<SessionMailbox>();
        auto listener = SessionListener::start(fixture.path(), mailbox);
        REQUIRE(listener);
        int client = connectTo(fixture.path());
        REQUIRE(client >= 0);
        listener->reset();
        close(client);
        CHECK_FALSE(std::filesystem::exists(fixture.path()));
    }
    CHECK(openDescriptors() == before);
}

//======================================================================================================================
TEST_CASE("session outbound overflow closes the client and restart IDs stay distinct",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    uint64_t previous = 0;
    {
        auto listener = SessionListener::start(fixture.path(), mailbox);
        REQUIRE(listener);
        int client = connectTo(fixture.path());
        REQUIRE(client >= 0);
        const auto opened = awaitInbound(mailbox);
        REQUIRE(opened.size() == 1);
        previous = opened[0].connection;
        mailbox->pushOutbound(previous, std::string(kMaxLineBytes + 1, 'x'));
        (*listener)->wake();
        char byte = 0;
        CHECK(recv(client, &byte, 1, 0) == 0);
        close(client);
        listener->reset();
        mailbox->takeInbound();
    }
    mailbox->pushOutbound(previous, encodeError(5, SessionError::Busy, "old"));
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    const auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].connection != previous);
    const auto fresh = encodeResult(6, "{}");
    mailbox->pushOutbound(opened[0].connection, fresh);
    (*listener)->wake();
    CHECK(readLine(client) == fresh);
    close(client);
}

//======================================================================================================================
TEST_CASE("session listener stops promptly during a full-inbox client flood",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    const auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    for (int i = 0; i < 64; ++i)
        REQUIRE(mailbox->pushInbound({opened[0].connection, "held", false, false}));
    sendAll(client, std::string(200000, '\n'));
    const auto start = std::chrono::steady_clock::now();
    listener->reset();
    CHECK(std::chrono::steady_clock::now() - start < 1s);
    CHECK_FALSE(std::filesystem::exists(fixture.path()));
    close(client);
}

//======================================================================================================================
TEST_CASE("session listener leaves a replacement pathname untouched", "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    REQUIRE(unlink(fixture.path().c_str()) == 0);
    {
        std::ofstream replacement(fixture.path());
        replacement << "replacement";
    }
    listener->reset();
    std::ifstream file(fixture.path());
    std::string contents;
    file >> contents;
    CHECK(contents == "replacement");
}

//======================================================================================================================
TEST_CASE("session stop retains a full-inbox close before a later listener opens",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    const auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    const auto oldConnection = opened[0].connection;
    for (int i = 0; i < 64; ++i)
        REQUIRE(mailbox->pushInbound({oldConnection, "held", false, false}));
    listener->reset();
    close(client);
    auto held = mailbox->takeInbound();
    REQUIRE(held.size() == 64);
    auto closing = mailbox->takeInbound();
    REQUIRE(closing.size() == 1);
    CHECK(closing[0].closed);
    CHECK(closing[0].connection == oldConnection);
    auto replacement = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(replacement);
    int next = connectTo(fixture.path());
    REQUIRE(next >= 0);
    const auto reopened = awaitInbound(mailbox);
    REQUIRE(reopened.size() == 1);
    CHECK(reopened[0].opened);
    CHECK(reopened[0].connection != oldConnection);
    close(next);
}

//======================================================================================================================
TEST_CASE("session outbound closure saturation still terminates the active peer",
          "[app][session-listener]") {
    SocketFixture fixture;
    auto mailbox = std::make_shared<SessionMailbox>();
    auto listener = SessionListener::start(fixture.path(), mailbox);
    REQUIRE(listener);
    int client = connectTo(fixture.path());
    REQUIRE(client >= 0);
    const auto opened = awaitInbound(mailbox);
    REQUIRE(opened.size() == 1);
    const std::string oversized(kMaxLineBytes + 1, 'x');
    for (uint64_t i = 0; i < 64; ++i)
        mailbox->pushOutbound(std::numeric_limits<uint64_t>::max() - i, oversized);
    mailbox->pushOutbound(opened[0].connection, oversized);
    (*listener)->wake();
    char byte = 0;
    CHECK(recv(client, &byte, 1, 0) == 0);
    close(client);
}
