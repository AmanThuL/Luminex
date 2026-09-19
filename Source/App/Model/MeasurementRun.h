//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementRun.h
/// @brief Declares deterministic measurement plans, strict frame joins, and JSON reports.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "RHI/Device.h"
#include "Render/LightingStatus.h"
#include "Render/Visibility.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::app {

/// Lifecycle of a measurement; failed joins terminate as cancelled with a diagnostic.
enum class MeasurementState {
    Idle,      ///< No run has started.
    Warmup,    ///< Submitting deterministic unsampled frames.
    Measuring, ///< Submitting frames retained in the report.
    Draining,  ///< All planned CPU samples exist; GPU joins remain.
    Complete,  ///< Every planned sample has retired.
    Cancelled, ///< Explicit cancellation or invalid evidence ended the run.
};

/// Frozen settings common to every frame in one run.
struct MeasurementPlan {
    uint32_t warmupFrames = 32;          ///< Deterministic unsampled frames.
    uint32_t measuredFrames = 256;       ///< Required fully joined samples.
    uint32_t width = 1280;               ///< Output pixels.
    uint32_t height = 720;               ///< Output pixels.
    uint32_t labOccluders = 0;           ///< Optional static slab count.
    uint32_t labInstances = 4096;        ///< Configured diagnostic workload size.
    std::string scene;                   ///< Catalog stable identifier.
    std::string temporal = "taa";        ///< Requested reconstruction.
    std::string submission = "indirect"; ///< Draw submission mode.
    std::string classify = "cpu";        ///< Requested classifier, cpu or gpu.
    bool occlusionEnabled = false;       ///< Previous-frame HZB rejection request.
    bool occlusionCheck = false;         ///< Independent reference; never scored.
    int32_t hzbDebugLevel = -1;          ///< Diagnostic pyramid mip; -1 means final.
    bool classifyCheck = false;          ///< CPU-oracle diagnostic, never scored.
    bool visibilityEnabled = true;       ///< Camera culling request.
    bool cameraTrack = true;             ///< Follow authored camera rail at 60 Hz.
    float renderScale = 1.0f;            ///< Requested reconstruction input scale.
    bool interactive = false;            ///< UI and presentation are included; always unscored.
    bool unscored = false;               ///< Explicit instrumentation override.
    std::string localLightMode = "clustered"; ///< Requested local-light path.
    bool localLightRig = false;               ///< Authored Sponza rig enabled for the complete run.
    uint32_t labLights = 256;                 ///< Authored LightLab base count.
    uint32_t labLightPile = 0;                ///< Authored LightLab saturation count.
    bool lightCheck = false;            ///< Exact CPU/GPU light-list diagnostic; never scored.
    std::string lightDebugView = "off"; ///< Lighting diagnostic view; never scored when active.
};

/// Actual host and executable provenance collected before the timed interval.
struct MeasurementProvenance {
    std::string device;         ///< RHI device name.
    std::string os;             ///< Operating-system release and kernel identity.
    std::string buildMode;      ///< Compile configuration.
    std::string executableHash; ///< SHA-256 of the running executable.
    std::vector<std::pair<std::string, std::string>> shaderHashes; ///< Runtime file SHA-256 pairs.
    std::vector<std::pair<std::string, std::string>> environment; ///< Actual instrumentation flags.
};

/// One deterministic frame to render; warmup frames have no report ordinal.
struct MeasurementFramePlan {
    uint32_t sequenceFrame = 0;      ///< Time is sequenceFrame / 60 seconds.
    std::optional<uint32_t> ordinal; ///< Zero-based measured sample, absent during warmup.
};

