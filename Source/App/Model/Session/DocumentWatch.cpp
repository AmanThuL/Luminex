//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWatch.cpp
/// @brief Waits for a stable document pair and one attributed proposal decision.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/DocumentWatch.h"

#include "Engine/Asset/Document/SceneDocument.h"

#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <utility>

namespace lmx::app {

//======================================================================================================================
bool FileStamp::samePair(const FileStamp& other) const {
    auto left = *this;
    left.saving = other.saving;
    return left == other;
}

//======================================================================================================================
DocumentProbe::Identity DocumentProbe::identify(const std::filesystem::path& file) {
    struct stat status{};
    if (::stat(file.c_str(), &status) != 0)
        return {};
    return {.size = static_cast<uintmax_t>(status.st_size),
            .time = static_cast<int64_t>(status.st_mtimespec.tv_sec) * 1'000'000'000 +
                    status.st_mtimespec.tv_nsec,
            .inode = static_cast<uint64_t>(status.st_ino),
            .device = static_cast<uint64_t>(status.st_dev),
            .present = true};
}

//======================================================================================================================
FileStamp DocumentProbe::observe(const std::filesystem::path& gltf,
                                 const std::optional<std::string>& fallbackBufferUri) {
    const auto document = identify(gltf);
    if (!m_bufferKnown || gltf != m_gltf || document != m_document) {
        m_buffer = gltf;
        m_buffer.replace_extension(".bin");
        std::ifstream input(gltf, std::ios::binary);
        if (input) {
            const std::string text(std::istreambuf_iterator<char>{input}, {});
            if (const auto candidate = asset::sceneDocumentBufferPath(text, gltf);
                candidate && *candidate)
                m_buffer = **candidate;
        } else if (fallbackBufferUri) {
            m_buffer = gltf.parent_path() / *fallbackBufferUri;
        }
        m_bufferKnown = document.present;
    }
    const auto directory = identify(gltf.parent_path());
    // A staging directory that came and went within one modification-time tick leaves the
    // directory's stamp where it was, so a positive answer is never trusted from the cache.
    if (!m_directoryKnown || m_saving || gltf != m_gltf || directory != m_directory) {
        m_saving = false;
        std::error_code error;
        for (std::filesystem::directory_iterator it(gltf.parent_path(), error), end;
             !error && it != end; it.increment(error)) {
            const auto name = it->path().filename().string();
            if (it->is_directory(error) && name.starts_with(".lmx-save-") &&
                name.ends_with(".tmp")) {
                m_saving = true;
                break;
            }
        }
        m_directoryKnown = directory.present;
    }
    m_gltf = gltf;
    m_document = document;
    m_directory = directory;
    const auto buffer = identify(m_buffer);
    return FileStamp{.gltfSize = document.size,
                     .bufferSize = buffer.size,
                     .gltfTime = document.time,
                     .bufferTime = buffer.time,
                     .saving = m_saving,
                     .gltfInode = document.inode,
                     .bufferInode = buffer.inode,
                     .gltfDevice = document.device,
                     .bufferDevice = buffer.device};
}

//======================================================================================================================
std::optional<std::string> saveOverwriteReason(std::string_view loadedHash,
                                               std::string_view diskHash, bool diskHashRejected,
                                               bool baselineStamp, bool proposalPending) {
    const bool reviewed =
        diskHash.empty() ? baselineStamp : diskHash == loadedHash || diskHashRejected;
    if (reviewed)
        return {};
    if (diskHash.empty())
        return "The scene file or its buffer cannot be read on disk";
    if (proposalPending)
        return "The scene file changed on disk; review its proposal first";
    return "The scene file changed on disk; its proposal will appear in the Session panel shortly";
}

//======================================================================================================================
void DocumentWatch::reset(const FileStamp& loaded) {
    m_loaded = loaded;
    m_observed = {};
    m_lastPoll = -1.0;
    m_graceStart = -1.0;
    m_hash.clear();
    m_observedOnce = false;
    m_hashed = false;
    m_ready = false;
}

//======================================================================================================================
bool DocumentWatch::adoptSave(const FileStamp& beforeHash, const FileStamp& afterHash,
                              const std::filesystem::path& watchedPath,
                              const std::filesystem::path& writtenPath,
                              std::string_view writtenHash, std::string_view currentHash,
                              bool documentAdopted) {
    if (writtenHash.empty() || writtenHash != currentHash || beforeHash != afterHash ||
        afterHash.saving || watchedPath.lexically_normal() != writtenPath.lexically_normal()) {
        if (documentAdopted)
            reset(FileStamp{});
        return false;
    }
    reset(afterHash);
    return true;
}

//======================================================================================================================
bool DocumentWatch::due(double seconds) const {
    return m_lastPoll < 0.0 || seconds - m_lastPoll >= 0.5;
}

//======================================================================================================================
WatchDecision DocumentWatch::poll(const FileStamp& now, double seconds) {
    if (!due(seconds))
        return WatchDecision::Wait;
    m_lastPoll = seconds;
    bool deferred = false;
    if (!now.saving) {
        m_stagingPolls = 0;
        m_stagingWarned = false;
        m_stagingWarning = false;
    } else if (m_stagingPolls < kStagingPatiencePolls) {
        ++m_stagingPolls;
        deferred = true;
    } else if (!m_stagingWarned) {
        m_stagingWarned = true;
        m_stagingWarning = true;
    }
    if (deferred || now.samePair(m_loaded)) {
        m_observedOnce = false;
        m_hashed = false;
        m_ready = false;
        return WatchDecision::Wait;
    }
    if (!m_observedOnce || !now.samePair(m_observed)) {
        m_observed = now;
        m_observedOnce = true;
        m_hashed = false;
        m_ready = false;
        m_graceStart = -1.0;
        return WatchDecision::Wait;
    }
    if (!m_hashed)
        return WatchDecision::Hash;
    if (m_ready)
        return WatchDecision::Wait;
    if (m_graceStart >= 0.0 && seconds - m_graceStart >= 4.0)
        m_ready = true;
    return WatchDecision::Sidecar;
}

//======================================================================================================================
bool DocumentWatch::takeStagingWarning() {
    return std::exchange(m_stagingWarning, false);
}

//======================================================================================================================
void DocumentWatch::hashed(std::string_view hash, bool sidecarMatches, double seconds) {
    if (!m_hashed || hash != m_hash) {
        m_hash = hash;
        m_graceStart = seconds;
    }
    m_hashed = true;
    m_ready = sidecarMatches || seconds - m_graceStart >= 4.0;
}

} // namespace lmx::app
