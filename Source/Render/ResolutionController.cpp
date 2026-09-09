//----------------------------------------------------------------------------------------------------------------------
/// @file ResolutionController.cpp
/// @brief Implements the GPU-time resolution controller's hysteresis and attribution.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/ResolutionController.h"

#include <algorithm>
#include <cmath>

namespace lmx::render {
namespace {

// A settle window is open whenever a judged entry's sequence is at most `changedAtSequence +
// settleFrames`, where `changedAtSequence` is the declaration count at the moment of the change,
// so the window covers exactly `settleFrames` frames declared after it. Before any change has
// happened, that window must never trigger regardless of
// `settleFrames`, so the starting and post-reset value of `changedAtSequence` is pinned far below
// any sequence number a ring entry can carry.
constexpr int64_t kNoChangeSequence = INT64_MIN / 2;

//======================================================================================================================
float clampToRange(float value, const ResolutionControllerSettings& settings) {
    return std::clamp(value, settings.minScale, settings.maxScale);
}

} // namespace

//======================================================================================================================
ResolutionController::ResolutionController(ResolutionControllerSettings settings)
    : m_settings(settings), m_scale(clampToRange(settings.maxScale, settings)) {
    clearAttribution();
}

//======================================================================================================================
const ResolutionControllerSettings& ResolutionController::settings() const {
    return m_settings;
}

//======================================================================================================================
void ResolutionController::setSettings(const ResolutionControllerSettings& settings) {
    m_settings = settings;
    m_scale = clampToRange(m_scale, m_settings);
}

//======================================================================================================================
float ResolutionController::scale() const {
    return m_scale;
}

//======================================================================================================================
void ResolutionController::reset(float scale) {
    m_scale = clampToRange(scale, m_settings);
    clearAttribution();
}

//======================================================================================================================
void ResolutionController::clearAttribution() {
    m_ring = {};
    m_sequence = 0;
    m_changedAtSequence = kNoChangeSequence;
    m_overBudgetCount = 0;
    m_underTargetCount = 0;
}

//======================================================================================================================
void ResolutionController::declared(uint64_t frame) {
    ++m_sequence;
    m_ring[m_sequence % kRingSize] = DeclaredFrame{frame, m_scale, m_sequence};
}

//======================================================================================================================
bool ResolutionController::observe(uint64_t frame, double gpuMilliseconds) {
    const DeclaredFrame* entry = nullptr;
    for (const DeclaredFrame& candidate : m_ring) {
        if (candidate.sequence != 0 && candidate.frame == frame) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr || entry->scale != m_scale ||
        entry->sequence <= m_changedAtSequence + static_cast<int64_t>(m_settings.settleFrames)) {
        return false;
    }

    const float target = m_settings.budgetMilliseconds * (1.0f - m_settings.headroom);
    // A timestamp pair can come back out of order and report a negative duration; taking the square
    // root of a negative ratio below would poison the scale with a NaN it never recovers from.
    const double sample = std::max(gpuMilliseconds, 0.0);
    const float measured = static_cast<float>(sample);

    if (sample <= m_settings.budgetMilliseconds && measured >= target) {
        m_overBudgetCount = 0;
        m_underTargetCount = 0;
        return false;
    }

    const float desired = m_scale * std::sqrt(target / measured);
    float nextScale = m_scale;
    bool changed = false;

    if (sample > m_settings.budgetMilliseconds) {
        m_underTargetCount = 0;
        ++m_overBudgetCount;
        if (m_overBudgetCount >= m_settings.overBudgetSamples) {
            nextScale = clampToRange(std::max(desired, m_scale - m_settings.maxStep), m_settings);
            changed = nextScale != m_scale;
        }
    } else {
        m_overBudgetCount = 0;
        ++m_underTargetCount;
        if (m_underTargetCount >= m_settings.underTargetSamples) {
            nextScale = clampToRange(std::min(desired, m_scale + m_settings.maxStep), m_settings);
            changed = nextScale != m_scale;
        }
    }

    if (changed) {
        m_scale = nextScale;
        m_overBudgetCount = 0;
        m_underTargetCount = 0;
        // The declaration count now, not the retired sample's sequence: the window is the frames
        // declared after the change, and the frames already in flight when a late sample lands
        // ran at the scale this decision just replaced.
        m_changedAtSequence = m_sequence;
    }
    return changed;
}

} // namespace lmx::render
