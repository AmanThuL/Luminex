//----------------------------------------------------------------------------------------------------------------------
/// @file RendererInternal.h
/// @brief Shares private derived frame state and resource imports between renderer units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Renderer/Renderer.h"

#include <memory>
#include <optional>

namespace lmx::render {

void seedReplacement(TextureStateTable& states, const engine::GpuTexture& previous,
                     const engine::GpuTexture& replacement, TextureUse initial);

// Values derived once before any frame resources or passes are declared.
struct RendererFrameState {
    bool temporalEnabled;
    ReconstructionSelection selection;
    ReconstructionMode reconstruction;
    bool vendorTemporal;
    float renderScale;
    FrameExtents extents;
    bool upscaled;
    FrameSignature signature;
    HistoryResetReason resetReason;
    bool historyValid;
    FrameExtents previousExtents;
    uint32_t slot;
    uint32_t previousSlot;
    bool nativeTaa;
    TemporalDebugView debugView;
    glm::vec2 jitterPixels;
    CameraFrameState cameraState;
    CameraFrameState previousCamera;
    ShadowMatrices shadow;
};

// Graph-local imports, in the original declaration order including exposure seeding.
struct RendererFrameImports {
    GraphTexture shadowMap;
    GraphTexture sceneColor;
    GraphTexture displayColor;
    GraphTexture sceneDepth;
    GraphTexture motionTargetHandle;
    GraphTexture reactiveTargetHandle;
    GraphTexture previousDepth;
    GraphTexture colorSlotImport;
    GraphTexture historyImport;
    GraphBuffer exposureCurrent;
    std::vector<GraphBuffer> sceneBuffers;
    std::optional<GraphBuffer> lightsImport;
};

// One declaration's occlusion state, published only when the whole frame declares.
struct RendererOcclusionFrame {
    std::shared_ptr<HzbGraphBinding> graph;     // Canonical pyramid imports.
    std::shared_ptr<HzbHistoryBinding> history; // Previous-pyramid observer.
    std::shared_ptr<const OcclusionDeclarationAssociation> association; // Reference join key.
    HzbSource current;      // Facts of this frame's build.
    OcclusionParams params; // Declared previous-source params.
    OcclusionInvalidReason reason = OcclusionInvalidReason::Disabled;
    glm::mat4 viewProjection{1.0f}; // Unjittered current view, for the next strict check.
    bool strictView = false;        // Unchanged unjittered view, coverage and active extent.
};

// One declaration's lighting status and light lists, published only when the whole frame declares.
struct RendererLightingFrame {
    LightingStatus status;        // Declared status, not yet retired.
    LightClusterOutputs clusters; // Grid and index lists; the declaration is discarded on failure.
};

// One declaration's visibility status and draw buffers, published with the frame.
struct RendererVisibilityFrame {
    std::array<GraphBuffer, 2> buffers;              // Draw rows and arguments to consume.
    VisibilityStatus status;                         // Declared status.
    std::optional<GpuVisibilityOutputs> declaration; // Live GPU intent, discarded on failure.
};

} // namespace lmx::render
