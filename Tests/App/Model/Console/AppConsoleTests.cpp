#include "App/Model/Console/ConsoleModel.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Diagnostics/LogSink.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("console count and byte caps evict oldest messages with explicit counters",
          "[app][console]") {
    ConsoleLog log;
    SECTION("entry capacity") {
        for (size_t i = 0; i <= ConsoleLog::kMaxEntries; ++i) {
            log.append(lmx::log::Level::Info, static_cast<int64_t>(i), "small");
        }
        const auto snapshot = log.snapshot();
        REQUIRE(snapshot.entries.size() == ConsoleLog::kMaxEntries);
        CHECK(snapshot.entries.front().sequence == 2);
        CHECK(snapshot.evictedEntries == 1);
        CHECK(snapshot.payloadBytes == ConsoleLog::kMaxEntries * 5);
    }
    SECTION("payload capacity") {
        const std::string message(ConsoleLog::kMaxMessageBytes, 'x');
        for (int i = 0; i < 129; ++i)
            log.append(lmx::log::Level::Info, i, message);
        const auto snapshot = log.snapshot();
        CHECK(snapshot.entries.size() == 128);
        CHECK(snapshot.payloadBytes == ConsoleLog::kMaxPayloadBytes);
        CHECK(snapshot.evictedEntries == 1);
        CHECK(snapshot.truncatedMessages == 0);
        CHECK(snapshot.entries.front().sequence == 2);
    }
}

//======================================================================================================================
TEST_CASE("console truncation retains valid UTF-8 boundary and counts lost payloads",
          "[app][console]") {
    ConsoleLog log;
    const std::string prefix(ConsoleLog::kMaxMessageBytes - 1, 'x');
    log.append(lmx::log::Level::Warning, 123, prefix + "€tail");
    const auto snapshot = log.snapshot();
    REQUIRE(snapshot.entries.size() == 1);
    CHECK(snapshot.entries.front().message == prefix);
    CHECK(snapshot.entries.front().truncated);
    CHECK(snapshot.truncatedMessages == 1);
    CHECK(snapshot.payloadBytes == prefix.size());
    CHECK(consoleVisibleText(snapshot, {}).ends_with(" [truncated]\n"));
}

//======================================================================================================================
TEST_CASE("console freeze keeps its snapshot while producers continue and Clear empties both views",
          "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    log->append(lmx::log::Level::Info, 1, "startup");
    ConsoleModel model(log);
    model.setFrozen(true);
    const auto frozenRevision = model.snapshot().revision;
    log->append(lmx::log::Level::Error, 2, "later error");
    model.refresh();
    REQUIRE(model.snapshot().entries.size() == 1);
    CHECK(model.snapshot().revision == frozenRevision);
    CHECK(log->snapshot().entries.size() == 2);
    model.filter.search = "error";
    CHECK(consoleVisibleText(model.snapshot(), model.filter).empty());
    model.setFrozen(false);
    REQUIRE(model.snapshot().entries.size() == 2);
    CHECK(consoleVisibleText(model.snapshot(), model.filter).find("later error") !=
          std::string::npos);
    model.setFrozen(true);
    model.clear();
    CHECK(model.frozen());
    CHECK(model.snapshot().entries.empty());
    CHECK(log->snapshot().entries.empty());
    CHECK(model.snapshot().payloadBytes == 0);
    CHECK(model.snapshot().evictedEntries == 0);
    CHECK(model.snapshot().truncatedMessages == 0);
    log->append(lmx::log::Level::Info, 3, "after clear");
    model.refresh();
    CHECK(model.snapshot().entries.empty());
    model.setFrozen(false);
    REQUIRE(model.snapshot().entries.size() == 1);
    CHECK(model.snapshot().entries.front().sequence == 3);
    CHECK(model.filter.search == "error");
}

//======================================================================================================================
TEST_CASE("console clipboard includes exactly the displayed matching messages and multiline text",
          "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    log->append(lmx::log::Level::Info, 1, "Shader started");
    log->append(lmx::log::Level::Warning, 1234, "SHADER warning\nsecond line");
    log->append(lmx::log::Level::Error, 2000, "scene error");
    ConsoleModel model(log);
    model.filter = {lmx::log::Level::Warning, "shader"};
    CHECK(consoleVisibleText(model.snapshot(), model.filter) ==
          "[00:00:01.234 UTC] [Warning] SHADER warning\nsecond line\n");
    model.filter.search = "absent";
    CHECK(consoleVisibleText(model.snapshot(), model.filter).empty());
    CHECK(model.snapshot().entries.size() == 3);
}

//======================================================================================================================
TEST_CASE("console ingestion serializes concurrent producers and preserves bounded coherent copies",
          "[app][console]") {
    ConsoleLog log;
    std::vector<std::jthread> producers;
    for (int worker = 0; worker < 4; ++worker) {
        producers.emplace_back([&log, worker] {
            for (int i = 0; i < 600; ++i)
                log.append(lmx::log::Level::Info, worker, "thread message");
        });
    }
    producers.clear();
    const auto snapshot = log.snapshot();
    REQUIRE(snapshot.entries.size() == 2000);
    CHECK(snapshot.evictedEntries == 400);
    CHECK(snapshot.entries.front().sequence == 401);
    CHECK(snapshot.entries.back().sequence == 2400);
    size_t bytes = 0;
    for (const auto& entry : snapshot.entries)
        bytes += entry.message.size();
    CHECK(snapshot.payloadBytes == bytes);
    CHECK_FALSE(log.snapshotAfter(snapshot.revision).has_value());
    log.clear();
    CHECK(snapshot.entries.size() == 2000);
    REQUIRE(log.snapshotAfter(snapshot.revision).has_value());
}

