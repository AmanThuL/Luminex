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

/// Emits a debug project log message. The call is filtered by the runtime level, not compiled
/// out: SPDLOG_DEBUG would vanish under spdlog's default compile-time level.
#define LMX_LOG_DEBUG(...)                                                                         \
    SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), spdlog::level::debug, __VA_ARGS__)
/// Emits an informational project log message.
#define LMX_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
/// Emits a warning project log message.
#define LMX_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
/// Emits an error project log message.
#define LMX_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
