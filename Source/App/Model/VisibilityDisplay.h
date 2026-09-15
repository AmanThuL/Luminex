//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDisplay.h
/// @brief Declares frame-scoped visibility lookup and Inspector diagnostic formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Visibility.h"
#include "Scene/Scene.h"

#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {
/// Read-only formatted diagnostic field.
struct VisibilityField {
    std::string label; ///< User-facing field name.
    std::string value; ///< Complete value for the retained frame.
};
/// Human-readable classification or pending state.
std::string_view visibilityStateName(render::VisibilityState state);
/// Compact status badge that remains visible before a long object name.
std::string_view visibilityBadge(render::VisibilityState state);
/// Human-readable bypass cause; None means an ordinary frustum test.
std::string_view visibilityReasonName(render::VisibilityReason reason);
/// Formats counts, commands, memory and CPU scopes from one declaration.
std::vector<VisibilityField> visibilityFields(const render::VisibilityStatus& status);
/// Formats one object's retained world bounds and classification.
std::vector<VisibilityField> objectVisibilityFields(const render::InstanceVisibility* visibility);
/// Maps stable full scene identities to the renderer's last declared candidate list.
class VisibilityDisplay {
public:
    /// Records current identities after declaration; no scene pointer is retained.
    void observe(const scene::Scene& scene, const render::VisibilityStatus& status);
    /// Clears identity mapping on a scene switch.
    void clear();
    /// Returns a candidate only when scene generation, frame and full object identity match.
    const render::InstanceVisibility* find(scene::InstanceId id,
                                           const render::VisibilityStatus& status,
                                           uint64_t sceneGeneration) const;

private:
    struct Entry {
        scene::InstanceId id;
        size_t candidate = 0;
    };
    std::vector<Entry> m_entries;
    uint64_t m_frame = 0;
    uint64_t m_generation = 0;
};
} // namespace lmx::app
