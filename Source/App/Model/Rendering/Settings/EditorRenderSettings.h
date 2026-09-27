//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderSettings.h
/// @brief Declares the editor-owned render knobs the shell writes onto every SceneView.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/SceneView.h"

namespace lmx::app {

/// Render knobs owned by the editor rather than by a scene, so switching scenes does not reset
/// them. The shell owns one of these and copies it onto every `render::SceneView` it builds; panels
/// borrow it to edit the same values, which is what keeps a viewport shortcut and an Inspector row
/// from becoming two parallel settings.
struct EditorRenderSettings {
    /// Local-light path.
    engine::LocalLightMode localLightMode = engine::LocalLightMode::Clustered;
    engine::LightDebugView lightDebugView = engine::LightDebugView::Off; ///< Lighting diagnostic.
    bool lightCheck = false;       ///< Exact retired CPU/GPU light-list comparison, unscored.
    bool visibilityEnabled = true; ///< Conservatively culls camera-view instances.
    render::ClassifyMode classifyMode = render::ClassifyMode::Cpu; ///< Visibility classifier.
    bool occlusionEnabled = false;    ///< Previous-frame HZB rejection; GPU culling only.
    bool occlusionCheck = false;      ///< Independent reference raster, always unscored.
    int32_t hzbDebugLevel = -1;       ///< Current HZB level diagnostic; -1 returns to Final.
    bool showOcclusionBounds = false; ///< Editor-only rejected bounds, capped at 128.
    bool classifyCheck = false;       ///< Compare retired GPU work against the CPU oracle.
    render::SubmissionMode submission = render::SubmissionMode::Indirect; ///< Draw encoding mode.
    bool wireframe = false; ///< Draws the scene in wireframe.
    /// Whether the frame's render graph may let transients whose lifetimes do not overlap share
    /// memory. The picture is the same either way, so what this changes is the frame's transient
    /// high-water mark and its alias savings.
    bool poolingEnabled = true;

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
    /// Whether the fly camera is overridden by the scene's camera track when one exists. Has no
    /// effect while the right mouse button is held (the fly-camera latch takes over) or while the
    /// active scene has no camera track or the top transport is Stopped.
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