/// Declaration-time values owned by a particular submitted frame.
struct MeasurementCpuSample {
    uint64_t frameId = 0; ///< Actual RHI frame identity, never inferred from retirement order.
    uint32_t sequenceFrame = 0; ///< Exact frame from nextFrame().
    double classifyMs = 0;      ///< CPU visibility classification duration.
    double prepareMs = 0;       ///< CPU draw-list and argument preparation duration.
    double encodeMs = 0;   ///< From completed beginFrame through endFrame commit, excluding wait.
    double slotWaitMs = 0; ///< Duration of beginFrame, including its retired-timing resolution.
    uint32_t renderWidth = 0;  ///< Actual raster width before reconstruction.
    uint32_t renderHeight = 0; ///< Actual raster height before reconstruction.
    uint32_t outputWidth = 0;  ///< Actual final output width.
    uint32_t outputHeight = 0; ///< Actual final output height.
    float effectiveScale = 1;  ///< Effective reconstruction scale after capability clamps.
    uint32_t effectiveReconstruction = 0; ///< Numeric ReconstructionMode used by this frame.
    uint32_t vendorFallback = 0;          ///< Numeric VendorFallback, zero for no fallback.
    uint32_t candidates = 0;              ///< Scene-view candidates.
    uint32_t visible = 0;                 ///< Scene-view accepted entries, including bypasses.
    uint32_t rejected = 0;                ///< Scene-view rejected entries.
    uint32_t sceneCommands = 0;           ///< Issued scene commands, including sky where present.
    uint32_t shadowCommands = 0;          ///< Issued shadow commands.
    uint64_t tableBytes = 0;              ///< Allocated scene-table bytes across the three slots.
    uint64_t reservedListBytes = 0;  ///< Declaration row reservation; unaffected by retirement.
    uint64_t listBytes = 0;          ///< Valid row payload; GPU value resolves on exact retirement.
    uint64_t argumentBytes = 0;      ///< Indirect-argument payload bytes for this frame.
    uint64_t allocatedListBytes = 0; ///< Active list allocation across slots.
    uint64_t allocatedArgumentBytes = 0; ///< Active argument allocation across slots.
    uint64_t candidateBytes = 0;         ///< Candidate preparation bytes.
    uint64_t runBytes = 0;               ///< Run preparation bytes.
    uint64_t chunkBytes = 0;             ///< Chunk preparation bytes.
    uint64_t stateBytes = 0;             ///< GPU state storage bytes.
    uint64_t counterBytes = 0;           ///< GPU counter storage bytes.
    render::ClassifyMode classifyMode = render::ClassifyMode::Cpu; ///< Effective classifier.
    render::VisibilityCounters sceneCounters;  ///< Immediate CPU scene counts; GPU joins later.
    render::VisibilityCounters shadowCounters; ///< Immediate CPU shadow counts; GPU joins later.
    uint64_t transientBytes = 0; ///< Compiled transient physical bytes for this frame.
    std::vector<std::string>
        expectedPasses;              ///< Scheduled pass labels required in retirement order.
    render::LightingStatus lighting; ///< Owned declaration identity; counters join at retirement.
};

/// A sample becomes reportable only after matching nonempty GPU timings arrive.
struct MeasurementSample {
    MeasurementCpuSample cpu; ///< Declaration metrics; GPU list payload resolves on retirement.
    std::vector<rhi::PassTiming> passes; ///< Retired timings joined by exact frame id.
    std::optional<render::VisibilityStatus> visibility; ///< Exact-frame retired GPU counters.
    bool retired = false; ///< Distinguishes absent timings from zero-duration timings.
    std::optional<render::LightingStatus> lighting; ///< Exact-frame retired light counts and lists.
};

/// True for enabled or unrecognized validation/capture environment values; unset and "0" are off.
bool measurementEnvironmentInstrumented(const MeasurementProvenance& provenance);

/// Owns one deterministic run and rejects missing, duplicate, or mismatched frame samples.
class MeasurementRun {
public:
    /// Starts a fresh run, refusing invalid plans and scored instrumented environments.
    bool start(MeasurementPlan plan, MeasurementProvenance provenance);
    /// Current lifecycle state.
    MeasurementState state() const { return m_state; }
    /// True while warmup, sample submission, or GPU retirement remains pending.
    bool active() const;
    /// Next deterministic render, or no value after all planned frames have been submitted.
    std::optional<MeasurementFramePlan> nextFrame() const;
    /// Records one submitted frame in plan order, then advances the plan.
    bool recordCpu(MeasurementCpuSample sample);
    /// Joins a retired RHI publication; repeated identical publications are harmless.
    bool retire(uint64_t frameId, std::span<const rhi::PassTiming> passes);
    /// Joins GPU classifier counters by exact submitted frame, independently of timings.
    bool retireVisibility(const render::VisibilityStatus& status);
    /// Joins lighting counters by declaration frame and scene, independently of other retirements.
    bool retireLighting(const render::LightingStatus& status);
    /// Called once all submitted work has retired; any missing join invalidates the run.
    bool finishDrain();
    /// Cancels a run while retaining its partial evidence and explicit reason.
    void cancel(std::string reason = "Cancelled by user");
    /// Frozen run settings.
    const MeasurementPlan& plan() const { return m_plan; }
    /// All measured CPU samples, including those still awaiting retirement.
    std::span<const MeasurementSample> samples() const { return m_samples; }
    /// Empty on success, otherwise the first terminal failure or cancellation reason.
    const std::string& failure() const { return m_failure; }
    /// Schema-4 JSON, including partial evidence, exact scopes, provenance and completion status.
    std::string json() const;

private:
    void completeIfReady();
    MeasurementState m_state = MeasurementState::Idle;
    MeasurementPlan m_plan;
    MeasurementProvenance m_provenance;
    uint32_t m_submitted = 0;
    uint64_t m_firstFrameId = 0;
    uint64_t m_lastFrameId = 0;
    std::vector<MeasurementSample> m_samples;
    std::vector<render::LightingStatus> m_lightingDeclarations;
    std::string m_failure;
    std::string m_referenceFailure;
};

} // namespace lmx::app
