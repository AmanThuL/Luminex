//----------------------------------------------------------------------------------------------------------------------
/// @file FrameRecordRing.h
/// @brief Declares the App's retention of compiled frame records and their retired GPU timings.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/CompiledFrameRecord.h"
#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lmx::app {

/// Editor measurements captured when a frame is declared, before GPU retirement. These values
/// belong to that frame even when a later resize, scene switch or mode change has already occurred.
struct FrameMetricsMetadata {
    uint64_t contextEpoch = 0;         ///< App revision separating incompatible scene/mode samples.
    uint32_t objectCount = 0;          ///< Scene object count submitted for this frame.
    uint32_t drawCount = 0;            ///< Draw items submitted for this frame.
    uint32_t viewportLogicalWidth = 0; ///< Image width in logical UI points.
    uint32_t viewportLogicalHeight = 0; ///< Image height in logical UI points.
    uint32_t renderPixelWidth = 0;  ///< Active rendering width before reconstruction, in pixels.
    uint32_t renderPixelHeight = 0; ///< Active rendering height before reconstruction, in pixels.
    uint32_t outputPixelWidth = 0;  ///< Scene output width after reconstruction, in pixels.
    uint32_t outputPixelHeight = 0; ///< Scene output height after reconstruction, in pixels.
};

/// One retained frame: what compilation decided, and what the GPU later measured of it.
///
/// The two arrive separately and several frames apart -- the record when the frame is declared, the
/// timings once that frame retires -- which is the whole reason they need joining rather than
/// simply being reported together.
struct RetainedFrame {
    render::CompiledFrameRecord record; ///< The frame as compilation described it.
    /// Per-pass GPU times, in the order the frame began its passes, or empty until they retire.
    /// Copied rather than borrowed: rojoRHI::Device::passTimings() is invalidated by the next
    /// beginFrame(), and this outlives several of those.
    std::vector<rojoRHI::PassTiming> timings;
    std::optional<FrameMetricsMetadata> metrics; ///< Declaration-time editor context, if supplied.
    bool timed = false; ///< Whether `timings` has been joined; distinguishes it from a timed frame
                        ///< that ran no passes.
};

/// A fixed-size ring of the most recent compiled frames, and the join between them and the GPU
/// timings the RHI publishes.
///
/// It is App-owned because retention is a policy question -- how far back an observer can look --
/// and the graph declares a frame without knowing there will be another. Nothing here reads the GPU
/// or the graph; it holds values and matches them by frame number.
class FrameRecordRing {
public:
    /// Frames retained at once.
    ///
    /// One more than the three that can be in flight, because a timing publication names a frame
    /// that has already *retired* -- the frame just past the in-flight window. Retaining exactly
    /// three would evict each frame's record on the beginFrame() that publishes its timings, which
    /// is the one moment the record is needed.
    static constexpr size_t kCapacity = 4;

    /// Retains one compiled frame, evicting the oldest once kCapacity are held. Frames are expected
    /// in increasing frameId order, which is the order a frame loop declares them in.
    /// Optional metrics are copied into the same retained entry, so delayed timings cannot be
    /// paired with current live settings. Capture callers may omit editor-only metadata.
    void retain(render::CompiledFrameRecord record,
                std::optional<FrameMetricsMetadata> metrics = std::nullopt);

    /// Joins timings measured on `frameId` to the retained record of that frame, and answers
    /// whether one was still retained. `frameId` of zero -- the RHI's "nothing published yet" --
    /// joins nothing. Joining the same frame twice replaces the timings rather than accumulating
    /// them, so a loop may call this every frame without checking whether the publication has
    /// advanced.
    bool joinTimings(uint64_t frameId, std::span<const rojoRHI::PassTiming> timings);

    /// The newest retained frame that has timings joined, or null while none has retired yet. This
    /// is the frame an observer displays: the newest one for which both halves are known.
    const RetainedFrame* newestTimedFrame() const;

    /// The retained frame with this number, or null if it was never retained or has been evicted.
    const RetainedFrame* find(uint64_t frameId) const;

    /// Frames currently retained, at most kCapacity.
    size_t size() const { return m_frames.size(); }

private:
    // Oldest first, so eviction is from the front and the newest is the back. At four entries a
    // vector is cheaper and clearer than an index-wrapped buffer.
    std::vector<RetainedFrame> m_frames;
};

} // namespace lmx::app
