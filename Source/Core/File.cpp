//----------------------------------------------------------------------------------------------------------------------
/// @file File.cpp
/// @brief Implements whole-file binary reading and stage-specific failures.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/File.h"

#include <fstream>

namespace lmx {

//======================================================================================================================
std::expected<std::vector<std::byte>, FileReadError>
readWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(FileReadError::Open);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected(FileReadError::Size);
    }
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) {
        return std::unexpected(FileReadError::Read);
    }
    return bytes;
}

} // namespace lmx
