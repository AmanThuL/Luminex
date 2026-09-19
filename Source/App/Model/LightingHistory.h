//----------------------------------------------------------------------------------------------------------------------
/// @file LightingHistory.h
/// @brief Declares temporal invalidation policy for local-light rendering edits.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/LocalLight.h"

#include <cstdint>

namespace lmx::app {

/// Keeps history for equivalent zero-live-light modes; content changes and mode changes with
/// live lights invalidate it. The count is the number of enabled lights after the edit; callers
/// report content changes separately so removing the last light still invalidates history.
bool lightingChangeNeedsHistoryReset(render::LocalLightMode previous,
                                     render::LocalLightMode current, uint32_t liveLightCount,
                                     bool contentChanged);

} // namespace lmx::app
