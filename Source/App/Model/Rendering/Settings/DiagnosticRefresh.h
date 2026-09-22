//----------------------------------------------------------------------------------------------------------------------
/// @file DiagnosticRefresh.h
/// @brief Shares the editor diagnostic snapshot publication interval.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::app {

/// Seconds between live diagnostic publications: four updates per second.
inline constexpr double kDiagnosticRefreshIntervalSeconds = 0.25;

} // namespace lmx::app
