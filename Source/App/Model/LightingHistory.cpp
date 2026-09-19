//----------------------------------------------------------------------------------------------------------------------
/// @file LightingHistory.cpp
/// @brief Implements local-light edit invalidation of temporal history.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/LightingHistory.h"

namespace lmx::app {

//======================================================================================================================
bool lightingChangeNeedsHistoryReset(render::LocalLightMode previous,
                                     render::LocalLightMode current, uint32_t liveLightCount,
                                     bool contentChanged) {
    return contentChanged || (liveLightCount > 0 && previous != current);
}

} // namespace lmx::app
