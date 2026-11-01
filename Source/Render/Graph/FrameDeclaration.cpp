//----------------------------------------------------------------------------------------------------------------------
/// @file FrameDeclaration.cpp
/// @brief Implements shared application frame declaration and accepted-record production.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/FrameDeclaration.h"

#include "Core/Diagnostics/Assert.h"
#include "Render/Renderer/Renderer.h"

namespace lmx::render {

//======================================================================================================================
FrameDeclaration::FrameDeclaration(TransientPool& pool, rojoRHI::CommandList& commands,
                                   bool poolingEnabled)
    : m_commands(commands), m_frameId(pool.frameNumber()), m_graph(pool) {
    pool.beginFrame();
    m_graph.setPoolingEnabled(poolingEnabled);
}

//======================================================================================================================
lmx::Result<void> FrameDeclaration::declare(Renderer& renderer, const engine::Camera& camera,
                                            const SceneView& view) {
    LMX_ASSERT(m_state == State::Undeclared, "FrameDeclaration declares only once");
    m_state = State::Failed;
    auto declared = renderer.declarePasses(m_graph, m_commands, camera, view);
    if (!declared)
        return std::unexpected(declared.error());
    m_displayColor = *declared;
    m_state = State::Declared;
    return {};
}

//======================================================================================================================
RenderGraph& FrameDeclaration::graph() {
    LMX_ASSERT(m_state == State::Declared,
               "FrameDeclaration graph requires a successful, unexecuted declaration");
    return m_graph;
}

//======================================================================================================================
GraphTexture FrameDeclaration::displayColor() const {
    LMX_ASSERT(m_state == State::Declared || m_state == State::Executed,
               "FrameDeclaration output requires a successful declaration");
    return m_displayColor;
}

//======================================================================================================================
CompiledFrameRecord FrameDeclaration::execute() {
    LMX_ASSERT(m_state == State::Declared,
               "FrameDeclaration executes a successful declaration only once");
    auto record = m_graph.execute(m_commands, m_frameId);
    m_state = State::Executed;
    return record;
}

} // namespace lmx::render
