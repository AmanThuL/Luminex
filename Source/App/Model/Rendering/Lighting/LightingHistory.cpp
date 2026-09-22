//----------------------------------------------------------------------------------------------------------------------
/// @file LightingHistory.cpp
/// @brief Implements local-light edit invalidation of temporal history.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Rendering/Lighting/LightingHistory.h"

namespace lmx::app {

//======================================================================================================================
bool lightingChangeNeedsHistoryReset(engine::LocalLightMode previous,
                                     engine::LocalLightMode current, uint32_t liveLightCount,
                                     bool contentChanged) {
    return contentChanged || (liveLightCount > 0 && previous != current);
}

} // namespace lmx::app
