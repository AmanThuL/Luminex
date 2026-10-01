#include "App/Model/Session/DocumentWatch.h"

#include "Engine/Asset/Document/SceneDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

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
