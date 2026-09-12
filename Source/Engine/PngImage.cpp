//----------------------------------------------------------------------------------------------------------------------
/// @file PngImage.cpp
/// @brief Implements deterministic SDR PNG chunks, validation, and decoding.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/PngImage.h"

#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>

namespace lmx::engine {

namespace {

constexpr std::array<uint8_t, 8> kSignature = {137, 80, 78, 71, 13, 10, 26, 10};

//======================================================================================================================
std::unexpected<AssetError> fail(const std::filesystem::path& path, std::string message,
                                 AssetErrorCode code = AssetErrorCode::Malformed) {
    return std::unexpected(AssetError{code, "PNG '" + path.string() + "': " + std::move(message)});
}

//======================================================================================================================
uint32_t readBigEndian(const uint8_t* bytes) {
    return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8) |
           bytes[3];
}

//======================================================================================================================
void appendBigEndian(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

//======================================================================================================================
uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

//======================================================================================================================
void appendChunk(std::vector<uint8_t>& bytes, std::string_view type,
                 std::span<const uint8_t> payload) {
    appendBigEndian(bytes, static_cast<uint32_t>(payload.size()));
    const size_t start = bytes.size();
    bytes.insert(bytes.end(), type.begin(), type.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    appendBigEndian(bytes, crc32(std::span(bytes).subspan(start)));
}

//======================================================================================================================
bool validKeyword(std::string_view keyword) {
    return !keyword.empty() && keyword.size() <= 79 && keyword.front() != ' ' &&
           keyword.back() != ' ' && !keyword.contains("  ") &&
           std::all_of(keyword.begin(), keyword.end(),
                       [](unsigned char c) { return (c >= 32 && c <= 126) || c >= 161; });
}

} // namespace

//======================================================================================================================
AssetResult<void> writePng(const std::filesystem::path& path, std::span<const uint8_t> rgba,
                           uint32_t width, uint32_t height, std::span<const PngTextChunk> text) {
    // stb's encoder sizes its row and filtered-image buffers with signed int arithmetic.
    const uint64_t rowBytes = uint64_t{width} * 4;
    if (width == 0 || height == 0 || rowBytes > std::numeric_limits<int>::max() ||
        (rowBytes + 1) * height > std::numeric_limits<int>::max() ||
        rgba.size() != rowBytes * height) {
        return fail(path, "expected complete nonempty RGBA pixels within encoder size limits");
    }
    for (const auto& entry : text) {
        if (!validKeyword(entry.keyword) || entry.text.contains('\0') ||
            entry.text.size() > std::numeric_limits<uint32_t>::max() - 80u) {
            return fail(path, "invalid tEXt keyword or payload");
        }
    }
    int encodedSize = 0;
    std::unique_ptr<unsigned char, decltype(&std::free)> encoded(
        stbi_write_png_to_mem(rgba.data(), static_cast<int>(rowBytes), static_cast<int>(width),
                              static_cast<int>(height), 4, &encodedSize),
        &std::free);
    if (!encoded || encodedSize < 33) {
        return fail(path, "encoder failed", AssetErrorCode::Io);
    }
    // stb's signature and fixed-size IHDR occupy 33 bytes. Preserve its compressed stream.
    std::vector<uint8_t> bytes(encoded.get(), encoded.get() + 33);
    const std::array<uint8_t, 1> intent = {0};
    appendChunk(bytes, "sRGB", intent);
    std::vector<uint8_t> gamma;
    appendBigEndian(gamma, 45455);
    appendChunk(bytes, "gAMA", gamma);
    std::vector<uint8_t> chromaticities;
    for (uint32_t value : {31270u, 32900u, 64000u, 33000u, 30000u, 60000u, 15000u, 6000u}) {
        appendBigEndian(chromaticities, value);
    }
    appendChunk(bytes, "cHRM", chromaticities);
    for (const auto& entry : text) {
        std::vector<uint8_t> payload(entry.keyword.begin(), entry.keyword.end());
        payload.push_back(0);
        payload.insert(payload.end(), entry.text.begin(), entry.text.end());
        appendChunk(bytes, "tEXt", payload);
    }
    bytes.insert(bytes.end(), encoded.get() + 33, encoded.get() + encodedSize);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(path, "cannot open for writing", AssetErrorCode::Io);
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        return fail(path, "write failed", AssetErrorCode::Io);
    }
    return {};
}

//======================================================================================================================
AssetResult<PngImage> readPng(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return fail(path, "cannot open for reading", AssetErrorCode::Io);
    }
    const std::streamoff size = file.tellg();
    if (size < 8 || size > std::numeric_limits<int>::max()) {
        return fail(path, "file is truncated or exceeds decoder size limits");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return fail(path, "read failed", AssetErrorCode::Io);
    }
    if (!std::equal(kSignature.begin(), kSignature.end(), bytes.begin())) {
        return fail(path, "invalid PNG signature");
    }
    PngImage image;
    bool ended = false;
    for (size_t offset = 8; offset < bytes.size();) {
        if (bytes.size() - offset < 12) {
            return fail(path, "truncated chunk header");
        }
        const uint32_t length = readBigEndian(bytes.data() + offset);
        if (length > bytes.size() - offset - 12) {
            return fail(path, "truncated chunk payload");
        }
        const std::string_view type(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        const auto payload = std::span(bytes).subspan(offset + 8, length);
        if (crc32(std::span(bytes).subspan(offset + 4, size_t{length} + 4)) !=
            readBigEndian(bytes.data() + offset + 8 + length)) {
            return fail(path, "CRC mismatch in " + std::string(type));
        }
        if (offset == 8 && (type != "IHDR" || length != 13)) {
            return fail(path, "missing or invalid initial IHDR");
        }
        if (type == "tEXt") {
            const auto separator = std::find(payload.begin(), payload.end(), 0);
            if (separator == payload.end()) {
                return fail(path, "tEXt has no keyword separator");
            }
            PngTextChunk entry{std::string(payload.begin(), separator),
                               std::string(separator + 1, payload.end())};
            if (!validKeyword(entry.keyword) || entry.text.contains('\0')) {
                return fail(path, "invalid tEXt keyword or payload");
            }
            image.text.push_back(std::move(entry));
        }
        offset += size_t{length} + 12;
        if (type == "IEND") {
            if (length != 0 || offset != bytes.size()) {
                return fail(path, "invalid IEND or trailing bytes");
            }
            ended = true;
        }
    }
    if (!ended) {
        return fail(path, "missing IEND");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                              &channels, 4),
        &stbi_image_free);
    if (!decoded) {
        const char* reason = stbi_failure_reason();
        return fail(path, reason ? reason : "decoder failed");
    }
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(decoded.get(), decoded.get() + size_t{image.width} * image.height * 4);
    return image;
}

} // namespace lmx::engine
