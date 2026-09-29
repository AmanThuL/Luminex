//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentDialogMailbox.h
/// @brief Declares an owned thread-safe native-dialog response mailbox.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace lmx::app {
/// Copied native-dialog response; a missing path and empty error means user cancellation.
struct DocumentDialogResult {
    std::optional<std::filesystem::path> path; ///< Owned path, never a borrowed native string.
    std::string error;                         ///< Native error, copied on the callback thread.
};
/// Main-thread begin/take with callback-thread post. Shared ownership can outlive a shell or
/// window; the callback retains only this mailbox and never accesses UI, scene, SDL window or app
/// state.
class DocumentDialogMailbox {
public:
    /// Starts one dialog; false while another callback or unconsumed result is outstanding.
    bool begin();
    /// Retains one completed response under the mutex; the native callback calls this once.
    void post(DocumentDialogResult result);
    /// Consumes a response once on the main thread and releases the pending-dialog gate.
    std::optional<DocumentDialogResult> take();
    /// True through callback completion until the owner consumes its queued response.
    bool pending() const;

private:
    mutable std::mutex m_mutex;
    bool m_pending = false;
    std::optional<DocumentDialogResult> m_result;
};
} // namespace lmx::app
