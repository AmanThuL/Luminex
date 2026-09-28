//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWorkflow.h
/// @brief Declares document action sequencing without windowing or filesystem work.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Scenes/SceneLibrary.h"

namespace lmx::app {

/// Document operations shared by menus, shortcuts and window close.
enum class DocumentAction {
    Open,        ///< Choose and open a document.
    OpenCatalog, ///< Open the specified catalog identity.
    Save,        ///< Save over the active path.
    SaveAs,      ///< Choose another path and adopt the saved document.
    Revert,      ///< Reload the active file.
    Quit         ///< Leave after resolving unsaved changes and any dialog.
};
/// User choice before an operation would discard unsaved edits.
enum class ConfirmChoice {
    Save,    ///< Save first, aborting the pending action on failure.
    Discard, ///< Continue without saving; the executor discards only after successful replacement.
    Cancel   ///< Keep the current document and cancel the requested action.
};
/// The owner supplies confirmation, a native path, or execution at these boundaries.
enum class WorkflowStep {
    Idle,       ///< No outstanding request.
    Confirm,    ///< Waiting for a Save/Discard/Cancel choice.
    ChoosePath, ///< Waiting for the native dialog response.
    Ready       ///< Ready for one takeWork, or executing until completion.
};
/// A single operation; saveFirst must succeed before its destructive action may execute.
struct PendingDocumentWork {
    DocumentAction action = DocumentAction::Open; ///< Requested final operation.
    std::optional<scenes::SceneId> target;        ///< Catalog or explicit scene identity.
    std::optional<std::filesystem::path> path;    ///< Selected native-dialog path.
    bool saveFirst = false;                       ///< Abort the operation if Save fails.
};

/// Pure document state machine. It owns intents, never a scene or a filesystem operation.
/// takeWork hands out a request once; the owner reports completion after all work or any failure.
class DocumentWorkflow {
public:
    /// Refreshes current canonical dirty and transport state. Does not clear pending user work.
    void setContext(bool dirty, bool stopped, bool measuring);
    /// Queues a request when idle. Quit coalesces while a path dialog or operation is outstanding
    /// and is reconsidered after that response; other overlapping requests return false.
    bool request(DocumentAction action, std::optional<scenes::SceneId> target = {});
    /// Resolves confirmation. Save is ignored unless stopped and not measuring. For Open, Save
    /// work must complete successfully before the native chooser becomes available.
    void confirm(ConfirmChoice choice);
    /// Completes the pending dialog, with null for cancellation or failure. A selected path runs
    /// before queued Quit; cancellation resumes Quit immediately against the current dirty state.
    void pathChosen(std::optional<std::filesystem::path> path);
    /// Returns ready work exactly once, leaving the workflow busy until complete is called.
    /// An accepted Save before Open first emits a standalone Save, retaining the Open intent.
    std::optional<PendingDocumentWork> takeWork();
    /// Finishes issued work. Failure retains dirty state and aborts its destructive continuation.
    /// Success refreshes dirty only for Save/Save As/save-first; the owner supplies actual state.
    /// A successful preparatory Save resumes the retained Open at ChoosePath without saving again.
    void complete(bool success);
    /// Current user or executor boundary.
    WorkflowStep step() const { return m_step; }
    /// Current dirty value, never cleared by Cancel, Discard intent or failure.
    bool dirty() const { return m_dirty; }
    /// Requested action while busy; used to choose the native dialog kind.
    std::optional<DocumentAction> action() const;
    /// Save, Save As and Revert require stopped transport and no active measurement.
    static std::optional<std::string> unavailableReason(DocumentAction action, bool stopped,
                                                        bool measuring);

private:
    bool savingBeforeOpen() const;
    void advance();
    void clear();
    void resumeQuit();
    WorkflowStep m_step = WorkflowStep::Idle;
    std::optional<PendingDocumentWork> m_pending;
    bool m_dirty = false;
    bool m_stopped = true;
    bool m_measuring = false;
    bool m_issued = false;
    bool m_quitQueued = false;
};
} // namespace lmx::app
