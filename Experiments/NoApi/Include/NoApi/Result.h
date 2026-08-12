//----------------------------------------------------------------------------------------------------------------------
/// @file Result.h
/// @brief Defines the prototype error domain and expected-based result type.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <expected>
#include <string>

namespace lmx::experimental::noapi {

/// Identifies the category of a prototype creation failure.
///
/// Creation is the only operation kind that reports failure through a value. Every other contract
/// in this interface is a precondition checked with `LMX_ASSERT`, because the model treats binding,
/// recording, and synchronization as caller-owned invariants rather than recoverable conditions.
enum class ErrorCode {
    /// The device or the required GPU feature level is unavailable.
    DeviceUnsupported,
    /// A memory allocation of the requested size, alignment, or kind failed.
    AllocationFailed,
    /// A texture, sampler, table, or synchronization object could not be created.
    ResourceCreationFailed,
    /// Shader intermediate code could not be loaded or compiled.
    ShaderLoadFailed,
    /// A compute or graphics pipeline could not be created.
    PipelineCreationFailed,
    /// A descriptor violated a documented precondition that is reportable rather than fatal.
    InvalidDesc,
};

/// Describes a prototype creation failure with a stable category and diagnostic context.
struct Error {
    ErrorCode code; ///< Stable failure category.
    /// Human-readable diagnostic context, including the failing object's label.
    std::string message;
};

/// Holds either a prototype creation result or the error that prevented it.
template <typename T>
using Result = std::expected<T, Error>;

} // namespace lmx::experimental::noapi
