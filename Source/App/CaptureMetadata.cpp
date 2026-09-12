//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureMetadata.cpp
/// @brief Serializes offscreen capture domains, settings, and per-frame state.
//----------------------------------------------------------------------------------------------------------------------

#include "App/CaptureMetadata.h"

#include "Engine/SceneAnimation.h"

#include <format>
#include <iomanip>
#include <sstream>

namespace lmx::app {

namespace {

//======================================================================================================================
std::string jsonString(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += std::format("\\u{:04x}", c);
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + "\"";
}

//======================================================================================================================
std::string_view captureModeName(TemporalMode mode) {
    switch (mode) {
    case TemporalMode::Off:
        return "off";
    case TemporalMode::Raw:
        return "raw";
    case TemporalMode::Taa:
        return "taa";
    case TemporalMode::Vendor:
        return "metalfx";
    }
    return "unknown";
}

} // namespace

//======================================================================================================================
std::string captureManifestJson(const AppOptions& options, std::string_view device,
                                const render::DisplayDomain& display, uint32_t width,
                                uint32_t height, bool cameraTrack,
                                const std::vector<std::string>& records, bool complete,
                                std::string_view failure) {
    std::ostringstream file;
    file << std::setprecision(17)
         << "{\n\"schemaVersion\":2,\"complete\":" << (complete ? "true" : "false")
         << ",\"scene\":" << jsonString(engine::sceneIdString(options.initialScene))
         << ",\"failure\":" << jsonString(failure) << ",\"device\":" << jsonString(device)
         << ",\"requestedMode\":" << jsonString(captureModeName(options.temporal))
         << ",\"width\":" << width << ",\"height\":" << height
         << ",\"fps\":60,\"warmup\":" << options.warmup << ",\"frameCount\":" << options.frames
         << ",\"renderScale\":" << options.renderScale
         << ",\"debugView\":" << static_cast<int>(options.temporalView)
         << ",\"cameraTrack\":" << (cameraTrack ? "true" : "false")
         << ",\"display\":" << render::toJson(display)
         << ",\"container\":" << jsonString(captureFormatName(options.captureFormat))
         << ",\"ui\":{\"composited\":false},\"dynamicResolution\":false,\"frames\":[\n";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i != 0)
            file << ",\n";
        file << records[i];
    }
    file << "\n]}\n";
    return file.str();
}

//======================================================================================================================
std::string captureRecordJson(uint32_t ordinal, uint32_t frame, const render::Camera& camera,
                              const render::SceneView& view, const render::TemporalStatus& status,
                              std::string_view filename, TemporalMode requested) {
    std::ostringstream out;
    out << std::setprecision(17) << "{\"ordinal\":" << ordinal << ",\"simulationFrame\":" << frame
        << ",\"timeSeconds\":" << static_cast<double>(frame) / engine::kAnimationBakeRate
        << ",\"file\":" << jsonString(filename) << ",\"camera\":{\"position\":["
        << camera.position.x << ',' << camera.position.y << ',' << camera.position.z
        << "],\"yaw\":" << camera.yaw << ",\"pitch\":" << camera.pitch
        << ",\"fovY\":" << camera.fovY << ",\"nearZ\":" << camera.nearZ
        << ",\"farZ\":" << camera.farZ << "}"
        << ",\"effectiveMode\":"
        << jsonString(requested == TemporalMode::Off ? "off"
                      : status.reconstruction == render::ReconstructionMode::VendorTemporal
                          ? "metalfx"
                      : status.reconstruction == render::ReconstructionMode::NativeTaa ? "taa"
                                                                                       : "raw")
        << ",\"fallback\":" << static_cast<int>(status.vendorFallback)
        << ",\"vendorName\":" << jsonString(status.vendorName)
        << ",\"renderWidth\":" << status.extents.renderWidth
        << ",\"renderHeight\":" << status.extents.renderHeight
        << ",\"effectiveScale\":" << status.renderScale << ",\"jitterIndex\":" << status.jitterIndex
        << ",\"jitterEnabled\":" << (view.temporal.jitterEnabled ? "true" : "false")
        << ",\"historyAge\":" << status.historyAge
        << ",\"lastResetReason\":" << jsonString(render::historyResetReasonName(status.lastReset))
        << ",\"lastResetFrame\":" << status.lastResetFrame
        << ",\"vendorReset\":" << (status.vendorReset ? "true" : "false")
        << ",\"exposureEv\":" << view.exposureEv
        << ",\"autoExposure\":" << (view.autoExposureEnabled ? "true" : "false")
        << ",\"bloom\":" << (view.bloomEnabled ? "true" : "false")
        << ",\"bloomThreshold\":" << view.bloomThreshold
        << ",\"bloomIntensity\":" << view.bloomIntensity << ",\"shadowFilter\":\"pcf\"}";
    return out.str();
}

//======================================================================================================================
std::string captureFrameMetadataJson(engine::SceneId scene, uint32_t frameCount,
                                     uint32_t simulationFrame, TemporalMode requested,
                                     render::TemporalDebugView debugView, float renderScale,
                                     const render::TemporalStatus& status,
                                     std::string_view device) {
    std::ostringstream out;
    out << std::setprecision(17) << "{\"scene\":" << jsonString(engine::sceneIdString(scene))
        << ",\"frameCount\":" << frameCount << ",\"simulationFrame\":" << simulationFrame
        << ",\"requestedMode\":" << jsonString(captureModeName(requested)) << ",\"effectiveMode\":"
        << jsonString(requested == TemporalMode::Off ? "off"
                      : status.reconstruction == render::ReconstructionMode::VendorTemporal
                          ? "metalfx"
                      : status.reconstruction == render::ReconstructionMode::NativeTaa ? "taa"
                                                                                       : "raw")
        << ",\"fallback\":" << static_cast<int>(status.vendorFallback)
        << ",\"renderScale\":" << renderScale << ",\"debugView\":" << static_cast<int>(debugView)
        << ",\"device\":" << jsonString(device) << "}";
    return out.str();
}

} // namespace lmx::app
