//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDisplay.h
/// @brief Declares frame-scoped visibility lookup and Inspector diagnostic formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "RHI/Device.h"
#include "Render/Visibility.h"
#include "Scene/Scene.h"

#include <deque>
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
/// Human-readable bypass cause; None means an ordinary frustum test.
std::string_view visibilityReasonName(render::VisibilityReason reason);
/// Formats counts, commands, memory and matched CPU/GPU scopes from one frame.
std::vector<VisibilityField> visibilityFields(const render::VisibilityStatus& status,
                                              std::span<const rhi::PassTiming> timings = {});
/// Formats one object's retained world bounds and classification.
std::vector<VisibilityField> objectVisibilityFields(const render::InstanceVisibility* visibility);
/// Maps saved declaration identities to immediate CPU or delayed GPU candidate results.
class VisibilityDisplay {
public:
    /// Records current identities after declaration; no scene pointer is retained.
    void observe(const scene::Scene& scene, const render::VisibilityStatus& status);
    /// Publishes retired classifications using identities saved at declaration, never current rows.
    void retire(const render::VisibilityStatus& status);
    /// Retains exact-frame timings for the displayed visibility publication.
    void observeTimings(uint64_t frame, std::span<const rhi::PassTiming> timings);
    /// Current complete CPU or retired GPU publication, or an explicit pending declaration.
    const render::VisibilityStatus& status() const { return m_status; }
    /// Timings only when their frame matches the displayed publication.
    std::span<const rhi::PassTiming> timings() const;
    /// Formats the selected object's matching result and frame; missing identities stay pending.
    std::vector<VisibilityField> objectFields(scene::InstanceId id, uint64_t sceneGeneration) const;
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
    struct Snapshot {
        uint64_t frame = 0;
        uint64_t generation = 0;
        std::vector<Entry> entries;
    };
    struct Timings {
        uint64_t frame = 0;
        std::vector<rhi::PassTiming> passes;
    };
    std::deque<Snapshot> m_pending;
    std::deque<Timings> m_timings;
    render::VisibilityStatus m_status;
    std::vector<Entry> m_entries;
    uint64_t m_frame = 0;
    uint64_t m_generation = 0;
};
} // namespace lmx::app
