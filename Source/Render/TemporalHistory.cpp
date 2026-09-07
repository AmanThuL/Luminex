//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalHistory.cpp
/// @brief Implements the ordered history reset derivation.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/TemporalHistory.h"

namespace lmx::render {
namespace {

//======================================================================================================================
bool sameExtents(const FrameExtents& a, const FrameExtents& b) {
    return a.renderWidth == b.renderWidth && a.renderHeight == b.renderHeight &&
           a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight;
}

} // namespace

//======================================================================================================================
HistoryResetReason deriveHistoryReset(const std::optional<FrameSignature>& previous,
                                      const FrameSignature& current, bool cameraCut) {
    if (!previous.has_value()) {
        return HistoryResetReason::FirstFrame;
    }
    if (!previous->temporalEnabled && current.temporalEnabled) {
        return HistoryResetReason::TemporalEnabled;
    }
    if (previous->sceneGeneration != current.sceneGeneration) {
        return HistoryResetReason::SceneChanged;
    }
    if (!sameExtents(previous->extents, current.extents)) {
        return HistoryResetReason::ExtentChanged;
    }
    if (previous->fovY != current.fovY || previous->nearZ != current.nearZ) {
        return HistoryResetReason::ProjectionChanged;
    }
    if (cameraCut) {
        return HistoryResetReason::CameraCut;
    }
    return HistoryResetReason::None;
}

//======================================================================================================================
std::string_view historyResetReasonName(HistoryResetReason reason) {
    switch (reason) {
    case HistoryResetReason::None:
        return "None";
    case HistoryResetReason::FirstFrame:
        return "FirstFrame";
    case HistoryResetReason::TemporalEnabled:
        return "TemporalEnabled";
    case HistoryResetReason::SceneChanged:
        return "SceneChanged";
    case HistoryResetReason::ExtentChanged:
        return "ExtentChanged";
    case HistoryResetReason::ProjectionChanged:
        return "ProjectionChanged";
    case HistoryResetReason::CameraCut:
        return "CameraCut";
    }
    return "Unknown";
}

} // namespace lmx::render
