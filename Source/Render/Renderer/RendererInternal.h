//----------------------------------------------------------------------------------------------------------------------
/// @file RendererInternal.h
/// @brief Shares private derived frame state and resource imports between renderer units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Renderer/Renderer.h"

namespace lmx::render {

void seedReplacement(TextureStateTable& states, const engine::GpuTexture& previous,
                     const engine::GpuTexture& replacement, TextureUse initial);

#ifndef LMX_PENDING_TEMPORAL
// The temporal slots' entries, depth then colour per slot, as forgetTemporalSlots() found them.
using TemporalSlotUses = std::array<std::optional<TextureUse>, 4>;

TemporalSlotUses forgetTemporalSlots(TextureStateTable& states, TemporalResolve& temporal);
void restoreTemporalSlots(TextureStateTable& states, TemporalResolve& temporal,
                          const TemporalSlotUses& outgoing);
#endif

// Values derived once before any frame resources or passes are declared.
struct RendererFrameState {
    bool temporalEnabled;
#ifndef LMX_PENDING_TEMPORAL
    ReconstructionSelection selection;
#endif
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
#ifndef LMX_PENDING_SCENE
    ShadowMatrices shadow;
#endif
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