//======================================================================================================================
TEST_CASE("scoped log observers preserve other observers and detach after startup capture",
          "[console][core]") {
    lmx::log::init();
    std::vector<std::string> first;
    std::vector<lmx::log::Level> second;
    lmx::log::SinkSubscription observer(
        [&](const lmx::log::Message& message) { first.emplace_back(message.text); });
    {
        lmx::log::SinkSubscription startup([&](const lmx::log::Message& message) {
            second.push_back(message.level);
            CHECK(message.timestampMilliseconds > 0);
        });
        LMX_LOG_WARN("console startup observer validation");
    }
    LMX_LOG_INFO("console observer remains registered");
    REQUIRE(first.size() == 2);
    CHECK(first[0] == "console startup observer validation");
    REQUIRE(second.size() == 1);
    CHECK(second[0] == lmx::log::Level::Warning);
}

//======================================================================================================================
TEST_CASE("console scrolling freezes the displayed history and returning to the end resumes",
          "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    log->append(lmx::log::Level::Info, 1, "shown");
    ConsoleModel model(log);
    model.setScrolledToEnd(false);
    CHECK(model.frozen());
    CHECK(model.newSinceFreeze() == 0);
    log->append(lmx::log::Level::Info, 2, "new");
    model.setScrolledToEnd(false);
    model.refresh();
    REQUIRE(model.snapshot().entries.size() == 1);
    CHECK(model.newSinceFreeze() == 1);
    model.setScrolledToEnd(true);
    CHECK_FALSE(model.frozen());
    CHECK(model.newSinceFreeze() == 0);
    CHECK(model.snapshot().entries.size() == 2);
}

//======================================================================================================================
TEST_CASE("console frozen arrivals survive eviction and resume by chip", "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    log->append(lmx::log::Level::Warning, 1, "keep this displayed");
    ConsoleModel model(log);
    model.setScrolledToEnd(false);
    for (size_t i = 0; i < ConsoleLog::kMaxEntries + 7; ++i)
        log->append(lmx::log::Level::Info, 2, "arrival");
    CHECK(model.newSinceFreeze() == ConsoleLog::kMaxEntries + 7);
    CHECK(consoleVisibleText(model.snapshot(), {}).find("keep this displayed") !=
          std::string::npos);
    CHECK(consoleVisibleText(model.snapshot(), {}).find("arrival") == std::string::npos);
    model.resumeAtEnd();
    CHECK_FALSE(model.frozen());
    CHECK(model.newSinceFreeze() == 0);
    CHECK(model.snapshot().entries.size() == ConsoleLog::kMaxEntries);
}

//======================================================================================================================
TEST_CASE("console Clear while frozen starts the arrival count at the clear boundary",
          "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    log->append(lmx::log::Level::Info, 1, "before freeze");
    ConsoleModel model(log);
    model.filter = {lmx::log::Level::Warning, "wanted"};
    model.setScrolledToEnd(false);
    log->append(lmx::log::Level::Info, 2, "before clear");
    CHECK(model.newSinceFreeze() == 1);
    model.clear();
    CHECK(model.frozen());
    CHECK(model.newSinceFreeze() == 0);
    CHECK(model.snapshot().entries.empty());
    CHECK(model.filter.minimumSeverity == lmx::log::Level::Warning);
    CHECK(model.filter.search == "wanted");
    log->append(lmx::log::Level::Warning, 3, "wanted after clear");
    log->append(lmx::log::Level::Info, 4, "filtered after clear");
    model.refresh();
    CHECK(model.newSinceFreeze() == 2);
    CHECK(consoleVisibleText(model.snapshot(), model.filter).empty());
    model.resumeAtEnd();
    CHECK(model.newSinceFreeze() == 0);
    CHECK(consoleVisibleText(model.snapshot(), model.filter) ==
          "[00:00:00.003 UTC] [Warning] wanted after clear\n");
}

//======================================================================================================================
TEST_CASE("console sequence cursors are coherent with snapshots and never reset at Clear",
          "[app][console]") {
    ConsoleLog log;
    CHECK(log.nextSequence() == 1);
    log.append(lmx::log::Level::Info, 1, "first");
    const auto shown = log.snapshot();
    CHECK(shown.nextSequence == 2);
    log.append(lmx::log::Level::Info, 2, "second");
    CHECK(shown.nextSequence == 2);
    CHECK(log.nextSequence() == 3);
    const auto cleared = log.clear();
    log.append(lmx::log::Level::Info, 3, "after clear");
    CHECK(cleared.nextSequence == 3);
    CHECK(log.nextSequence() - cleared.nextSequence == 1);
}

//======================================================================================================================
TEST_CASE("console severity counts describe each level of the displayed snapshot",
          "[app][console]") {
    auto log = std::make_shared<ConsoleLog>();
    CHECK(consoleSeverityCounts(log->snapshot()) == ConsoleSeverityCounts{});
    for (int level = 0; level != 6; ++level)
        for (int n = 0; n <= level; ++n)
            log->append(static_cast<lmx::log::Level>(level), n, "message");
    ConsoleModel model(log);
    const ConsoleSeverityCounts expected{1, 2, 3, 4, 5, 6};
    CHECK(consoleSeverityCounts(model.snapshot()) == expected);
    model.setScrolledToEnd(false);
    log->append(lmx::log::Level::Error, 0, "hidden arrival");
    model.filter = {lmx::log::Level::Critical, "absent"};
    CHECK(consoleSeverityCounts(model.snapshot()) == expected);
    model.resumeAtEnd();
    CHECK(consoleSeverityCounts(model.snapshot())[4] == 6);
}
