//----------------------------------------------------------------------------------------------------------------------
/// @file PassTiming.h
/// @brief Declares per-pass GPU timings and temporal upscaler support as Luminex values.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lmx::engine {

/// How long the GPU spent on one labeled pass, measured on the device timeline.
///
/// `label` is the label the pass was begun with, with the backend's unnamed-pass fallback
/// substituted for an empty one. `gpuMilliseconds` covers the whole pass, load and store actions
/// included, and is wall time on the GPU rather than a sum of shader costs: a pass that overlaps
/// another still reports its own span, so times across a frame may add up to more than the frame
/// took.
struct PassTiming {
    std::string label;            ///< Render-pass diagnostic label.
    double gpuMilliseconds = 0.0; ///< Measured GPU wall time in milliseconds.
};

/// The pass timings of one retired frame, in the order that frame began its passes.
struct PassTimingFrame {
    uint64_t frame = 0; ///< Frame the timings were measured on; zero when none yet.
    std::span<const PassTiming>
        passes; ///< Rows the context owns; valid until the next passTimings or beginFrame.
};

/// Device support for temporal reconstruction, fixed for the device lifetime.
struct UpscalerSupportInfo {
    bool available = false;     ///< Whether the device offers temporal reconstruction.
    float minInputScale = 0.0f; ///< Minimum content-to-output extent ratio, per axis.
    float maxInputScale = 0.0f; ///< Maximum content-to-output extent ratio, per axis.
    std::string_view name;      ///< Device-lifetime algorithm display name; empty when unavailable.
};

} // namespace lmx::engine
