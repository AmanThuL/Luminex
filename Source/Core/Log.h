//----------------------------------------------------------------------------------------------------------------------
/// @file Log.h
/// @brief Declares logging initialization and project logging macros.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <spdlog/spdlog.h>

namespace lmx::log {
/// Initializes project logging; safe to call more than once.
void init();
} // namespace lmx::log

/// Emits an informational project log message.
#define LMX_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
/// Emits a warning project log message.
#define LMX_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
/// Emits an error project log message.
#define LMX_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
