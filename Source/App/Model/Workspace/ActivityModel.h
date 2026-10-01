//----------------------------------------------------------------------------------------------------------------------
/// @file ActivityModel.h
/// @brief Selects one current editor activity from existing work and policy observations.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Performance/MeasurementRun.h"
#include "App/Model/Workspace/Provenance.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lmx::app {

/// Snapshot of one measurement phase; counters describe that phase's submitted frames.
struct MeasureProgress {
    MeasurementState phase = MeasurementState::Idle; ///< Existing run state; terminal states hide.
    uint32_t done = 0;  ///< Submitted frames in this phase, in [0, total].
    uint32_t total = 0; ///< Planned frames; zero has no determinate progress. Ignored in Draining.
};

/// Observed controller change; the caller retains the latest observation without applying it here.
struct ScaleChange {
    float from = 1.0f; ///< Previous finite unitless render/output scale in [0.5, 1].
    float to = 1.0f;   ///< New finite unitless render/output scale in [0.5, 1].
    double at = 0.0;   ///< Finite monotonic timestamp in seconds, in the same clock as now.
};

/// Caller-owned snapshot; this model neither starts work nor retains references to these inputs.
struct ActivityInputs {
    std::optional<MeasureProgress> measure; ///< Run phase and its current frame counters.
    bool capturePending = false;            ///< Existing operator-requested capture is pending.
    std::optional<std::string>
        documentWork;                      ///< Owned current document-work label, absent when idle.
    std::optional<ScaleChange> controller; ///< Latest actual controller change, absent before one.
    /// Finite monotonic timestamp in seconds, in the same clock as controller.at.
    double now = 0.0;
};

/// Owned presentation of existing work; stoppable describes a capability, never executes Stop.
struct Activity {
    Actor actor;      ///< Initiator; document processing and controller work are System.
    std::string verb; ///< Owned phase or caller-supplied document-work label.
    std::optional<float>
        progress;        ///< Unitless [0, 1] phase fraction; absent for indeterminate work.
    bool stoppable;      ///< True only for an active measurement run.
    std::string tooltip; ///< Owned source description independent of input lifetimes.
};

/// Selects active Measure, pending capture, document work, then a controller change, in that order.
/// Measurement uses Warmup, Measuring, and Draining; other lifecycle states are inactive.
/// A controller change is visible for [at, at + 2 seconds); future or expired changes are hidden.
/// Returns no value when idle. Results own their strings; no work, settings, or clock is changed.
std::optional<Activity> currentActivity(const ActivityInputs& inputs);

} // namespace lmx::app
