#pragma once
#include <spdlog/spdlog.h>

namespace lmx::log {
void init(); // safe to call more than once
}

#define LMX_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LMX_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LMX_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
