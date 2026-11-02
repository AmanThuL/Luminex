//----------------------------------------------------------------------------------------------------------------------
/// @file HzbHistoryBinding.cpp
/// @brief Retains canonical imports, actual content witnesses and frozen history parameters.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/HzbHistoryBinding.h"
#include "Render/Passes/Occlusion/HzbStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"

#include <array>
#include <format>
#include <optional>
#include <utility>

namespace lmx::render {
struct HzbGraphBinding::Impl {
    HzbStage& owner;
    RenderGraph& graph;
    rojoRHI::CommandList& commands;
    std::shared_ptr<HzbStage::Allocation> allocation;
    uint64_t actualFrame = 0;
    std::array<std::optional<GraphTexture>, 2> cursors;
    std::optional<GraphBuffer> publication;
};

struct HzbHistoryBinding::Impl {
    std::shared_ptr<HzbGraphBinding> graph;
    GraphTexture texture;
    uint32_t slot = 0;
    uint64_t declaredSerial = 0;
    HzbStage::ContentWitness candidate;
    HzbSource source;
    OcclusionParams requested;
    OcclusionParams params;
    std::vector<std::shared_ptr<VisibilityDeclarationProjection>> projections;
};

//======================================================================================================================
HzbGraphBinding::HzbGraphBinding(HzbStage& owner, RenderGraph& graph,
                                 rojoRHI::CommandList& commands)
    : m_impl(std::make_unique<Impl>(owner, graph, commands, owner.m_allocation,
                                    owner.m_gpu.frameNumber())) {}

//======================================================================================================================
HzbGraphBinding::~HzbGraphBinding() = default;

//======================================================================================================================
GraphTexture HzbGraphBinding::importSlot(uint32_t slot) {
    LMX_ASSERT(slot < m_impl->cursors.size() && m_impl->allocation,
               "pyramid import requires its allocated slot");
    if (!m_impl->cursors[slot]) {
        const auto& texture = m_impl->allocation->textures[slot];
        m_impl->cursors[slot] =
            m_impl->graph.importPersistentTexture(texture, std::format("lmx.render.hzb{}", slot));
    }
    return *m_impl->cursors[slot];
}

//======================================================================================================================
void HzbGraphBinding::advance(uint32_t slot, GraphTexture texture) {
    LMX_ASSERT(slot < m_impl->cursors.size(), "pyramid cursor requires an allocated slot");
    m_impl->cursors[slot] = texture;
}

//======================================================================================================================
GraphBuffer HzbGraphBinding::importPublication() {
    if (!m_impl->publication) {
        auto& owner = m_impl->owner;
        m_impl->publication =
            owner.m_published
                ? m_impl->graph.importBuffer(owner.m_publishBuffer, "lmx.render.hzbPublish",
                                             BufferUse::StorageWrite, owner.m_publishPhysical)
                : m_impl->graph.importBuffer(owner.m_publishBuffer, "lmx.render.hzbPublish");
    }
    return *m_impl->publication;
}

//======================================================================================================================
void HzbGraphBinding::advancePublication(GraphBuffer buffer) {
    m_impl->publication = buffer;
}

//======================================================================================================================
RenderGraph& HzbGraphBinding::graph() const {
    return m_impl->graph;
}

//======================================================================================================================
rojoRHI::CommandList& HzbGraphBinding::commands() const {
    return m_impl->commands;
}

//======================================================================================================================
HzbHistoryBinding::HzbHistoryBinding(std::shared_ptr<HzbGraphBinding> graph, uint32_t slot,
                                     const HzbSource& source)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->graph = std::move(graph);
    const auto& bundle = *m_impl->graph->m_impl;
    m_impl->slot = slot;
    m_impl->declaredSerial = bundle.owner.m_declaredSerials[slot];
    m_impl->candidate = bundle.allocation->contents[slot];
    m_impl->source = source;
    m_impl->texture = m_impl->graph->importSlot(slot);
    m_impl->requested =
        makeOcclusionParams(source.viewProjection, source.activeWidth, source.activeHeight,
                            source.levelCount, true, source.built);
    m_impl->params = prepareRecording();
}

