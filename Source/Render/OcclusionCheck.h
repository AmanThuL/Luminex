//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionCheck.h
/// @brief Declares independent reference visibility observations and recovery streaks.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace lmx::render {
/// One candidate's independently rasterized visible coverage and production rejection.
struct OcclusionCheckObservation {
    uint64_t instanceIdentity = 0; ///< Full generational identity; zero uses row within the scene.
    uint32_t instanceRow = 0;      ///< Declaration-time stable row.
    uint64_t visiblePixels = 0; ///< Reference pixels surviving independent depth and alpha tests.
    bool occluded = false;      ///< Production classification explicitly rejected by occlusion.
};
/// A visible rejected instance and its consecutive missing-frame count.
struct OcclusionMissingInstance {
    uint64_t instanceIdentity = 0;  ///< Declaration-time generational identity.
    uint32_t instanceRow = 0;       ///< Declaration-time stable row.
    uint64_t visiblePixels = 0;     ///< Missing reference pixels in this frame.
    uint32_t consecutiveFrames = 0; ///< Adjacent checked frames with visible rejected coverage.
};
/// Independent geometry correctness result for one retired frame, never an image-accuracy score.
struct OcclusionCheckResult {
    bool enabled = false;          ///< The independent reference was declared for this frame.
    bool strict = false;           ///< Unchanged view and coverage require zero missing instances.
    uint64_t frameNumber = 0;      ///< Device frame owning all observations.
    uint64_t sceneGeneration = 0;  ///< Scene identity owning all row and instance tokens.
    uint32_t visibleInstances = 0; ///< Candidates with at least one reference pixel.
    uint32_t falselyRejectedInstances = 0; ///< Reference-visible candidates classified Occluded.
    uint64_t falselyRejectedPixels = 0;    ///< Sum of independent pixels for rejected instances.
    uint64_t invalidReferencePixels = 0;   ///< Nonzero IDs absent from declaration candidates.
    uint32_t unmatchedCandidates = 0;      ///< Reference candidates missing a production state.
    uint32_t maximumMissingStreak = 0;     ///< Largest current consecutive missing-frame count.
    std::vector<OcclusionMissingInstance> missing; ///< Per-instance missing coverage and streaks.
    /// Strict frames fail immediately; moving frames allow one missing frame only.
    bool passed() const {
        return invalidReferencePixels == 0 && unmatchedCandidates == 0 &&
               (!strict || falselyRejectedInstances == 0) && maximumMissingStreak < 2;
    }
};
/// CPU-only recovery tracker; scene changes, frame gaps and visibility recovery clear streaks.
class OcclusionCheckHistory {
public:
    /// Consumes observations in increasing frame order; repeated or skipped frames start fresh.
    OcclusionCheckResult observe(uint64_t frameNumber, uint64_t sceneGeneration, bool strict,
                                 std::span<const OcclusionCheckObservation> observations);
    /// Discards all history after a check-mode reset.
    void reset();

private:
    struct Key {
        uint64_t identity;
        uint32_t row;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key& key) const {
            return std::hash<uint64_t>{}(key.identity) ^ std::hash<uint32_t>{}(key.row);
        }
    };
    uint64_t m_frameNumber = 0;
    uint64_t m_sceneGeneration = 0;
    std::unordered_map<Key, uint32_t, KeyHash> m_streaks;
};
} // namespace lmx::render
