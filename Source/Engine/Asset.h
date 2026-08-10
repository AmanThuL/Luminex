//----------------------------------------------------------------------------------------------------------------------
/// @file Asset.h
/// @brief Defines asset-loading errors and expected-result aliases.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <expected>
#include <string>

namespace lmx::engine {

/// Categorizes failures encountered while locating, decoding, or uploading an asset.
enum class AssetErrorCode {
    NotFound,     ///< Required source path does not exist.
    Io,           ///< Filesystem access failed.
    Malformed,    ///< Source bytes violate the expected format.
    Unsupported,  ///< Source uses a valid but unsupported feature.
    UploadFailed, ///< GPU resource creation failed.
};

/// Describes an asset failure with a stable category and actionable context.
struct AssetError {
    AssetErrorCode code; ///< Stable failure category for programmatic handling.
    std::string message; ///< Human-readable context naming the affected asset.
};

/// Expected result used by asset-loading and baking boundaries.
template <typename T>
using AssetResult = std::expected<T, AssetError>;

} // namespace lmx::engine
