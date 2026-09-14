//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLoadState.cpp
/// @brief Implements one-shot scene requests and persistent failure recovery state.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SceneLoadState.h"

#include <utility>

namespace lmx::app {

//======================================================================================================================
void SceneLoadState::request(scene::SceneId sceneId) {
    m_requestedScene = sceneId;
    m_failedScene.reset();
    m_failureMessage.clear();
}

//======================================================================================================================
std::optional<scene::SceneId> SceneLoadState::consumeRequest() {
    return std::exchange(m_requestedScene, std::nullopt);
}

//======================================================================================================================
void SceneLoadState::fail(scene::SceneId sceneId, std::string message) {
    m_failedScene = sceneId;
    m_failureMessage = std::move(message);
}

} // namespace lmx::app
