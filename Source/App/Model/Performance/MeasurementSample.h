//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementSample.h
/// @brief Declares the renderer-to-measurement sample handoff shared by all front ends.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Performance/MeasurementRun.h"
#include "Engine/Scene/SceneTableStats.h"
#include "Render/Graph/CompiledFrameRecord.h"
#include "Render/Renderer/SceneView.h"

namespace lmx::app {
/// Allocated instance, mesh, material and local-light row bytes across all three current slots.
uint64_t measurementTableBytes(const engine::SceneTableStats& stats);
/// Captures declaration diagnostics without borrowing the renderer's next-frame storage. GPU
/// declarations supply enabled/disabled population from row flags; derived counts await retirement.
MeasurementCpuSample measurementCpuSample(uint32_t sequenceFrame, double waitMs, double encodeMs,
                                          const render::VisibilityStatus& visibility,
                                          const engine::SceneTableStats& tables,
                                          const render::CompiledFrameRecord& record, bool hasSky,
                                          const render::TemporalStatus& temporal,
                                          const render::LightingStatus& lighting);
} // namespace lmx::app
