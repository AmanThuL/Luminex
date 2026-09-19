//----------------------------------------------------------------------------------------------------------------------
/// @file FrameDeclaration.h
/// @brief Declares one application frame's shared graph construction and execution.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/RenderGraph.h"

#include <cstdint>

namespace lmx::render {

/// Camera borrowed by frame declaration.
class Camera;
/// Renderer composing the frame's draw and processing stages.
class Renderer;
/// Frame inputs borrowed until graph execution finishes.
struct SceneView;

/// One declared frame, with a caller-chosen sink and one accepted compiled record.
///
/// Construct only after Device::beginFrame has retired the pool's next slot. The graph dies before
/// its pool; the view, caller-owned draw items, camera, and scene resources remain valid through
/// execute. The application declares the pool before the renderer, and the shell after it, so
/// destruction runs shell, renderer, pool, device, then the SDL layer. The owner establishes its
/// wait boundary before those resources unwind.
///
/// This object never begins or ends a device frame, joins timings, waits, or presents. The caller
/// appends its UI and present sink, or exports displayColor for headless readback, then executes
/// once. Pool rotation, renderer declarations, and execution are common to both paths.
class FrameDeclaration {
public:
    /// Rotates the retired transient slot, applies pooling policy, and declares renderer passes.
    /// The renderer, commands, camera, and view are borrowed until execute finishes.
    FrameDeclaration(TransientPool& pool, Renderer& renderer, rojoRHI::CommandList& commands,
                     const Camera& camera, const SceneView& view, bool poolingEnabled);

    /// Frames cannot duplicate graph declarations or their single execution boundary.
    FrameDeclaration(const FrameDeclaration&) = delete;
    /// Frames cannot replace an existing declaration by assignment.
    FrameDeclaration& operator=(const FrameDeclaration&) = delete;

    /// The graph to append application passes and sinks to; asserts after execution.
    RenderGraph& graph();

    /// The final display-transformed image, to sample for UI or export for readback.
    GraphTexture displayColor() const { return m_displayColor; }

    /// Executes exactly once and returns the accepted record under this device frame's number.
    /// Graph validation failure is programmer error; motion commit remains the caller's boundary.
    CompiledFrameRecord execute();

private:
    rojoRHI::CommandList& m_commands;
    uint64_t m_frameId;
    RenderGraph m_graph;
    GraphTexture m_displayColor;
    bool m_executed = false;
};

} // namespace lmx::render
