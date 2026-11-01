//----------------------------------------------------------------------------------------------------------------------
/// @file FrameDeclaration.h
/// @brief Declares one application frame's shared graph construction and execution.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Render/Graph/RenderGraph.h"

#include <cstdint>

namespace lmx::engine {
/// Camera borrowed by frame declaration.
class Camera;
} // namespace lmx::engine

namespace lmx::render {

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
/// once after declare succeeds. A failed declaration exposes no graph or output and cannot be
/// retried. Pool rotation, renderer declarations, and execution are common to both paths.
class FrameDeclaration {
public:
    /// Rotates the retired transient slot and applies pooling policy without declaring passes.
    /// The pool and commands are borrowed until this object is destroyed.
    FrameDeclaration(TransientPool& pool, rojoRHI::CommandList& commands, bool poolingEnabled);

    /// Frames cannot duplicate graph declarations or their single execution boundary.
    FrameDeclaration(const FrameDeclaration&) = delete;
    /// Frames cannot replace an existing declaration by assignment.
    FrameDeclaration& operator=(const FrameDeclaration&) = delete;

    /// Frames cannot transfer a graph whose callbacks borrow their declaration state.
    FrameDeclaration(FrameDeclaration&&) = delete;
    /// Frames cannot replace their borrowed command list or declaration state.
    FrameDeclaration& operator=(FrameDeclaration&&) = delete;

    /// Attempts renderer declaration exactly once, preserving any causal creation or retirement
    /// error. Success permits graph access and execution; failure leaves both unavailable.
    /// The renderer, camera, view, and every borrowed scene input remain valid through execute.
    lmx::Result<void> declare(Renderer& renderer, const engine::Camera& camera,
                              const SceneView& view);

    /// The graph to append application passes and sinks to, only after successful declaration and
    /// before execution. Other lifecycle states are misuse.
    RenderGraph& graph();

    /// The successfully declared display image, to sample for UI or export for readback.
    /// Access before declaration succeeds or after it fails is misuse.
    GraphTexture displayColor() const;

    /// Executes exactly once and returns the accepted record under this device frame's number.
    /// Graph validation failure is programmer error; motion commit remains the caller's boundary.
    CompiledFrameRecord execute();

private:
    enum class State : uint8_t { Undeclared, Failed, Declared, Executed };

    rojoRHI::CommandList& m_commands;
    uint64_t m_frameId;
    RenderGraph m_graph;
    GraphTexture m_displayColor;
    State m_state = State::Undeclared;
};

} // namespace lmx::render
