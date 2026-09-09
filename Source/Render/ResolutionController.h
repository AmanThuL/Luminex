//----------------------------------------------------------------------------------------------------------------------
/// @file ResolutionController.h
/// @brief Declares a pure, device-free render-scale controller driven by measured GPU time.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Temporal.h"

#include <array>
#include <cstdint>

namespace lmx::render {

/// Tuning for `ResolutionController`. `minScale`/`maxScale` default to `Temporal.h`'s
/// `kMinRenderScale`/`kMaxRenderScale`, the same range the reconstruction accepts.
struct ResolutionControllerSettings {
    float budgetMilliseconds = 16.0f; ///< Measured GPU time a frame must not exceed.
    float headroom = 0.15f; ///< target = budget * (1 - headroom); the deadband is [target, budget].
    float minScale = kMinRenderScale; ///< Defaults to `Temporal.h`'s `kMinRenderScale`.
    float maxScale = kMaxRenderScale; ///< Defaults to `Temporal.h`'s `kMaxRenderScale`.
    float maxStep = 0.05f;            ///< The largest change of scale one decision makes.
    uint32_t settleFrames = 6; ///< Declared frames after a change whose samples are not judged.
    uint32_t overBudgetSamples = 2;   ///< Consecutive over-budget samples before stepping down.
    uint32_t underTargetSamples = 12; ///< Consecutive under-target samples before stepping up.
};

/// Steps a render scale against a GPU time budget from retired-frame measurements. Pure and
/// device-free: it owns no GPU resources and knows nothing of the App or the render graph. A
/// caller declares each frame's scale before submitting it, then reports that frame's measured
/// GPU time once it retires; attribution is by frame number, so a late-arriving measurement is
/// judged against the scale its frame actually ran at.
class ResolutionController {
public:
    /// Constructs a controller at `settings.maxScale` with every counter and the ring cleared.
    explicit ResolutionController(ResolutionControllerSettings settings = {});

    /// The controller's current settings.
    const ResolutionControllerSettings& settings() const;

    /// Replaces the settings and clamps `scale()` into the new `[minScale, maxScale]` range.
    /// Counters, the ring and the settle window are left as they are.
    void setSettings(const ResolutionControllerSettings& settings);

    /// The render scale the caller should apply to the next declared frame.
    float scale() const;

    /// Seeds `scale()` (clamped into range) and clears every counter, the ring and the settle
    /// window, so the next judged sample is treated as if the controller had just started.
    void reset(float scale);

    /// Records that `frame` is being declared at the current `scale()`, so a later `observe()`
    /// for it can be attributed to the scale it actually ran at.
    void declared(uint64_t frame);

    /// Reports `frame`'s measured GPU time -- the sum of its retired pass timings, not wall time.
    /// The sample is judged only when `frame` was named by `declared()`, it ran at the current
    /// `scale()`, and it was declared after the settle window opened by the last change; an
    /// unjudged sample leaves every counter untouched. A negative `gpuMilliseconds` is treated as
    /// zero. Answers whether `scale()` changed.
    bool observe(uint64_t frame, double gpuMilliseconds);

private:
    struct DeclaredFrame {
        uint64_t frame = 0;
        float scale = 0.0f;
        int64_t sequence = 0;
    };

    static constexpr std::size_t kRingSize = 8;

    void clearAttribution();

    ResolutionControllerSettings m_settings;
    float m_scale;
    std::array<DeclaredFrame, kRingSize> m_ring{};
    int64_t m_sequence = 0;
    int64_t m_changedAtSequence = 0;
    uint32_t m_overBudgetCount = 0;
    uint32_t m_underTargetCount = 0;
};

} // namespace lmx::render
