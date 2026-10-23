//----------------------------------------------------------------------------------------------------------------------
/// @file Result.h
/// @brief Defines the project error vocabulary and the expected-based result type.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <expected>
#include <string>

namespace lmx {

/// Identifies the category of a failed operation.
enum class ErrorCode {
    InvalidArgument, ///< A caller-supplied value violated a precondition.
    NotFound,        ///< A named resource or entry does not exist.
    Io,              ///< Reading or writing a file failed.
    Parse,           ///< Input data could not be decoded.
    Gpu,             ///< A GPU object, library, pipeline or swapchain could not be created.
    Unsupported,     ///< The requested capability is unavailable on this device.
};

/// Describes a failure with a stable category and a diagnostic message.
struct Error {
    ErrorCode code;      ///< Stable failure category.
    std::string message; ///< Human-readable diagnostic context.
};

/// Holds either a value or an error.
template <typename T>
using Result = std::expected<T, Error>;

} // namespace lmx
