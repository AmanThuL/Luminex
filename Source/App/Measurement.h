//----------------------------------------------------------------------------------------------------------------------
/// @file Measurement.h
/// @brief Declares measurement front-end helpers shared by headless and interactive runs.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/AppOptions.h"
#include "App/Model/MeasurementRun.h"
#include "Render/CompiledFrameRecord.h"
#include "Render/Visibility.h"
#include "Scene/SceneTableStats.h"

namespace lmx::app {
/// Collects actual runtime file hashes, device, OS and instrumentation environment before timing.
MeasurementProvenance collectMeasurementProvenance(const rhi::Device& device);
/// Allocated instance, mesh, material and local-light row bytes across all three current slots.
uint64_t measurementTableBytes(const scene::SceneTableStats& stats);
/// Captures declaration diagnostics without borrowing the renderer's next-frame storage.
MeasurementCpuSample measurementCpuSample(uint32_t sequenceFrame, double waitMs, double encodeMs,
                                          const render::VisibilityStatus& visibility,
                                          const scene::SceneTableStats& tables,
                                          const render::CompiledFrameRecord& record, bool hasSky,
                                          const render::TemporalStatus& temporal,
                                          const render::LightingStatus& lighting);
/// Renders a deterministic offscreen run and writes a complete or explicitly failed JSON report.
int runMeasurement(const AppOptions& options);
} // namespace lmx::app
