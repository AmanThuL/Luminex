//----------------------------------------------------------------------------------------------------------------------
/// @file EditorLighting.cpp
/// @brief Publishes retired lighting diagnostics to the editor and reports check failures.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/LightingDiagnostics.h"
#include "App/Shell/EditorShell.h"
#include "Core/Diagnostics/Log.h"

namespace lmx::app {

//======================================================================================================================
void EditorShell::retireLighting(render::Renderer& renderer) {
    for (const auto& status : renderer.takeRetiredLighting()) {
        m_lightingDisplay.retire(status);
        retireMeasurementLighting(status);
        const auto failure = lightingFailure(status);
        if (!failure.empty() && !m_lightingFailureLogged)
            LMX_LOG_ERROR("{}", failure);
        m_lightingFailureLogged = !failure.empty();
    }
}

} // namespace lmx::app
