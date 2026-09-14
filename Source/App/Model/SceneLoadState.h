//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLoadState.h
/// @brief Declares queued scene-loading intent and persistent recoverable failure feedback.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Scene/SceneLibrary.h"

#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Holds a scene request until the next frame boundary and its failed result until another
/// request. The shell presents Loading before consuming the request and owns synchronous loading.
class SceneLoadState {
public:
    /// Queues the requested scene and clears the previous failure. Does not load or switch scenes.
    void request(scene::SceneId sceneId);
    /// Consumes a queued request once; a failed load never retries without a new explicit request.
    std::optional<scene::SceneId> consumeRequest();
    /// Stores a failed attempt's scene and causal error. The active scene remains owned by shell.
    void fail(scene::SceneId sceneId, std::string message);
    /// Last failed scene, retained so Retry can request exactly the same catalog entry.
    const std::optional<scene::SceneId>& failedScene() const { return m_failedScene; }
    /// Last failure text; empty before an attempt or after a new request begins.
    std::string_view failureMessage() const { return m_failureMessage; }

private:
    std::optional<scene::SceneId> m_requestedScene;
    std::optional<scene::SceneId> m_failedScene;
    std::string m_failureMessage;
};

} // namespace lmx::app
