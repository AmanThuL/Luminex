//----------------------------------------------------------------------------------------------------------------------
/// @file LightingDiagnostics.h
/// @brief Declares frame-keyed lighting report and failure formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Render/LightingStatus.h"
#include <string>
#include <string_view>
namespace lmx::app {
/// Stable lowercase token shared by CLI, captures and measurement reports.
std::string_view localLightModeName(engine::LocalLightMode mode);
/// Stable lowercase post-display diagnostic token.
std::string_view lightDebugViewName(engine::LightDebugView view);
/// Serializes exact declaration identity with retired counters or null pending counters.
std::string lightingDiagnosticsJson(const render::LightingStatus& status);
/// Returns an actionable failure for an incomplete or inconsistent retired result.
std::string lightingFailure(const render::LightingStatus& status);
} // namespace lmx::app
