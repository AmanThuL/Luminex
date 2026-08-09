#include "Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace lmx::log {

//======================================================================================================================
void init() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    auto logger = spdlog::stdout_color_mt("lmx");
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::trace);
}

} // namespace lmx::log
