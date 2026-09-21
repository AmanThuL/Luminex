//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleLogSink.h
/// @brief Declares the App-owned lifetime of editor console log capture.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/ConsoleLog.h"
#include "Core/Diagnostics/LogSink.h"

#include <memory>

namespace lmx::app {

/// Captures project logs into an App-owned store while preserving terminal output. Construct after
/// log::init, before device/scene initialization; destroy after all worker log producers stop.
/// The callback retains the shared store and never accesses ImGui or a panel.
class ConsoleLogSink {
public:
    /// Registers the non-null store for the duration of this object.
    explicit ConsoleLogSink(std::shared_ptr<ConsoleLog> log);

private:
    log::SinkSubscription m_subscription;
};

} // namespace lmx::app
