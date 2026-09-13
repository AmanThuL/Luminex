//----------------------------------------------------------------------------------------------------------------------
/// @file RepositoryAsset.cpp
/// @brief Implements bounded, nearest-first repository asset discovery.
//----------------------------------------------------------------------------------------------------------------------

#include "Asset/RepositoryAsset.h"

namespace lmx::asset {

//======================================================================================================================
std::optional<std::filesystem::path> findRepositoryAsset(std::string_view relative,
                                                         RepositoryAssetKind kind) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        const std::filesystem::path candidate = dir / relative;
        const bool matches = kind == RepositoryAssetKind::RegularFile
                                 ? std::filesystem::is_regular_file(candidate)
                                 : std::filesystem::exists(candidate);
        if (matches) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

} // namespace lmx::asset
