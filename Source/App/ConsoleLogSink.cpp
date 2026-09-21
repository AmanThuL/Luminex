//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleLogSink.cpp
/// @brief Forwards neutral log callbacks into the bounded App console store.
//----------------------------------------------------------------------------------------------------------------------

#include "App/ConsoleLogSink.h"

#include "Core/Diagnostics/Assert.h"

#include <utility>

namespace lmx::app {
namespace {

//======================================================================================================================
ConsoleSeverity consoleSeverity(log::Level level) {
    switch (level) {
    case log::Level::Trace:
        return ConsoleSeverity::Trace;
    case log::Level::Debug:
        return ConsoleSeverity::Debug;
    case log::Level::Info:
        return ConsoleSeverity::Info;
    case log::Level::Warning:
        return ConsoleSeverity::Warning;
    case log::Level::Error:
        return ConsoleSeverity::Error;
    case log::Level::Critical:
        return ConsoleSeverity::Critical;
    }
    return ConsoleSeverity::Info;
}

} // namespace

//======================================================================================================================
ConsoleLogSink::ConsoleLogSink(std::shared_ptr<ConsoleLog> log)
    : m_subscription([store = std::move(log)](const log::Message& message) {
          LMX_ASSERT(store != nullptr, "Console log capture requires a store");
          store->append(consoleSeverity(message.level), message.timestampMilliseconds,
                        message.text);
      }) {}

} // namespace lmx::app
