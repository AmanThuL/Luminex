//----------------------------------------------------------------------------------------------------------------------
/// @file RhiLog.h
/// @brief Declares the forwarding of RHI diagnostic messages into the project log.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::render {

/// Installs the process-wide RHI message sink that forwards each message to the project log,
/// mapping Info, Warning and Error onto the matching `LMX_LOG_*` macro. Call it once at startup,
/// after the project log is initialized; without it the RHI writes its messages to stderr.
void installRhiLogForwarding();

} // namespace lmx::render
