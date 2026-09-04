//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderSettings.h
/// @brief Declares the editor-owned render knobs the shell writes onto every SceneView.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer.h"

namespace lmx::app {

/// Render knobs owned by the editor rather than by a scene, so switching scenes does not reset
/// them. The shell owns one of these and copies it onto every `render::SceneView` it builds; panels
/// borrow it to edit the same values, which is what keeps a viewport shortcut and an Inspector row
/// from becoming two parallel settings.
struct EditorRenderSettings {
    bool wireframe = false; ///< Draws the scene in wireframe.
    /// Shadow filtering kernel, applied by the scene pass.
    render::ShadowFilter shadowFilter = render::ShadowFilter::PCF;
    /// Manual exposure in stops; zero is unit exposure.
    float exposureEv = 0.0f;
    /// Whether the frame's render graph may let transients whose lifetimes do not overlap share
    /// memory. The picture is the same either way, so what this changes is the frame's transient
    /// high-water mark and its alias savings.
    bool poolingEnabled = true;

    /// Opt-in histogram auto exposure; off by default so manual exposure stays the default mode.
    bool autoExposureEnabled = false;
    float exposureLowPercentile = 50.0f;  ///< Lower metering percentile, in percent.
    float exposureHighPercentile = 95.0f; ///< Upper metering percentile, in percent.
    float exposureTargetGrey = 0.18f;     ///< Scene-linear luminance the metering aims at.
    float exposureEvMin = -8.0f;          ///< Lower clamp on the resolved automatic exposure.
    float exposureEvMax = 8.0f;           ///< Upper clamp on the resolved automatic exposure.
    float exposureCompensationEv = 0.0f;  ///< Bias applied to the resolved automatic exposure.

    bool bloomEnabled = true;    ///< Whether the bloom passes contribute to the display transform.
    float bloomThreshold = 1.0f; ///< Scene-linear luminance above which bloom is gathered.
    float bloomIntensity = 0.2f; ///< Weight of the bloom contribution.
};

} // namespace lmx::app
