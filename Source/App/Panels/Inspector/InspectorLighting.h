//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorLighting.h
/// @brief Declares local-light and clustered-lighting Inspector sections.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Engine/Scene/SceneIds.h"

namespace lmx::app {

struct InspectorPanelContext;

/// Edits one complete light identity through SceneSession; stale identities show an explanation.
void drawLocalLightSection(const InspectorPanelContext& context, engine::LightId id);

} // namespace lmx::app
