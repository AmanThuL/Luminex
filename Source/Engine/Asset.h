#pragma once

#include <expected>
#include <string>

namespace lmx::engine {

enum class AssetErrorCode {
    NotFound,
    Io,
    Malformed,
    Unsupported,
    UploadFailed,
};

struct AssetError {
    AssetErrorCode code;
    std::string message;
};

template <typename T>
using AssetResult = std::expected<T, AssetError>;

} // namespace lmx::engine
