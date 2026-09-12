#include "Engine/PngImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace lmx::engine;

namespace {

//======================================================================================================================
std::vector<uint8_t> fileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

//======================================================================================================================
void putBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file);
}

//======================================================================================================================
uint32_t bigEndian(std::span<const uint8_t> bytes, size_t offset) {
    return (uint32_t{bytes[offset]} << 24) | (uint32_t{bytes[offset + 1]} << 16) |
           (uint32_t{bytes[offset + 2]} << 8) | bytes[offset + 3];
}

//======================================================================================================================
uint32_t pngCrc(std::span<const uint8_t> bytes) {
    std::array<uint32_t, 256> table;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = value & 1 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        }
        table[i] = value;
    }
    uint32_t result = 0xFFFFFFFFu;
    for (uint8_t byte : bytes) {
        result = table[(result ^ byte) & 0xFFu] ^ (result >> 8);
    }
    return result ^ 0xFFFFFFFFu;
}

} // namespace

//======================================================================================================================
TEST_CASE("SDR PNG chunks have declared order, values and CRCs", "[engine][png]") {
    const auto path = std::filesystem::temp_directory_path() / "lmx-png-chunks.png";
    const std::array<uint8_t, 8> pixels = {1, 128, 255, 0, 23, 45, 67, 89};
    const std::array<PngTextChunk, 2> text = {PngTextChunk{"lmx:display", "{\"view\":\"sdr\"}"},
                                              PngTextChunk{"lmx:frame", "{\"scene\":\"test\"}"}};
    REQUIRE(writePng(path, pixels, 2, 1, text));
    const auto bytes = fileBytes(path);
    const std::array<uint8_t, 8> signature = {137, 80, 78, 71, 13, 10, 26, 10};
    REQUIRE(std::equal(signature.begin(), signature.end(), bytes.begin()));
    std::vector<std::string> types;
    for (size_t offset = 8; offset < bytes.size();) {
        REQUIRE(bytes.size() - offset >= 12);
        const size_t size = bigEndian(bytes, offset);
        REQUIRE(size <= bytes.size() - offset - 12);
        const std::string type(bytes.begin() + offset + 4, bytes.begin() + offset + 8);
        types.push_back(type);
        REQUIRE(pngCrc(std::span(bytes).subspan(offset + 4, size + 4)) ==
                bigEndian(bytes, offset + size + 8));
        const size_t payload = offset + 8;
        if (type == "IHDR") {
            REQUIRE(bigEndian(bytes, payload) == 2);
            REQUIRE(bigEndian(bytes, payload + 4) == 1);
            REQUIRE(bytes[payload + 8] == 8);
            REQUIRE(bytes[payload + 9] == 6);
        } else if (type == "sRGB") {
            REQUIRE(size == 1);
            REQUIRE(bytes[payload] == 0);
            REQUIRE(bigEndian(bytes, payload + 1) == 0xAECE1CE9u);
        } else if (type == "gAMA") {
            REQUIRE(size == 4);
            REQUIRE(bigEndian(bytes, payload) == 45455);
        } else if (type == "cHRM") {
            REQUIRE(size == 32);
            const std::array<uint32_t, 8> expected = {31270, 32900, 64000, 33000,
                                                      30000, 60000, 15000, 6000};
            for (size_t index = 0; index < expected.size(); ++index) {
                REQUIRE(bigEndian(bytes, payload + index * 4) == expected[index]);
            }
        }
        offset += size + 12;
    }
    REQUIRE(types == std::vector<std::string>{"IHDR", "sRGB", "gAMA", "cHRM", "tEXt", "tEXt",
                                              "IDAT", "IEND"});
    const auto decoded = readPng(path);
    REQUIRE(decoded);
    REQUIRE(decoded->width == 2);
    REQUIRE(decoded->height == 1);
    REQUIRE(decoded->rgba == std::vector<uint8_t>(pixels.begin(), pixels.end()));
    REQUIRE(decoded->text.size() == text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        REQUIRE(decoded->text[index].keyword == text[index].keyword);
        REQUIRE(decoded->text[index].text == text[index].text);
    }
    std::filesystem::remove(path);
}

//======================================================================================================================
TEST_CASE("PNG writes are deterministic at one pixel and capture extent", "[engine][png]") {
    const auto first = std::filesystem::temp_directory_path() / "lmx-png-deterministic-a.png";
    const auto second = std::filesystem::temp_directory_path() / "lmx-png-deterministic-b.png";
    for (const auto [width, height] : {std::pair{1u, 1u}, std::pair{1280u, 720u}}) {
        CAPTURE(width, height);
        std::vector<uint8_t> pixels(size_t{width} * height * 4);
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<uint8_t>((i * 17 + i / 256) & 255);
        }
        REQUIRE(writePng(first, pixels, width, height));
        REQUIRE(writePng(second, pixels, width, height));
        REQUIRE(fileBytes(first) == fileBytes(second));
        const auto decoded = readPng(first);
        REQUIRE(decoded);
        REQUIRE(decoded->width == width);
        REQUIRE(decoded->height == height);
        REQUIRE(decoded->rgba == pixels);
        REQUIRE(decoded->text.empty());
    }
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

//======================================================================================================================
TEST_CASE("PNG boundary rejects malformed pixels, text, chunks and inaccessible files",
          "[engine][png]") {
    const auto path = std::filesystem::temp_directory_path() / "lmx-png-invalid.png";
    const std::array<uint8_t, 4> pixels = {10, 20, 30, 40};
    REQUIRE_FALSE(writePng(path, pixels, 0, 1));
    REQUIRE_FALSE(writePng(path, pixels, 2, 1));
    REQUIRE_FALSE(writePng(path, pixels, std::numeric_limits<uint32_t>::max(), 1));
    for (const auto& entry : std::vector<PngTextChunk>{{"", "text"},
                                                       {" key", "text"},
                                                       {"key ", "text"},
                                                       {"two  spaces", "text"},
                                                       {std::string(80, 'x'), "text"},
                                                       {"key", std::string("a\0b", 3)}}) {
        REQUIRE_FALSE(writePng(path, pixels, 1, 1, std::span(&entry, 1)));
    }
    REQUIRE(writePng(path, pixels, 1, 1));
    const auto valid = fileBytes(path);
    auto corrupt = valid;
    corrupt[29] ^= 1;
    putBytes(path, corrupt);
    const auto crcFailure = readPng(path);
    REQUIRE_FALSE(crcFailure);
    REQUIRE(crcFailure.error().message.contains("CRC mismatch"));
    for (size_t length : {size_t{0}, size_t{7}, size_t{20}, valid.size() - 1, valid.size() - 12}) {
        putBytes(path, std::span(valid).first(length));
        REQUIRE_FALSE(readPng(path));
    }
    corrupt = valid;
    corrupt[8] = 0xFF;
    putBytes(path, corrupt);
    REQUIRE_FALSE(readPng(path));
    corrupt = valid;
    corrupt.push_back(0);
    putBytes(path, corrupt);
    REQUIRE_FALSE(readPng(path));
    std::filesystem::remove(path);
    REQUIRE_FALSE(readPng(path));
    REQUIRE_FALSE(writePng(path / "missing" / "out.png", pixels, 1, 1));
}
