//----------------------------------------------------------------------------------------------------------------------
/// @file GpuError.cpp
/// @brief Implements the translation from a RojoRHI error to the project error.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuError.h"

namespace lmx::engine {

namespace {

//======================================================================================================================
ErrorCode gpuErrorCode(rojoRHI::ErrorCode code) {
    switch (code) {
    case rojoRHI::ErrorCode::InvalidDesc:
        return ErrorCode::InvalidArgument;
    case rojoRHI::ErrorCode::DeviceUnsupported:
        return ErrorCode::Unsupported;
    case rojoRHI::ErrorCode::ShaderLoadFailed:
    case rojoRHI::ErrorCode::PipelineCreationFailed:
    case rojoRHI::ErrorCode::ResourceCreationFailed:
    case rojoRHI::ErrorCode::SwapchainFailed:
        return ErrorCode::Gpu;
    }
    return ErrorCode::Gpu; // unreachable for a valid enumerator; keeps the exhaustive switch free
                           // of default
}

} // namespace

//======================================================================================================================
Error gpuError(const rojoRHI::Error& error) {
    return Error{gpuErrorCode(error.code), error.message};
}

} // namespace lmx::engine
