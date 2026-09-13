//----------------------------------------------------------------------------------------------------------------------
/// @file File.h
/// @brief Declares whole-file binary reading with explicit I/O failure stages.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>
#include <expected>
#include <filesystem>
#include <vector>

namespace lmx {

/// The failed stage of a whole-file read; callers translate it into their domain's diagnostics.
enum class FileReadError {
    Open, ///< The binary input stream could not be opened.
    Size, ///< Seeking to the end did not produce a non-negative file size.
    Read  ///< Rewinding or reading the complete byte count failed.
};

/// Reads a seekable file in binary mode into owned bytes; an empty file succeeds with no bytes.
/// Returns the failed I/O stage without imposing decoder size limits or interpreting the contents.
std::expected<std::vector<std::byte>, FileReadError>
readWholeFile(const std::filesystem::path& path);

} // namespace lmx
