//----------------------------------------------------------------------------------------------------------------------
/// @file BmpImage.cpp
/// @brief Implements deterministic top-down BGRA bitmap writing.
//----------------------------------------------------------------------------------------------------------------------

#include "Asset/BmpImage.h"

#include "Core/Log.h"

#include <fstream>

namespace lmx::asset {

namespace {

//======================================================================================================================
void appendLittleEndian(std::vector<uint8_t>& out, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.push_back(static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu));
    }
}

//======================================================================================================================
void appendLittleEndian(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

} // namespace

//======================================================================================================================
// A negative-height, 32-bit BI_RGB bitmap accepts top-down BGRA8 readback verbatim. Four-byte
// pixels also satisfy BMP row alignment without padding.
bool writeBmp(const std::filesystem::path& path, const std::vector<uint8_t>& bgra, uint32_t width,
              uint32_t height) {
    constexpr uint32_t kFileHeaderSize = 14;
    constexpr uint32_t kInfoHeaderSize = 40;
    constexpr uint32_t kPixelOffset = kFileHeaderSize + kInfoHeaderSize;
    // Nonzero density prevents image readers from guessing display scale.
    constexpr int32_t kPixelsPerMeter = 2835;

    const uint32_t imageSize = static_cast<uint32_t>(bgra.size());

    std::vector<uint8_t> header;
    header.reserve(kPixelOffset);
    header.push_back('B');
    header.push_back('M');
    appendLittleEndian(header, kPixelOffset + imageSize);
    appendLittleEndian(header, uint16_t{0}); // reserved1
    appendLittleEndian(header, uint16_t{0}); // reserved2
    appendLittleEndian(header, kPixelOffset);

    appendLittleEndian(header, kInfoHeaderSize);
    appendLittleEndian(header, width);
    // BMP encodes top-down rows with a negative signed height.
    appendLittleEndian(header, static_cast<uint32_t>(-static_cast<int32_t>(height)));
    appendLittleEndian(header, uint16_t{1});  // planes
    appendLittleEndian(header, uint16_t{32}); // bits per pixel
    appendLittleEndian(header, uint32_t{0});  // BI_RGB, no compression
    appendLittleEndian(header, imageSize);
    appendLittleEndian(header, static_cast<uint32_t>(kPixelsPerMeter));
    appendLittleEndian(header, static_cast<uint32_t>(kPixelsPerMeter));
    appendLittleEndian(header, uint32_t{0}); // palette colors used
    appendLittleEndian(header, uint32_t{0}); // palette colors required

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LMX_LOG_ERROR("screenshot: cannot open '{}' for writing", path.string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(bgra.data()),
               static_cast<std::streamsize>(bgra.size()));
    file.close();
    if (!file) {
        LMX_LOG_ERROR("screenshot: failed while writing '{}'", path.string());
        return false;
    }
    return true;
}

} // namespace lmx::asset
