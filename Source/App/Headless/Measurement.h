//----------------------------------------------------------------------------------------------------------------------
/// @file Measurement.h
/// @brief Declares measurement front-end helpers shared by headless and interactive runs.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Options/AppOptions.h"
#include "App/Model/Performance/MeasurementSample.h"
#include "Engine/Scene/SceneTableStats.h"
#include "Render/Graph/CompiledFrameRecord.h"
#include "Render/Passes/Visibility/Visibility.h"

namespace lmx::app {
/// Collects actual runtime file hashes, device, OS and instrumentation environment before timing.
MeasurementProvenance collectMeasurementProvenance(const rojoRHI::Device& device);
/// Renders a deterministic offscreen run and writes a complete or explicitly failed JSON report.
int runMeasurement(const AppOptions& options);
} // namespace lmx::app
