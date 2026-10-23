//----------------------------------------------------------------------------------------------------------------------
/// @file GpuError.h
/// @brief Declares the single translation from a RojoRHI error to the project error.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"

#include <rojoRHI/Result.h>

namespace lmx::engine {

/// Translates a RojoRHI error into the project error, keeping the message verbatim.
Error gpuError(const rojoRHI::Error& error);

} // namespace lmx::engine
