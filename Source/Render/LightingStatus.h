//----------------------------------------------------------------------------------------------------------------------
/// @file LightingStatus.h
/// @brief Declares frame-keyed local-light selection and retired cluster statistics.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Lights/LocalLight.h"
#include "Render/LightClusterCheck.h"
#include <memory>

namespace lmx::render {

/// One declaration's lighting context. GPU counters become valid only after retirement; the
/// frame and scene keys remain those of the declaration, including across later mode switches.
struct LightingStatus {
    /// Requested path for this frame.
    engine::LocalLightMode requested = engine::LocalLightMode::Clustered;
    /// Off when the frame has no live local light.
    engine::LocalLightMode effective = engine::LocalLightMode::Off;
    uint64_t frameNumber = 0;      ///< Device frame this context belongs to.
    uint64_t sceneGeneration = 0;  ///< Scene activation generation from the frame's view.
    uint32_t liveLightCount = 0;   ///< Number of live lights at declaration, excluding free rows.
    bool checkEnabled = false;     ///< Exact CPU/GPU list comparison requested at declaration.
    LightClusterCheckResult check; ///< Exact retired list and counter differences.
    std::shared_ptr<const LightClusterCheckFrame> checkFrame; ///< Owned diagnostic evidence only.
    /// Pending checks cannot pass; disabled checks impose no comparison requirement.
    bool checkPassed() const { return !checkEnabled || (isRetired && check.passed()); }
    bool isRetired = false; ///< Counters and listBytes describe completed GPU work when true.
    LightClusterCounters counters;   ///< Zero for Off/Direct, or the retired clustered totals.
    uint64_t listBytes = 0;          ///< Defined index prefix in bytes for this completed frame.
    uint64_t allocatedListBytes = 0; ///< Physical index allocation across all three retained slots.
};

} // namespace lmx::render
