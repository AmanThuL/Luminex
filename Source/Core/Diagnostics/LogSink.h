//----------------------------------------------------------------------------------------------------------------------
/// @file LogSink.h
/// @brief Declares scoped log observation without exposing logging backend types.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace lmx::log {

/// Increasing importance of a log record delivered to a subscriber.
enum class Level : uint8_t {
    Trace,    ///< Detailed tracing.
    Debug,    ///< Debugging information.
    Info,     ///< Informational event.
    Warning,  ///< Recoverable warning.
    Error,    ///< Failed operation.
    Critical, ///< Critical failure.
};

/// One borrowed log record, valid only for the duration of the subscriber callback.
struct Message {
    Level level = Level::Info;         ///< Importance assigned by the producer.
    int64_t timestampMilliseconds = 0; ///< UTC milliseconds since the Unix epoch.
    std::string_view text; ///< Formatted message payload; copy it to retain it after the callback.
};

/// Adds an observer beside the existing terminal sinks. Construct after log::init and before
/// producers start; destroy after producers have stopped. Callbacks may run on producer threads
/// and must neither call logging recursively nor access thread-confined UI. Each subscription
/// serializes its callbacks. It owns the callback until destruction, removing only its own sink.
class SinkSubscription {
public:
    /// Installs a nonempty callback on the current project logger without changing terminal output.
    explicit SinkSubscription(std::function<void(const Message&)> callback);
    /// Removes this observer; the caller must already have stopped concurrent log producers.
    ~SinkSubscription();
    /// Subscriptions have unique ownership.
    SinkSubscription(const SinkSubscription&) = delete;
    /// Subscriptions cannot be copied.
    SinkSubscription& operator=(const SinkSubscription&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace lmx::log
