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
    /// How fast auto exposure may brighten, in stops per second; meaningless while
    /// autoExposureEnabled is false.
    float exposureAdaptUpStopsPerSecond = 3.0f;
    /// How fast auto exposure may darken, in stops per second; meaningless while
    /// autoExposureEnabled is false. Slower than brightening by default.
    float exposureAdaptDownStopsPerSecond = 1.5f;

    bool bloomEnabled = true;    ///< Whether the bloom passes contribute to the display transform.
    float bloomThreshold = 1.0f; ///< Scene-linear luminance above which bloom is gathered.
    float bloomIntensity = 0.2f; ///< Weight of the bloom contribution.

    /// Temporal path (motion, history, reprojection); on by default (ADR 0013's forward note --
    /// the default flips in the App, not in the Renderer). False keeps the pre-temporal frame.
    bool temporalEnabled = true;
    /// Offsets rasterisation by the frame's Halton sample; meaningless while temporalEnabled is
    /// false. On by default alongside temporalEnabled.
    bool jitterEnabled = true;
    /// Which reconstruction the temporal path runs; meaningless while temporalEnabled is false.
    /// Native TAA by default -- the resolve pass runs and its output becomes the colour history.
    render::ReconstructionMode reconstruction = render::ReconstructionMode::NativeTaa;
    /// Diagnostic drawn over the display transform's own output.
    render::TemporalDebugView temporalDebugView = render::TemporalDebugView::Off;
    /// Whether the scene clock advances each frame. True by default so a scene with tracks plays
    /// on load, matching the pre-temporal frame loop's always-on behavior for a static scene.
    bool animationPlaying = true;
    /// Whether the fly camera is overridden by the scene's camera track when one exists. Has no
    /// effect while the right mouse button is held (the fly-camera latch takes over) or while the
    /// active scene has no camera track.
    bool followCameraTrack = true;

    /// Fraction of the output extent the scene rasterises at, within [kMinRenderScale, 1]. Edited
    /// directly by the manual slider while dynamic resolution is off; while it is on, the shell's
    /// `render::ResolutionController` writes this instead and the slider only displays it.
    float renderScale = 1.0f;
    /// Opt-in dynamic resolution, driving `renderScale` from measured GPU time against
    /// `gpuBudgetMilliseconds` (spec section 8); off by default, like auto exposure.
    bool dynamicResolutionEnabled = false;
    /// GPU time budget the dynamic-resolution controller steps `renderScale` against; meaningless
    /// while dynamicResolutionEnabled is false.
    float gpuBudgetMilliseconds = 16.0f;
};

} // namespace lmx::app
