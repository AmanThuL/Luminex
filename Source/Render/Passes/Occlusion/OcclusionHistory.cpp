//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionHistory.cpp
/// @brief Derives global invalidation before any previous-view rejection.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/OcclusionHistory.h"
#include <cmath>
#include <glm/geometric.hpp>
namespace lmx::render {
//======================================================================================================================
OcclusionInvalidReason occlusionHistoryReason(const std::optional<OcclusionFrameFacts>& source,
                                              const OcclusionFrameFacts& current,
                                              bool previouslyEnabled) {
    using R = OcclusionInvalidReason;
    if (!current.enabled)
        return R::Disabled;
    if (current.wireframe)
        return R::Wireframe;
    if (!source)
        return R::NoSource;
    if (!previouslyEnabled)
        return R::PreviouslyDisabled;
    if (source->frameNumber + 1 != current.frameNumber)
        return R::SourceGap;
    if (source->sceneGeneration != current.sceneGeneration)
        return R::SceneChanged;
    if (source->outputWidth != current.outputWidth || source->outputHeight != current.outputHeight)
        return R::OutputExtentChanged;
    if (current.cameraCut)
        return R::CameraCut;
    for (uint32_t i = 0; i < 3; ++i)
        if (!std::isfinite(source->cameraPosition[i]) ||
            !std::isfinite(current.cameraPosition[i]) || !std::isfinite(source->cameraForward[i]) ||
            !std::isfinite(current.cameraForward[i]))
            return R::NonFiniteCamera;
    if (glm::distance(source->cameraPosition, current.cameraPosition) > kOcclusionCutDistance)
        return R::CameraTranslation;
    constexpr float threshold = 0.984807753012208f;
    if (glm::dot(source->cameraForward, current.cameraForward) < threshold)
        return R::CameraRotation;
    if (source->coverageEpoch != current.coverageEpoch)
        return R::CoverageChanged;
    return R::None;
}
//======================================================================================================================
const char* occlusionInvalidReasonName(OcclusionInvalidReason reason) {
    switch (reason) {
    case OcclusionInvalidReason::None:
        return "Valid";
    case OcclusionInvalidReason::Disabled:
        return "Disabled";
    case OcclusionInvalidReason::Wireframe:
        return "Wireframe";
    case OcclusionInvalidReason::NoSource:
        return "No source";
    case OcclusionInvalidReason::PreviouslyDisabled:
        return "Previously disabled";
    case OcclusionInvalidReason::SourceGap:
        return "Source frame gap";
    case OcclusionInvalidReason::SceneChanged:
        return "Scene changed";
    case OcclusionInvalidReason::OutputExtentChanged:
        return "Output extent changed";
    case OcclusionInvalidReason::CameraCut:
        return "Camera cut";
    case OcclusionInvalidReason::CameraTranslation:
        return "Camera translation";
    case OcclusionInvalidReason::CameraRotation:
        return "Camera rotation";
    case OcclusionInvalidReason::CoverageChanged:
        return "Coverage changed";
    case OcclusionInvalidReason::NonFiniteCamera:
        return "Nonfinite camera";
    }
    return "Unknown";
}
} // namespace lmx::render
