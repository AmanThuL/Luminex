//----------------------------------------------------------------------------------------------------------------------
/// @file ActionFeedback.h
/// @brief Draws persistent diagnostic results and output-path actions.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/ActionResult.h"

namespace lmx::app {

/// Draws the last operation result and retains errors from clipboard/Finder
/// actions separately.
void drawActionFeedback(const char* id, ActionResult& result);

} // namespace lmx::app
