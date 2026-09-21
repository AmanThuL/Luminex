//----------------------------------------------------------------------------------------------------------------------
/// @file RendererInternal.h
/// @brief Shares private derived frame state and resource imports between renderer units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/Renderer.h"

namespace lmx::render {
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

} // namespace lmx::render
