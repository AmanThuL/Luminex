//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleLogSink.cpp
/// @brief Forwards neutral log callbacks into the bounded App console store.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/ConsoleLogSink.h"

#include "Core/Diagnostics/Assert.h"

#include <utility>

namespace lmx::app {

//======================================================================================================================
ConsoleLogSink::ConsoleLogSink(std::shared_ptr<ConsoleLog> log)
    : m_subscription([store = std::move(log)](const log::Message& message) {
          LMX_ASSERT(store != nullptr, "Console log capture requires a store");
          store->append(message.level, message.timestampMilliseconds, message.text);
      }) {}

} // namespace lmx::app
