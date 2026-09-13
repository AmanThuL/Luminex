#include "Asset/BmpImage.h"
#include "Asset/RepositoryAsset.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

class TempTree {
public:
    //==================================================================================================================
    TempTree() : m_previous(std::filesystem::current_path()) {
        static std::atomic<uint32_t> sequence{0};
        m_root = std::filesystem::temp_directory_path() /
                 ("lmx-asset-files-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(sequence++));
        std::filesystem::create_directories(m_root);
        m_root = std::filesystem::canonical(m_root);
    }

    //==================================================================================================================
    ~TempTree() {
        std::error_code error;
        std::filesystem::current_path(m_previous, error);
        std::filesystem::remove_all(m_root, error);
    }

    //==================================================================================================================
    const std::filesystem::path& root() const { return m_root; }

private:
    std::filesystem::path m_previous;
    std::filesystem::path m_root;
};

//======================================================================================================================
void touch(const std::filesystem::path& path) {
    std::ofstream file(path);
    REQUIRE(file.good());
}

} // namespace

//======================================================================================================================
TEST_CASE("repository asset discovery preserves existing-path and regular-file predicates",
          "[asset][repository-asset]") {
    TempTree tree;
    const auto child = tree.root() / "child";
    std::filesystem::create_directories(child / "candidate");
    touch(tree.root() / "candidate");
    std::filesystem::current_path(child);

    REQUIRE(lmx::asset::findRepositoryAsset("candidate") == child / "candidate");
    REQUIRE(lmx::asset::findRepositoryAsset(
                "candidate", lmx::asset::RepositoryAssetKind::ExistingPath) == child / "candidate");
    REQUIRE(lmx::asset::findRepositoryAsset("candidate",
                                            lmx::asset::RepositoryAssetKind::RegularFile) ==
            tree.root() / "candidate");
}

//======================================================================================================================
TEST_CASE("repository asset discovery chooses the nearest candidate and reports missing paths",
          "[asset][repository-asset]") {
    TempTree tree;
    const auto child = tree.root() / "child";
    std::filesystem::create_directories(child);
    touch(tree.root() / "candidate");
    touch(child / "candidate");
    std::filesystem::current_path(child);

    for (const auto kind : {lmx::asset::RepositoryAssetKind::ExistingPath,
                            lmx::asset::RepositoryAssetKind::RegularFile}) {
        REQUIRE(lmx::asset::findRepositoryAsset("candidate", kind) == child / "candidate");
        REQUIRE_FALSE(lmx::asset::findRepositoryAsset("lmx-missing-asset/candidate", kind));
    }
}

//======================================================================================================================
TEST_CASE("repository asset discovery includes seven parents and excludes the eighth",
          "[asset][repository-asset]") {
    TempTree tree;
    touch(tree.root() / "candidate");
    auto nested = tree.root();
    for (int i = 0; i < 7; ++i) {
        nested /= "nested";
    }
    std::filesystem::create_directories(nested / "eighth");
    std::filesystem::current_path(nested);
    REQUIRE(lmx::asset::findRepositoryAsset("candidate") == tree.root() / "candidate");
    std::filesystem::current_path(nested / "eighth");
    REQUIRE_FALSE(lmx::asset::findRepositoryAsset("candidate"));
}

//======================================================================================================================
TEST_CASE("BMP writer preserves top-down BGRA bytes and reports inaccessible output",
          "[asset][bmp]") {
    TempTree tree;
    const std::vector<uint8_t> pixels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const auto path = tree.root() / "image.bmp";
    REQUIRE(lmx::asset::writeBmp(path, pixels, 2, 2));
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> actual = {std::istreambuf_iterator<char>(file),
                                         std::istreambuf_iterator<char>()};
    // A 2x2, 32-bit BI_RGB image has a 54-byte header, negative height and 72-DPI density.
    const std::array<uint8_t, 54> header = {
        0x42, 0x4d, 0x46, 0,    0,    0,    0,    0,    0, 0, 0x36, 0, 0, 0, 0x28, 0, 0,    0,
        2,    0,    0,    0,    0xfe, 0xff, 0xff, 0xff, 1, 0, 0x20, 0, 0, 0, 0,    0, 0x10, 0,
        0,    0,    0x13, 0x0b, 0,    0,    0x13, 0x0b, 0, 0, 0,    0, 0, 0, 0,    0, 0,    0};
    std::vector<uint8_t> expected(header.begin(), header.end());
    expected.insert(expected.end(), pixels.begin(), pixels.end());
    REQUIRE(actual == expected);
    REQUIRE_FALSE(lmx::asset::writeBmp(tree.root() / "absent" / "image.bmp", pixels, 2, 2));
    REQUIRE_FALSE(std::filesystem::exists(tree.root() / "absent"));
}