//======================================================================================================================
HzbHistoryBinding::~HzbHistoryBinding() = default;

//======================================================================================================================
const HzbSource& HzbHistoryBinding::source() const {
    return m_impl->source;
}

//======================================================================================================================
std::optional<uint64_t> HzbHistoryBinding::contentFrame() const {
    const auto& bundle = *m_impl->graph->m_impl;
    const auto& candidate = m_impl->candidate;
    const auto& actual = bundle.allocation->contents[m_impl->slot];
    if (!bundle.owner.m_gpu.inFrame() || bundle.owner.m_gpu.frameNumber() != bundle.actualFrame ||
        bundle.owner.m_allocation != bundle.allocation || !candidate.complete || !actual.complete ||
        candidate.owner != &bundle.owner.m_gpu || candidate.commands == nullptr ||
        candidate.generation != bundle.allocation->generation ||
        candidate.serial != m_impl->declaredSerial || actual.serial != candidate.serial ||
        actual.owner != candidate.owner || actual.commands != candidate.commands ||
        actual.actualFrame != candidate.actualFrame || actual.generation != candidate.generation)
        return std::nullopt;
    return candidate.actualFrame;
}

//======================================================================================================================
OcclusionParams HzbHistoryBinding::prepareRecording() const {
    auto params = m_impl->requested;
    const auto frame = contentFrame();
    const auto actualFrame = m_impl->graph->m_impl->actualFrame;
    if (!frame || *frame >= actualFrame || actualFrame - *frame != 1)
        params.flags &= ~2u;
    return params;
}

//======================================================================================================================
OcclusionParams HzbHistoryBinding::recordingParams() const {
    return m_impl->params;
}

//======================================================================================================================
void HzbHistoryBinding::bindDeclaration(
    OcclusionParams params, std::shared_ptr<VisibilityDeclarationProjection> projection) {
    m_impl->requested = params;
    m_impl->params = prepareRecording();
    if (projection)
        m_impl->projections.push_back(std::move(projection));
}

//======================================================================================================================
void HzbHistoryBinding::publishRecording(const OcclusionParams& params) noexcept {
    m_impl->params = params;
    for (const auto& projection : m_impl->projections) {
        projection->status.occlusionParams = params;
        if (projection->status.occlusionEnabled && (params.flags & 2u) == 0 &&
            projection->status.occlusionInvalidReason == OcclusionInvalidReason::None)
            projection->status.occlusionInvalidReason = OcclusionInvalidReason::NoSource;
    }
}

//======================================================================================================================
rojoRHI::Texture HzbHistoryBinding::allocationTexture() const {
    return m_impl->graph->m_impl->allocation->textures[m_impl->slot].rhi();
}

//======================================================================================================================
rojoRHI::ViewIndex HzbHistoryBinding::sampledView(const PassResources& resources) const {
    return resources.view(m_impl->texture);
}

//======================================================================================================================
engine::TextureRef HzbHistoryBinding::sampledTexture(const PassResources& resources) const {
    return resources.texture(m_impl->texture);
}

//======================================================================================================================
void HzbHistoryBinding::appendRead(RenderGraph& graph, std::string_view name, ComputePassDesc desc,
                                   ExecuteFn execute, bool potentialCameraWork) {
    LMX_ASSERT(&graph == &m_impl->graph->graph(),
               "history observer belongs to its declaring graph");
    const auto pass = static_cast<uint32_t>(graph.m_passes.size());
    const TextureUseDesc observed{m_impl->texture};
    desc.shaderTextureReads.push_back(observed);
    const auto declaration =
        static_cast<uint32_t>(desc.textureReads.size() + desc.shaderTextureReads.size() - 1);
    graph.addComputePass(name, std::move(desc), std::move(execute));
    graph.m_hzbObservers.push_back(
        {shared_from_this(), pass, observed, potentialCameraWork, declaration});
}
} // namespace lmx::render
