#include "App/Model/Session/DocumentWatch.h"

#include "Engine/Asset/Document/SceneDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("document watch waits for a stable pair and matching sidecar", "[unit][session]") {
    DocumentWatch watch;
    const FileStamp loaded{100, 200, 1, 1, false};
    const FileStamp gltf{101, 200, 2, 1, false};
    const FileStamp pair{101, 201, 2, 3, false};
    watch.reset(loaded);
    CHECK(watch.due(0.0));
    CHECK(watch.poll(loaded, 0.0) == WatchDecision::Wait);
    CHECK_FALSE(watch.due(0.49));
    CHECK(watch.due(0.5));
    CHECK(watch.poll(gltf, 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(pair, 1.0) == WatchDecision::Wait);
    CHECK(watch.poll(pair, 1.5) == WatchDecision::Hash);
    watch.hashed("changed", false, 1.5);
    CHECK_FALSE(watch.ready());
    CHECK(watch.poll(pair, 2.0) == WatchDecision::Sidecar);
    watch.hashed("changed", true, 2.0);
    CHECK(watch.ready());
    CHECK(watch.poll(pair, 2.5) == WatchDecision::Wait);
}

//======================================================================================================================
TEST_CASE("document watch defers saves, restarts changes, and times out absent sidecars",
          "[unit][session]") {
    DocumentWatch watch;
    const FileStamp loaded{100, 200, 1, 1, false};
    const FileStamp saving{101, 201, 2, 2, true};
    const FileStamp first{101, 201, 2, 2, false};
    const FileStamp second{102, 202, 3, 3, false};
    watch.reset(loaded);
    CHECK(watch.poll(saving, 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(saving, 1.0) == WatchDecision::Wait);
    CHECK(watch.poll(first, 1.5) == WatchDecision::Wait);
    CHECK(watch.poll(first, 2.0) == WatchDecision::Hash);
    watch.hashed("first", false, 2.0);
    CHECK(watch.poll(second, 2.5) == WatchDecision::Wait);
    CHECK(watch.poll(second, 3.0) == WatchDecision::Hash);
    watch.hashed("second", false, 3.0);
    CHECK(watch.poll(second, 6.5) == WatchDecision::Sidecar);
    CHECK_FALSE(watch.ready());
    CHECK(watch.poll(second, 7.0) == WatchDecision::Sidecar);
    CHECK(watch.ready());
    watch.reset(second);
    CHECK(watch.poll(second, 8.0) == WatchDecision::Wait);
}

//======================================================================================================================
TEST_CASE("candidate glTF chooses its own buffer while sidecar attribution waits",
          "[unit][session]") {
    const std::filesystem::path candidate = "SceneDocuments/candidate.scene.gltf";
    const auto buffer =
        lmx::asset::sceneDocumentBufferPath(R"({"buffers":[{"uri":"new-pair.bin"}]})", candidate);
    REQUIRE(buffer);
    REQUIRE(*buffer);
    CHECK(**buffer == std::filesystem::path("SceneDocuments/new-pair.bin"));
    CHECK_FALSE(lmx::asset::sceneDocumentBufferPath(R"({"buffers":[{"uri":42}]})", candidate));
    CHECK_FALSE(lmx::asset::sceneDocumentBufferPath("[]", candidate));
    CHECK_FALSE(lmx::asset::sceneDocumentBufferPath(R"({"buffers":[42]})", candidate));
    DocumentWatch watch;
    watch.reset(FileStamp{100, 200, 1, 1, false});
    const FileStamp newGltfMissingBuffer{101, 0, 2, 0, false};
    const FileStamp completeNewPair{101, 350, 2, 3, false};
    CHECK(watch.poll(newGltfMissingBuffer, 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(newGltfMissingBuffer, 1.0) == WatchDecision::Hash);
    watch.hashed("", false, 1.0);
    CHECK(watch.poll(completeNewPair, 1.5) == WatchDecision::Wait);
    CHECK(watch.poll(completeNewPair, 2.0) == WatchDecision::Hash);
    watch.hashed("complete-hash", false, 2.0);
    CHECK(watch.poll(completeNewPair, 2.5) == WatchDecision::Sidecar);
    watch.hashed("complete-hash", true, 2.5);
    CHECK(watch.ready());
}

//======================================================================================================================
TEST_CASE("document watch observes encoded buffer-only and late pair changes", "[unit][session]") {
    namespace fs = std::filesystem;
    const fs::path directory = "SessionWatchEncoded";
    fs::create_directories(directory);
    const fs::path document = directory / "candidate.scene.gltf";
    const auto write = [](const fs::path& path, std::string_view bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << bytes;
        REQUIRE(output.good());
    };
    const auto stamp = [&](int64_t gltfTime, int64_t bufferTime) {
        std::ifstream input(document, std::ios::binary);
        const std::string json(std::istreambuf_iterator<char>{input}, {});
        const auto buffer = lmx::asset::sceneDocumentBufferPath(json, document);
        REQUIRE(buffer);
        REQUIRE(*buffer);
        std::error_code error;
        const uintmax_t size = fs::file_size(**buffer, error);
        return FileStamp{fs::file_size(document), error ? 0 : size, gltfTime,
                         error ? 0 : bufferTime, false};
    };

    write(document, R"({"buffers":[{"uri":"space%20name.bin","byteLength":2}]})");
    write(directory / "space name.bin", "ab");
    const auto loaded = stamp(1, 1);
    write(directory / "space name.bin", "abc");
    const auto bufferOnly = stamp(1, 2);
    CHECK(bufferOnly.gltfSize == loaded.gltfSize);
    CHECK(bufferOnly.bufferSize == 3);
    DocumentWatch watch;
    watch.reset(loaded);
    CHECK(watch.poll(bufferOnly, 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(bufferOnly, 1.0) == WatchDecision::Hash);

    write(document, R"({"buffers":[{"uri":"renamed%25.bin","byteLength":2}]})");
    const auto missing = stamp(2, 0);
    CHECK(missing.bufferSize == 0);
    watch.hashed("", false, 1.0);
    CHECK(watch.poll(missing, 1.5) == WatchDecision::Wait);
    write(directory / "renamed%.bin", "xy");
    const auto arrived = stamp(2, 3);
    CHECK(arrived.bufferSize == 2);
    CHECK(watch.poll(arrived, 2.0) == WatchDecision::Wait);
    CHECK(watch.poll(arrived, 2.5) == WatchDecision::Hash);
    fs::remove_all(directory);
}

//======================================================================================================================
TEST_CASE("save hash observations cannot adopt a different stamp or an active writer",
          "[unit][session][save-watch]") {
    DocumentWatch watch;
    const FileStamp loaded{100, 200, 1, 1, false};
    const FileStamp written{101, 201, 2, 2, false};
    FileStamp afterHash = written;
    SECTION("external revision completes while the editor hashes") {
        afterHash.gltfTime = 3;
    }
    SECTION("another writer starts before the hash completes") {
        afterHash.saving = true;
    }
    watch.reset(loaded);
    REQUIRE(watch.poll(written, 0.5) == WatchDecision::Wait);
    REQUIRE(watch.poll(written, 1.0) == WatchDecision::Hash);
    watch.hashed("external", false, 1.0);
    CHECK_FALSE(watch.adoptSave(written, afterHash, "loaded.scene.gltf", "loaded.scene.gltf",
                                "editor", "editor"));
    CHECK(watch.poll(written, 1.5) == WatchDecision::Sidecar);
    CHECK(watch.poll(written, 5.0) == WatchDecision::Sidecar);
    CHECK(watch.ready());
}

//======================================================================================================================
TEST_CASE("a leftover staging directory defers the watch for ten polls and warns once",
          "[unit][session]") {
    DocumentWatch watch;
    const FileStamp loaded{100, 200, 1, 1, false};
    const FileStamp leftover{101, 201, 2, 2, true};
    watch.reset(loaded);
    double seconds = 0.0;
    for (int poll = 0; poll < DocumentWatch::kStagingPatiencePolls; ++poll) {
        CHECK(watch.poll(leftover, seconds += 0.5) == WatchDecision::Wait);
        CHECK_FALSE(watch.takeStagingWarning());
    }
    // Five seconds after the first sighting the changed pair is watched like any other.
    CHECK(watch.poll(leftover, seconds += 0.5) == WatchDecision::Wait);
    CHECK(watch.takeStagingWarning());
    CHECK_FALSE(watch.takeStagingWarning());
    CHECK(watch.poll(leftover, seconds += 0.5) == WatchDecision::Hash);
    CHECK_FALSE(watch.takeStagingWarning());
    watch.hashed("external", true, seconds);
    CHECK(watch.ready());

    // Adopting a pair keeps the count: the same leftover delays nothing a second time.
    watch.reset(leftover);
    const FileStamp next{102, 202, 3, 3, true};
    CHECK(watch.poll(leftover, seconds += 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(next, seconds += 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(next, seconds += 0.5) == WatchDecision::Hash);
    CHECK_FALSE(watch.takeStagingWarning());

    // Once the directory is gone a new one is waited for, and warned about, again.
    FileStamp cleared = next;
    cleared.saving = false;
    watch.reset(cleared);
    CHECK(watch.poll(cleared, seconds += 0.5) == WatchDecision::Wait);
    const FileStamp again{103, 203, 4, 4, true};
    for (int poll = 0; poll < DocumentWatch::kStagingPatiencePolls; ++poll)
        CHECK(watch.poll(again, seconds += 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(again, seconds += 0.5) == WatchDecision::Wait);
    CHECK(watch.takeStagingWarning());
}

//======================================================================================================================
TEST_CASE("a staging directory that clears in time never warns", "[unit][session]") {
    DocumentWatch watch;
    const FileStamp loaded{100, 200, 1, 1, false};
    const FileStamp saving{101, 201, 2, 2, true};
    const FileStamp saved{101, 201, 2, 2, false};
    watch.reset(loaded);
    for (int poll = 1; poll < DocumentWatch::kStagingPatiencePolls; ++poll)
        CHECK(watch.poll(saving, 0.5 * poll) == WatchDecision::Wait);
    CHECK(watch.poll(saved, 5.0) == WatchDecision::Wait);
    CHECK(watch.poll(saved, 5.5) == WatchDecision::Hash);
    CHECK_FALSE(watch.takeStagingWarning());
}

//======================================================================================================================
TEST_CASE("a replaced file with the same size and time is a changed pair", "[unit][session]") {
    namespace fs = std::filesystem;
    const fs::path directory = "SessionWatchReplace";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const fs::path document = directory / "replace.scene.gltf";
    const auto write = [](const fs::path& path, std::string_view bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << bytes;
        REQUIRE(output.good());
    };
    write(document, R"({"asset":{"version":"2.0"},"scene":0})");
    DocumentProbe probe;
    const auto loaded = probe.observe(document);
    CHECK(loaded.gltfSize == fs::file_size(document));
    CHECK(loaded.gltfInode != 0);
    CHECK(loaded.bufferInode == 0);
    CHECK_FALSE(loaded.saving);
    CHECK(probe.observe(document) == loaded);

    const fs::path replacement = directory / "replacement.tmp";
    write(replacement, R"({"asset":{"version":"2.0"},"scene":1})");
    fs::last_write_time(replacement, fs::last_write_time(document));
    fs::rename(replacement, document);
    const auto replaced = probe.observe(document);
    CHECK(replaced.gltfSize == loaded.gltfSize);
    CHECK(replaced.gltfTime == loaded.gltfTime);
    CHECK(replaced.gltfInode != loaded.gltfInode);
    CHECK_FALSE(replaced.samePair(loaded));
    DocumentWatch watch;
    watch.reset(loaded);
    CHECK(watch.poll(replaced, 0.5) == WatchDecision::Wait);
    CHECK(watch.poll(replaced, 1.0) == WatchDecision::Hash);

    FileStamp staging = loaded;
    staging.saving = true;
    CHECK(staging != loaded);
    CHECK(staging.samePair(loaded));
    fs::remove_all(directory);
}

//======================================================================================================================
TEST_CASE("the probe rereads the glTF and its directory only when their stamps change",
          "[unit][session]") {
    namespace fs = std::filesystem;
    const fs::path directory = "SessionWatchProbe";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const fs::path document = directory / "probe.scene.gltf";
    const auto write = [](const fs::path& path, std::string_view bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << bytes;
        REQUIRE(output.good());
    };
    write(directory / "a.bin", "ab");
    write(directory / "b.bin", "abcd");
    write(document, R"({"buffers":[{"uri":"a.bin","byteLength":2}]})");
    DocumentProbe probe;
    const auto first = probe.observe(document);
    CHECK(first.bufferSize == 2);

    // The companion is stamped on every observation; only its path is remembered.
    write(directory / "a.bin", "abc");
    CHECK(probe.observe(document).bufferSize == 3);

    // An in-place rewrite that keeps the glTF's size, time and identity is not re-read, so the
    // remembered companion stands until the glTF's stamp moves.
    const auto time = fs::last_write_time(document);
    write(document, R"({"buffers":[{"uri":"b.bin","byteLength":2}]})");
    fs::last_write_time(document, time);
    const auto remembered = probe.observe(document);
    CHECK(remembered.gltfTime == first.gltfTime);
    CHECK(remembered.bufferSize == 3);
    fs::last_write_time(document, time + std::chrono::seconds(1));
    CHECK(probe.observe(document).bufferSize == 4);
    CHECK(DocumentProbe{}.observe(document).bufferSize == 4);

    // Creating and removing a staging directory moves the directory's own stamp.
    CHECK_FALSE(probe.observe(document).saving);
    fs::create_directory(directory / ".lmx-save-1234.tmp");
    CHECK(probe.observe(document).saving);
    fs::remove(directory / ".lmx-save-1234.tmp");
    CHECK_FALSE(probe.observe(document).saving);

    // A glTF that cannot be opened falls back to the loaded document's buffer URI.
    fs::remove(document);
    const auto missing = probe.observe(document, std::string("b.bin"));
    CHECK(missing.gltfSize == 0);
    CHECK(missing.gltfInode == 0);
    CHECK(missing.bufferSize == 4);
    fs::remove_all(directory);
}

//======================================================================================================================
TEST_CASE("Save replaces only a pair the editor loaded or the operator rejected",
          "[unit][session][save-watch]") {
    const std::string_view review = "The scene file changed on disk; review its proposal first";
    const std::string_view soon =
        "The scene file changed on disk; its proposal will appear in the Session panel shortly";
    const std::string_view unreadable = "The scene file or its buffer cannot be read on disk";
    for (const bool pending : {false, true}) {
        // The loaded document is still on disk.
        CHECK_FALSE(saveOverwriteReason("loaded", "loaded", false, true, pending));
        CHECK_FALSE(saveOverwriteReason("loaded", "loaded", false, false, pending));
        // The operator rejected exactly these bytes; a later Save overwrites them.
        CHECK_FALSE(saveOverwriteReason("loaded", "external", true, false, pending));
        // An unreadable or missing pair is replaceable only at the stamp already loaded or
        // rejected, and is never described as a proposal to review.
        CHECK_FALSE(saveOverwriteReason("loaded", "", false, true, pending));
        CHECK(saveOverwriteReason("loaded", "", false, false, pending) == unreadable);
        CHECK(saveOverwriteReason("loaded", "", true, false, pending) == unreadable);
    }
    // An external edit the watch has not turned into a card yet.
    CHECK(saveOverwriteReason("loaded", "external", false, false, false) == soon);
    CHECK(saveOverwriteReason("loaded", "external", false, true, false) == soon);
    // An external edit whose card is pending.
    CHECK(saveOverwriteReason("loaded", "external", false, false, true) == review);
    CHECK(saveOverwriteReason("loaded", "external", false, true, true) == review);
}

//======================================================================================================================
TEST_CASE("the probe rescans while it reports a staging directory", "[unit][session]") {
    namespace fs = std::filesystem;
    const fs::path directory = "SessionWatchCoarseProbe";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const fs::path document = directory / "probe.scene.gltf";
    {
        std::ofstream output(document, std::ios::binary);
        output << R"({"buffers":[]})";
        REQUIRE(output.good());
    }
    DocumentProbe probe;
    fs::create_directory(directory / ".lmx-save-1234.tmp");
    const auto time = fs::last_write_time(directory);
    CHECK(probe.observe(document).saving);
    // A coarse modification clock: the removal lands in the tick the creation was observed in,
    // so the directory's stamp does not move.
    fs::remove(directory / ".lmx-save-1234.tmp");
    fs::last_write_time(directory, time);
    CHECK_FALSE(probe.observe(document).saving);
    CHECK_FALSE(probe.observe(document).saving);
    fs::remove_all(directory);
}
