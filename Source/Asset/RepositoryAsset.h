//----------------------------------------------------------------------------------------------------------------------
/// @file RepositoryAsset.h
/// @brief Declares working-directory-relative repository asset discovery.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace lmx::asset {

/// Selects the historical filesystem predicate required by the calling scene.
enum class RepositoryAssetKind {
    ExistingPath, ///< Accept any existing path, including a directory.
    RegularFile,  ///< Accept a regular file only.
};

/// Searches the working directory and up to seven parents, nearest first, for `relative`.
/// Returns the first matching path or nullopt; filesystem errors propagate to the caller.
std::optional<std::filesystem::path>
findRepositoryAsset(std::string_view relative,
                    RepositoryAssetKind kind = RepositoryAssetKind::ExistingPath);

} // namespace lmx::asset
