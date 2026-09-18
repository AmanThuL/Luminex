//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionHistory.h
/// @brief Declares source-frame coverage and camera validity for depth evidence.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <glm/vec3.hpp>
#include <optional>
namespace lmx::render {
/// Maximum continuous per-frame camera rotation, in degrees.
inline constexpr float kOcclusionCutAngle = 10.0f;
/// Maximum continuous per-frame camera translation, in world metres.
inline constexpr float kOcclusionCutDistance = 1.0f;
/// First applicable reason a previous depth source cannot reject geometry.
enum class OcclusionInvalidReason {
    None,                ///< Adjacent source with unchanged scene coverage.
    Disabled,            ///< Occlusion is disabled on this frame.
    Wireframe,           ///< Filled depth evidence cannot reject wireframe candidates.
    NoSource,            ///< No completed declaration has produced a source.
    PreviouslyDisabled,  ///< Previous declared frame did not build depth evidence.
    SourceGap,           ///< Source does not belong to exactly the preceding device frame.
    SceneChanged,        ///< Scene identity changed.
    OutputExtentChanged, ///< Output allocation changed.
    CameraCut,           ///< Caller signalled a camera discontinuity.
    CameraTranslation,   ///< Translation exceeded the fixed threshold.
    CameraRotation,      ///< Rotation exceeded the fixed threshold.
    CoverageChanged,     ///< Any scene coverage changed.
    NonFiniteCamera      ///< Camera pose cannot be evaluated safely.
};
/// Immutable facts captured with one declared frame, independent of temporal mode.
struct OcclusionFrameFacts {
    uint64_t frameNumber = 0;          ///< Owning device frame.
    uint64_t sceneGeneration = 0;      ///< Scene identity.
    uint64_t coverageEpoch = 0;        ///< Scene coverage revision.
    uint32_t outputWidth = 0;          ///< Output width in texels.
    uint32_t outputHeight = 0;         ///< Output height in texels.
    glm::vec3 cameraPosition{};        ///< World-space camera origin in metres.
    glm::vec3 cameraForward{0, 0, -1}; ///< Unit world-space view direction.
    bool enabled = false;              ///< This frame builds occlusion evidence.
    bool wireframe = false;            ///< Wireframe rendering disables depth rejection.
    bool cameraCut = false;            ///< Explicit caller discontinuity event.
};
/// Returns the first invalidation cause; None is the sole valid outcome.
OcclusionInvalidReason occlusionHistoryReason(const std::optional<OcclusionFrameFacts>& source,
                                              const OcclusionFrameFacts& current,
                                              bool previouslyEnabled);
/// Returns a stable readable diagnostic name.
const char* occlusionInvalidReasonName(OcclusionInvalidReason reason);
} // namespace lmx::render
