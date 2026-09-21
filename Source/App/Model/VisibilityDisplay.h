//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDisplay.h
/// @brief Declares frame-scoped visibility lookup and Inspector diagnostic formatting.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Engine/Scene/Scene.h"
#include "Render/Visibility.h"
#include <rojoRHI/Device.h>

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {
/// Inspector category owning a formatted diagnostic field.
enum class VisibilityFieldGroup {
    Frame,      ///< Classification source and publication identity shared by diagnostic categories.
    Visibility, ///< Frustum decisions and CPU-oracle comparisons.
    Occlusion,  ///< Previous-frame history, pyramid and independent visibility reference.
    Submission, ///< Draw preparation, payload storage and classifier timings.
};
/// Read-only formatted diagnostic field.
struct VisibilityField {
    std::string label; ///< User-facing field name.
    std::string value; ///< Complete value for the retained frame.
    VisibilityFieldGroup group = VisibilityFieldGroup::Visibility; ///< Owning Inspector category.
};
/// Human-readable classification or pending state.
std::string_view visibilityStateName(render::VisibilityState state);
/// Human-readable bypass cause; None means an ordinary frustum test.
std::string_view visibilityReasonName(render::VisibilityReason reason);
/// Formats counts, commands, memory and matched CPU/GPU scopes from one frame.
std::vector<VisibilityField> visibilityFields(const render::VisibilityStatus& status,
                                              std::span<const rojoRHI::PassTiming> timings = {});
/// Formats one object's retained world bounds and classification.
std::vector<VisibilityField> objectVisibilityFields(const render::InstanceVisibility* visibility);
/// Maps saved declaration identities to immediate CPU or delayed GPU candidate results.
class VisibilityDisplay {
public:
    /// Records current identities after declaration; no scene pointer is retained.
    void observe(const engine::Scene& scene, const render::VisibilityStatus& status);
    /// Publishes retired classifications using identities saved at declaration, never current rows.
    void retire(const render::VisibilityStatus& status);
    /// Retains exact-frame timings for the displayed visibility publication.
    void observeTimings(uint64_t frame, std::span<const rojoRHI::PassTiming> timings);
    /// Current complete CPU or retired GPU publication, or an explicit pending declaration.
    const render::VisibilityStatus& status() const { return m_status; }
    /// Timings only when their frame matches the displayed publication.
    std::span<const rojoRHI::PassTiming> timings() const;
    /// Publishes Inspector counters and exact-frame timings together at the shared 250 ms cadence.
    /// First data, pending-to-ready and classifier changes publish immediately; scene clears reset
    /// the publication. The caller supplies a nonnegative monotonic clock in seconds.
    void publishReadings(double nowSeconds);
    /// Owned Inspector publication, independent of immediate object lookup and failure reporting.
    const render::VisibilityStatus& readingsStatus() const { return m_readingsStatus; }
    /// Owned timings matching readingsStatus(); empty when no exact-frame join was available.
    std::span<const rojoRHI::PassTiming> readingsTimings() const { return m_readingsTimings; }
    /// Formats the selected object's matching result and frame; missing identities stay pending.
    std::vector<VisibilityField> objectFields(engine::InstanceId id,
                                              uint64_t sceneGeneration) const;
    /// Clears identity mapping on a scene switch.
    void clear();
    /// Returns a candidate only when scene generation, frame and full object identity match.
    const render::InstanceVisibility* find(engine::InstanceId id,
                                           const render::VisibilityStatus& status,
                                           uint64_t sceneGeneration) const;

private:
    struct Entry {
        engine::InstanceId id;
        size_t candidate = 0;
    };
    struct Snapshot {
        uint64_t frame = 0;
        uint64_t generation = 0;
        std::vector<Entry> entries;
    };
    struct Timings {
        uint64_t frame = 0;
        std::vector<rojoRHI::PassTiming> passes;
    };
    std::deque<Snapshot> m_pending;
    std::deque<Timings> m_timings;
    render::VisibilityStatus m_status;
    render::VisibilityStatus m_readingsStatus;
    std::vector<rojoRHI::PassTiming> m_readingsTimings;
    double m_nextReadingsSeconds = 0.0;
    std::vector<Entry> m_entries;
    uint64_t m_frame = 0;
    uint64_t m_generation = 0;
};
} // namespace lmx::app
