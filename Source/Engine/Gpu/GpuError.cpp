//----------------------------------------------------------------------------------------------------------------------
/// @file GpuError.cpp
/// @brief Translates GPU creation error categories without changing their messages.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuError.h"

namespace lmx::engine {
namespace {

//======================================================================================================================
ErrorCode gpuErrorCode(rojoRHI::ErrorCategory category) {
    switch (category) {
    case rojoRHI::ErrorCategory::Unsupported:
        return ErrorCode::Unsupported;
    case rojoRHI::ErrorCategory::OutOfMemory:
    case rojoRHI::ErrorCategory::DriverFailure:
    case rojoRHI::ErrorCategory::ShaderAbiMismatch:
        return ErrorCode::Gpu;
    }
    return ErrorCode::Gpu;
}

} // namespace

//======================================================================================================================
Error gpuError(const rojoRHI::Error& error) {
    return Error{gpuErrorCode(error.category), error.message};
}

} // namespace lmx::engine
