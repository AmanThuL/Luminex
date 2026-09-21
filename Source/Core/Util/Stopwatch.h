//----------------------------------------------------------------------------------------------------------------------
/// @file Stopwatch.h
/// @brief Declares a high-resolution timer for measuring elapsed milliseconds.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <chrono>

namespace lmx {

/// High-resolution timer for measuring elapsed time using steady_clock.
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

private:
    std::chrono::steady_clock::time_point
        m_start; ///< Time point when stopwatch started or restarted.
};

} // namespace lmx
