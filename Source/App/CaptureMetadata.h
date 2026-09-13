//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureMetadata.h
/// @brief Declares JSON serialization for deterministic offscreen captures.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/AppOptions.h"
#include "Render/DisplayDomain.h"

#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// Serializes a schema-v2 sequence manifest at full floating-point precision. Records are
/// JSON objects from captureRecordJson; display describes the renderer's final output. The
/// offscreen capture has no composited UI. Incomplete runs retain any supplied failure reason.
std::string captureManifestJson(const AppOptions& options, std::string_view device,
                                const render::DisplayDomain& display, uint32_t width,
                                uint32_t height, bool cameraTrack,
                                const std::vector<std::string>& records, bool complete,
                                std::string_view failure = {});

/// Serializes one sequence frame's camera, settings, reconstruction, and history state.
/// Frame is the zero-based simulation frame at 60 Hz; ordinal excludes unsaved warmup frames.
std::string captureRecordJson(uint32_t ordinal, uint32_t frame, const render::Camera& camera,
                              const render::SceneView& view, const render::TemporalStatus& status,
                              std::string_view filename, TemporalMode requested);

/// Serializes the lmx:frame PNG payload for a screenshot or sequence frame. frameCount is the
/// requested number of saved frames (total rendered frames for a screenshot); simulationFrame
/// is the zero-based rendered frame including warmup. Device and status describe the actual run.
std::string captureFrameMetadataJson(engine::SceneId scene, uint32_t frameCount,
                                     uint32_t simulationFrame, TemporalMode requested,
                                     render::TemporalDebugView debugView, float renderScale,
                                     const render::TemporalStatus& status, std::string_view device);

} // namespace lmx::app
