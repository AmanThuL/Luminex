//----------------------------------------------------------------------------------------------------------------------
/// @file LogSink.cpp
/// @brief Adapts project log records to scoped neutral observer callbacks.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Diagnostics/LogSink.h"

#include "Core/Diagnostics/Assert.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

namespace lmx::log {
namespace {

class ObserverSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    //==================================================================================================================
    explicit ObserverSink(std::function<void(const Message&)> callback);

private:
    //==================================================================================================================
    void sink_it_(const spdlog::details::log_msg& message) override;
    void flush_() override;
    std::function<void(const Message&)> m_callback;
};

//======================================================================================================================
ObserverSink::ObserverSink(std::function<void(const Message&)> callback)
    : m_callback(std::move(callback)) {}

//======================================================================================================================
void ObserverSink::sink_it_(const spdlog::details::log_msg& message) {
    Level level = Level::Info;
    switch (message.level) {
    case spdlog::level::trace:
        level = Level::Trace;
        break;
    case spdlog::level::debug:
        level = Level::Debug;
        break;
    case spdlog::level::info:
        level = Level::Info;
        break;
    case spdlog::level::warn:
        level = Level::Warning;
        break;
    case spdlog::level::err:
        level = Level::Error;
        break;
    case spdlog::level::critical:
        level = Level::Critical;
        break;
    case spdlog::level::off:
    case spdlog::level::n_levels:
        return;
    }
    const auto time =
        std::chrono::duration_cast<std::chrono::milliseconds>(message.time.time_since_epoch())
            .count();
    m_callback({level, time, {message.payload.data(), message.payload.size()}});
}

//======================================================================================================================
void ObserverSink::flush_() {}

} // namespace

struct SinkSubscription::Impl {
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<ObserverSink> sink;
};

//======================================================================================================================
SinkSubscription::SinkSubscription(std::function<void(const Message&)> callback)
    : m_impl(std::make_unique<Impl>()) {
    LMX_ASSERT(static_cast<bool>(callback), "Log observer callback must not be empty");
    m_impl->logger = spdlog::default_logger();
    m_impl->sink = std::make_shared<ObserverSink>(std::move(callback));
    m_impl->logger->sinks().push_back(m_impl->sink);
}

//======================================================================================================================
SinkSubscription::~SinkSubscription() {
    auto& sinks = m_impl->logger->sinks();
    std::erase(sinks, m_impl->sink);
}

} // namespace lmx::log
