//----------------------------------------------------------------------------------------------------------------------
/// @file FrameDeclaration.cpp
/// @brief Implements shared application frame declaration and accepted-record retention.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/FrameDeclaration.h"

#include "Core/Assert.h"

namespace lmx::app {

//======================================================================================================================
FrameDeclaration::FrameDeclaration(render::TransientPool& pool, render::Renderer& renderer,
                                   rhi::CommandList& commands, const render::Camera& camera,
                                   const render::SceneView& view, bool poolingEnabled)
    : m_commands(commands), m_frameId(pool.device().frameNumber()), m_graph(pool) {
    pool.beginFrame();
    m_graph.setPoolingEnabled(poolingEnabled);
    m_displayColor = renderer.declarePasses(m_graph, commands, camera, view);
}

//======================================================================================================================
render::RenderGraph& FrameDeclaration::graph() {
    LMX_ASSERT(!m_executed, "FrameDeclaration cannot change an executed graph");
    return m_graph;
}

//======================================================================================================================
void FrameDeclaration::execute(FrameRecordRing& records) {
    LMX_ASSERT(!m_executed, "FrameDeclaration executes only once");
    records.retain(m_graph.execute(m_commands, m_frameId));
    m_executed = true;
}

} // namespace lmx::app
