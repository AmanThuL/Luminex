//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWatch.h
/// @brief Declares stable document-pair polling and sidecar grace decisions.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Filesystem observation of a loaded glTF, its buffer, and writer temporary directories.
struct FileStamp {
    uintmax_t gltfSize = 0;    ///< glTF byte count, zero when absent.
    uintmax_t bufferSize = 0;  ///< Companion byte count, zero when absent.
    int64_t gltfTime = 0;      ///< Filesystem modification clock ticks.
    int64_t bufferTime = 0;    ///< Filesystem modification clock ticks.
    bool saving = false;       ///< A writer's .lmx-save-*.tmp directory exists.
    uint64_t gltfInode = 0;    ///< glTF file serial number, zero when absent.
    uint64_t bufferInode = 0;  ///< Companion file serial number, zero when absent.
    uint64_t gltfDevice = 0;   ///< Device holding the glTF, zero when absent.
    uint64_t bufferDevice = 0; ///< Device holding the companion, zero when absent.
    bool operator==(const FileStamp&) const = default; ///< Exact observation equality.
    /// Whether both files are the same observation, whatever staging directory exists. A replaced
    /// file differs by serial number even when its size and modification time are preserved.
    bool samePair(const FileStamp& other) const;
};

/// Stamps a loaded pair with stat calls only while nothing moved. The glTF is re-read for its
/// buffer URI only when its own size, time or identity changes, and its directory is re-scanned
/// for staging directories when the directory's modification time or identity changes or while
/// the last scan found one: a staging directory removed within one modification-time tick leaves
/// the directory's stamp unchanged.
class DocumentProbe {
public:
    /// Observes gltf, the companion it names and any .lmx-save-*.tmp directory beside it. A glTF
    /// naming no buffer uses the adjacent .bin; fallbackBufferUri (relative to the glTF) names the
    /// companion only while the glTF cannot be opened.
    FileStamp observe(const std::filesystem::path& gltf,
                      const std::optional<std::string>& fallbackBufferUri = {});

private:
    struct Identity {
        uintmax_t size = 0;
        int64_t time = 0;
        uint64_t inode = 0;
        uint64_t device = 0;
        bool present = false;
        bool operator==(const Identity&) const = default;
    };
    static Identity identify(const std::filesystem::path& file);

    std::filesystem::path m_gltf;
    std::filesystem::path m_buffer;
    Identity m_document;
    Identity m_directory;
    bool m_bufferKnown = false;
    bool m_directoryKnown = false;
    bool m_saving = false;
};

/// Decides whether Save may replace the watched pair, immediately before writing. diskHash is the
/// pair's current hash, empty when it cannot be hashed. A hashable pair may be replaced when it is
/// the loaded document or one the operator rejected; an unhashable pair only while its stamp is
/// still the baseline the editor loaded or the operator rejected (baselineStamp). Otherwise
/// returns the disabled reason: the pair cannot be read, an external change has a card to review
/// (proposalPending), or its card has not appeared yet.
std::optional<std::string> saveOverwriteReason(std::string_view loadedHash,
                                               std::string_view diskHash, bool diskHashRejected,
                                               bool baselineStamp, bool proposalPending);

/// Next main-thread operation permitted by a stable poll.
enum class WatchDecision {
    Wait,    ///< Do nothing this poll.
    Hash,    ///< Hash the stable pair once.
    Sidecar, ///< Retry sidecar attribution within its four-second grace period.
};

/// Pure half-second polling model. Caller owns file I/O and uses one monotonic seconds clock.
class DocumentWatch {
public:
    /// Consecutive polls a staging directory defers the watch: ten polls, five seconds.
    static constexpr int kStagingPatiencePolls = 10;

    /// Adopts the current pair observation after load or an editor save. The staging-directory
    /// count describes the directory, not the pair, and survives.
    void reset(const FileStamp& loaded);
    /// Adopts an editor write only when its destination is the watched pair and its expected hash
    /// matches the current disk hash with equal non-saving stamps observed around that hash.
    /// Empty receipts and mismatches preserve all pending polling
    /// and sidecar attribution state unless documentAdopted is true. An uncertified adopted
    /// document invalidates the stamp baseline so the current pair is reviewed on stable polls.
    /// Callers must supply a fresh hash of the watched pair.
    bool adoptSave(const FileStamp& beforeHash, const FileStamp& afterHash,
                   const std::filesystem::path& watchedPath,
                   const std::filesystem::path& writtenPath, std::string_view writtenHash,
                   std::string_view currentHash, bool documentAdopted = false);
    /// Whether a filesystem observation is due on this clock; does not update the poll cursor.
    bool due(double seconds) const;
    /// Returns Hash after two equal changed observations, never more often than 0.5 s. A staging
    /// directory defers every decision for kStagingPatiencePolls consecutive polls; one that
    /// outlives them is a leftover of a failed rollback or a crash and is ignored from then on.
    /// The editor's own save is synchronous, so none is in progress during a poll.
    WatchDecision poll(const FileStamp& now, double seconds);
    /// Returns true once when a staging directory outlives the bound, for one Console warning;
    /// re-arms after a poll sees the directory gone.
    bool takeStagingWarning();
    /// Reports a pair hash and whether a sidecar names it; a mismatch waits up to four seconds.
    void hashed(std::string_view hash, bool sidecarMatches, double seconds);
    /// True once a matching sidecar arrives or the four-second grace expires.
    bool ready() const { return m_ready; }

private:
    FileStamp m_loaded;
    FileStamp m_observed;
    double m_lastPoll = -1.0;
    double m_graceStart = -1.0;
    std::string m_hash;
    int m_stagingPolls = 0;
    bool m_stagingWarned = false;
    bool m_stagingWarning = false;
    bool m_observedOnce = false;
    bool m_hashed = false;
    bool m_ready = false;
};

} // namespace lmx::app
