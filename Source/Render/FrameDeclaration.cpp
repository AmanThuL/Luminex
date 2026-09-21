//----------------------------------------------------------------------------------------------------------------------
/// @file FrameDeclaration.cpp
/// @brief Implements shared application frame declaration and accepted-record production.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/FrameDeclaration.h"

#include "Core/Assert.h"
#include "Render/Renderer.h"

namespace lmx::render {

//======================================================================================================================
FrameDeclaration::FrameDeclaration(TransientPool& pool, Renderer& renderer,
                                   rojoRHI::CommandList& commands, const engine::Camera& camera,
                                   const SceneView& view, bool poolingEnabled)
    : m_commands(commands), m_frameId(pool.device().frameNumber()), m_graph(pool) {
    pool.beginFrame();
    m_graph.setPoolingEnabled(poolingEnabled);
    m_displayColor = renderer.declarePasses(m_graph, commands, camera, view);
}

//======================================================================================================================
RenderGraph& FrameDeclaration::graph() {
    LMX_ASSERT(!m_executed, "FrameDeclaration cannot change an executed graph");
    return m_graph;
}

//======================================================================================================================
CompiledFrameRecord FrameDeclaration::execute() {
    LMX_ASSERT(!m_executed, "FrameDeclaration executes only once");
    auto record = m_graph.execute(m_commands, m_frameId);
    m_executed = true;
    return record;
}

} // namespace lmx::render
