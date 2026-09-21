//----------------------------------------------------------------------------------------------------------------------
/// @file DiagnosticLegend.cpp
/// @brief Describes the actual diagnostic encodings and authored lab fixtures.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/DiagnosticLegend.h"

namespace lmx::app {

//======================================================================================================================
DiagnosticLegend diagnosticLegend(render::TemporalDebugView view) {
    using enum render::TemporalDebugView;
    switch (view) {
    case Off:
        return {"Final", "Scene image after exposure, bloom and the SDR display transform."};
    case MotionVectors:
        return {
            "Motion vectors",
            "Red = horizontal, green = vertical motion; +X right, +Y down. "
            "UV delta (current - previous): channel = 0.5 + 8 x delta. "
            "Grey = zero; displayed channels clip beyond +/-0.0625 UV. Magenta = invalid motion."};
    case ReprojectionError:
        return {
            "Reprojection error",
            "Grey = 4 x luminance of absolute history/current pre-exposed linear-color difference. "
            "Black = zero; white = 0.25 or more. Blue = no comparison (invalid motion, "
            "outside history or no history). Exposure changes also contribute."};
    case ReprojectedHistory:
        return {
            "Reprojected history",
            "Exposure-corrected history before neighborhood clipping, shown through PBR Neutral "
            "and sRGB. Blue = no valid reprojected history. This is a color image, not an error "
            "scale."};
    case RejectionMask:
        return {
            "History rejection",
            "Black = accepted; blue = outside history; magenta = invalid/reset; red = disoccluded; "
            "yellow = fully reactive. Added 0.5 green marks neighborhood clipping (clamped at 1)."};
    case BlendWeight:
        return {
            "Current-frame blend weight",
            "Greyscale alpha: black = 0, white = 1 (current frame only). This is the blend alpha "
            "before inverse-luminance reweighting, not the final normalized contribution."};
    case HistoryAge:
        return {"History age",
                "Greyscale = floor(age) / 16, clamped to white. Black = 0 frames; "
                "white = 16 or more accumulated frames. Rejected pixels restart at 1 frame."};
    }
    return {"Final", "Scene image."};
}

//======================================================================================================================
DiagnosticLegend diagnosticLegend(engine::LightDebugView view) {
    using enum engine::LightDebugView;
    switch (view) {
    case Off:
        return {"Final", "Scene image after exposure, bloom and the SDR display transform."};
    case Count:
        return {"Light count", "Stored local-light count in the visible surface's froxel; "
                               "0 black; 1-4 blue; 5-16 green; 17-64 yellow; 65-128 red."};
    case Overflow:
        return {"Light overflow",
                "Magenta marks truncated froxels over the final image at 25% brightness. "
                "Unmarked froxels retained every candidate light."};
    case Missed:
        return {"Missed lights",
                "Red = an absent light reaches this surface in an untruncated "
                "froxel (list error). Yellow = an absent light reaches it in a "
                "truncated froxel (expected loss). Black = no missing contribution."};
    }
    return {"Final", "Scene image."};
}

//======================================================================================================================
std::string_view diagnosticUnavailableReason(render::TemporalDebugView view, bool temporalEnabled,
                                             render::ReconstructionMode effective) {
    if (view == render::TemporalDebugView::Off) {
        return {};
    }
    if (!temporalEnabled) {
        return "Enable temporal inputs to inspect motion and history diagnostics.";
    }
    if (effective == render::ReconstructionMode::VendorTemporal &&
        render::nativeOnlyTemporalView(view)) {
        return "Unavailable with device reconstruction: this view needs native accumulation "
               "internals. "
               "Select Native TAA to inspect it.";
    }
    return {};
}

//======================================================================================================================
std::string_view diagnosticModeNote(render::TemporalDebugView view,
                                    render::ReconstructionMode effective) {
    if (effective != render::ReconstructionMode::Raw) {
        return {};
    }
    using enum render::TemporalDebugView;
    switch (view) {
    case RejectionMask:
    case BlendWeight:
        return "Raw does not accumulate history: this view shows a zero placeholder, not a native "
               "measurement.";
    case ReprojectedHistory:
        return "Raw does not produce reprojected-history color: blue means no data in this mode.";
    case HistoryAge:
        return "Raw stores coverage in alpha, not accumulation age. This view shows "
               "floor(coverage) / 16.";
    default:
        return {};
    }
}

//======================================================================================================================
std::string_view labDescription(scenes::SceneId sceneId) {
    const auto id = scenes::sceneIdString(sceneId);
    if (id == "material-lab") {
        return "MaterialLab: 5 x 5 spheres. Roughness 0.05 -> 1 left to right; metallic 0 -> 1 "
               "bottom to top "
               "in the initial view. Texture/normal probes are at X=14; depth probes at X=28. "
               "Hold RMB + A/D to move between lanes.";
    }
    if (id == "light-lab") {
        return "LightLab: a deterministic grid of unshadowed point and spot lights, orbiting "
               "lights and a 12-second camera rail. The optional colocated pile forces overflow; "
               "clear it to return to the measured grid workload.";
    }
    if (id == "temporal-lab") {
        return "TemporalLab: compare rotating and static cubes, orbiting geometry, thin moving "
               "poles, "
               "invalid motion and a pulsing emissive sign. Playback and its camera rail repeat "
               "every 24 s.";
    }
    return {};
}

} // namespace lmx::app
