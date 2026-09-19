//----------------------------------------------------------------------------------------------------------------------
/// @file RendererVisibility.cpp
/// @brief Coordinates CPU declarations and separately retired GPU visibility diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Assert.h"
#include "Render/GpuVisibility.h"
#include "Render/OcclusionReference.h"
#include "Render/Renderer.h"
#include <chrono>
#include <limits>

namespace lmx::render {
namespace {
//======================================================================================================================
VisibilityCounters cpuCounters(const VisibilityResult& result, uint32_t commands) {
    return {.candidates = static_cast<uint32_t>(result.candidates.size()),
            .visible = result.visible,
            .rejected = result.rejected,
            .bypassed = result.bypassed,
            .emittedRows = static_cast<uint32_t>(result.visibleItems.size()),
            .emittedCommands = commands};
}
} // namespace

//======================================================================================================================
std::array<GraphBuffer, 2> Renderer::prepareVisibility(RenderGraph& graph,
                                                       rojoRHI::CommandList& commands,
                                                       const SceneView& view,
                                                       const FrustumPlanes& planes,
                                                       std::span<const GraphBuffer> sceneBuffers) {
    const auto frame = m_device.frameNumber();
    // Retirement precedes draw-buffer growth, whose release list may destroy completed storage.
    if (m_gpuVisibility && frame >= 3)
        m_gpuVisibility->retireThrough(frame - 3);
    m_visibilityStatus = {};
    m_visibilityStatus.occlusionEnabled = view.occlusionEnabled;
    m_visibilityStatus.occlusionCheckEnabled = view.occlusionCheck;
    m_visibilityStatus.occlusionInvalidReason = m_occlusionReason;
    m_visibilityStatus.occlusionSourceFrame = m_occlusionSource.frameNumber;
    m_visibilityStatus.occlusionParams = m_occlusionParams;
    m_visibilityStatus.pyramidBytes =
        m_hzbStage && view.occlusionEnabled ? m_hzbStage->layout().bytes : 0;
    m_visibilityStatus.classifyMode = view.classifyMode;
    m_visibilityStatus.checkEnabled = view.classifyCheck;
    m_visibilityStatus.frameNumber = frame;
    m_visibilityStatus.sceneGeneration = view.temporal.sceneGeneration;
    const auto classifyBegin = std::chrono::steady_clock::now();
    if (view.classifyMode == ClassifyMode::Cpu) {
        m_visibilityStatus.scene =
            classifyView(planes, view.items, view.tables, view.visibilityEnabled);
        m_visibilityStatus.shadow =
            classifyView(planes, view.items, view.tables, view.visibilityEnabled, true);
    } else {
        // Preserve declaration-time row identity and bounds without executing the CPU box oracle.
        for (const auto& item : view.items) {
            LMX_ASSERT(item.instanceRow < view.tables.instanceRows.size(),
                       "visibility requires canonical instance rows");
            const auto& row = view.tables.instanceRows[item.instanceRow];
            m_visibilityStatus.scene.candidates.push_back(
                {.instanceIdentity = item.instanceIdentity,
                 .instanceRow = item.instanceRow,
                 .worldBounds = {row.worldBoundsMin, row.worldBoundsMax}});
        }
        m_visibilityStatus.shadow.candidates = m_visibilityStatus.scene.candidates;
        if (!m_gpuVisibility) {
            auto stage = GpuVisibility::create(m_device);
            LMX_ASSERT(stage.has_value(), stage.error().message);
            m_gpuVisibility = std::move(*stage);
        }
    }
    const auto prepareBegin = std::chrono::steady_clock::now();
    m_visibilityStatus.classifyMs =
        view.classifyMode == ClassifyMode::Cpu
            ? std::chrono::duration<double, std::milli>(prepareBegin - classifyBegin).count()
            : 0;
    const auto prepared =
        m_drawSubmission.prepare(frame, view, m_visibilityStatus.scene, m_visibilityStatus.shadow);
    LMX_ASSERT(prepared.has_value(), prepared.error().message);
    m_visibilityStatus.submission = m_drawSubmission.stats();
    auto import = [&](rojoRHI::Buffer& buffer, const char* label, std::optional<rojoRHI::BufferUse> use) {
        return use ? graph.importBuffer(buffer, label, *use) : graph.importBuffer(buffer, label);
    };
    auto rows = import(*m_drawSubmission.scene().rows, "lmx.draw.rows", m_drawSubmission.rowUse());
    auto arguments = import(*m_drawSubmission.scene().arguments, "lmx.draw.args",
                            m_drawSubmission.argumentUse());
    if (view.classifyMode == ClassifyMode::Gpu) {
        const auto output = m_gpuVisibility->declare(
            graph, commands, view, planes, m_drawSubmission.prepared(),
            sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[3],
            sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[2], rows, arguments,
            m_visibilityStatus, m_previousPyramid, m_occlusionParams);
        rows = output.rows;
        arguments = output.arguments;
    } else {
        m_visibilityStatus.sceneCounters =
            cpuCounters(m_visibilityStatus.scene, m_visibilityStatus.submission.sceneCommands);
        m_visibilityStatus.shadowCounters =
            cpuCounters(m_visibilityStatus.shadow, m_visibilityStatus.submission.shadowCommands);
    }
    m_visibilityStatus.prepareMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareBegin)
            .count();
    if (view.classifyMode == ClassifyMode::Gpu)
        m_gpuVisibility->recordDeclarationStatus(m_visibilityStatus);
    // CPU-only frames keep their historical imports; once a GPU writer has used this allocation
    // its terminal reads remain recorded across classifier switches.
    if (view.classifyMode == ClassifyMode::Gpu || m_drawSubmission.rowUse())
        m_drawSubmission.recordUses();
    return {rows, arguments};
}

//======================================================================================================================
std::vector<VisibilityStatus> Renderer::takeRetiredVisibility() {
    auto results =
        m_gpuVisibility ? m_gpuVisibility->takeRetired() : std::vector<VisibilityStatus>{};
    if (m_occlusionReference) {
        for (auto& result : results) {
            m_occlusionReference->retireThrough(result.frameNumber);
            auto checked = m_occlusionReference->check(result);
            LMX_ASSERT(checked || !result.occlusionCheckEnabled,
                       "enabled occlusion reference must join its retired frame");
            if (checked)
                result.occlusionCheck = std::move(*checked);
        }
    }
    return results;
}

//======================================================================================================================
void Renderer::drainVisibilityAfterIdle() {
    if (m_occlusionReference)
        m_occlusionReference->retireThrough(std::numeric_limits<uint64_t>::max());
    if (m_gpuVisibility)
        m_gpuVisibility->retireThrough(std::numeric_limits<uint64_t>::max());
}
} // namespace lmx::render
