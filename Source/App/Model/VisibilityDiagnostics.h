//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDiagnostics.h
/// @brief Declares shared retired-visibility report and failure formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Visibility.h"
#include <string>
#include <string_view>

namespace lmx::app {
/// Stable classifier token shared by reports and controls.
std::string_view classifyModeName(render::ClassifyMode mode);
/// Retired counter/check fields, preserving their frame identity; pending GPU counts are null.
std::string visibilityDiagnosticsJson(const render::VisibilityStatus& status);
/// Empty for valid results; otherwise an actionable frame-labelled failure.
std::string visibilityFailure(const render::VisibilityStatus& status);
} // namespace lmx::app
