//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentSaveInternal.h
/// @brief Declares the private filesystem seam used to verify scene-save rollback failures.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"

#include <filesystem>
#include <functional>
#include <system_error>

namespace lmx::asset {
struct SceneDocument;

namespace detail {

/// Rename operation shared by backup, install and rollback; tests inject reported failures.
using DocumentRename = std::function<void(const std::filesystem::path&,
                                          const std::filesystem::path&, std::error_code&)>;

/// Uses the same rename operation for backup, install and restoration; production supplies the
/// filesystem implementation, while tests inject one reported failure followed by real recovery.
AssetResult<void> saveSceneDocumentWithRename(const SceneDocument& doc,
                                              const std::filesystem::path& path,
                                              const DocumentRename& rename);

} // namespace detail
} // namespace lmx::asset
