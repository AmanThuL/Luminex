//----------------------------------------------------------------------------------------------------------------------
/// @file EditorVisibility.cpp
/// @brief Joins renderer declarations and retired visibility into editor diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "App/EditorShell.h"

#include "App/Model/VisibilityDiagnostics.h"
#include "Core/Diagnostics/Log.h"

namespace lmx::app {

//======================================================================================================================
void EditorShell::observeDeclaration(const render::Renderer& renderer, uint64_t frameId) {
    observeDeclaredTemporal(m_temporalState, m_settings, renderer.temporalStatus(), frameId);
    m_visibilityDisplay.observe(m_session.scene(), renderer.visibilityStatus());
    m_lightingDisplay.observe(renderer.lightingStatus());
    if (m_measurement.active()) {
        m_measurementVisibility = renderer.visibilityStatus();
        m_measurementLighting = renderer.lightingStatus();
        m_measurementTemporal = renderer.temporalStatus();
        if (renderer.width() != m_measurement.plan().width ||
            renderer.height() != m_measurement.plan().height ||
            m_settings.visibilityEnabled != m_measurement.plan().visibilityEnabled) {
            m_measurement.cancel("Viewport or rendering settings changed during measurement");
        }
    }
}

//======================================================================================================================
void EditorShell::retireVisibility(render::Renderer& renderer) {
    for (const auto& status : renderer.takeRetiredVisibility()) {
        m_visibilityDisplay.retire(status);
        const auto failure = visibilityFailure(status);
        if (!failure.empty() && !m_visibilityFailureLogged)
            LMX_LOG_ERROR("{}", failure);
        m_visibilityFailureLogged = !failure.empty();
        if (m_measurement.active())
            m_measurement.retireVisibility(status);
    }
    finishMeasurementPlayback();
}

} // namespace lmx::app
