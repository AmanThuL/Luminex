//----------------------------------------------------------------------------------------------------------------------
/// @file Stopwatch.h
/// @brief Declares a monotonic timer for measuring elapsed milliseconds.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <chrono>

namespace lmx {

/// Monotonic timer for measuring elapsed time using steady_clock.
class Stopwatch {
public:
    /// Initializes the stopwatch, starting measurement immediately.
    Stopwatch() : m_start(std::chrono::steady_clock::now()) {}

    /// Resets the start time to the current moment.
    void restart() { m_start = std::chrono::steady_clock::now(); }

    /// Returns the elapsed time in milliseconds since construction or last restart.
    double elapsedMilliseconds() const {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now - m_start);
        return elapsed.count();
    }

    /// Measures to another stopwatch's captured start without sampling the clock again.
    /// The result is negative if that start precedes this one; restart changes a captured start.
    double elapsedMillisecondsUntil(const Stopwatch& boundary) const {
        return std::chrono::duration<double, std::milli>(boundary.m_start - m_start).count();
    }

private:
    std::chrono::steady_clock::time_point
        m_start; ///< Time point when stopwatch started or restarted.
};

} // namespace lmx
