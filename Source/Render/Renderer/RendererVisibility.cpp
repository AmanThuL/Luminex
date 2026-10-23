//----------------------------------------------------------------------------------------------------------------------
/// @file RendererVisibility.cpp
/// @brief Coordinates CPU declarations and separately retired GPU visibility diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Diagnostics/Assert.h"
#include "Core/Util/Stopwatch.h"
#ifndef LMX_PENDING_VISIBILITY
#include "Render/Passes/Occlusion/OcclusionReference.h"
#endif

#ifndef LMX_PENDING_VISIBILITY
#include "Render/Passes/Visibility/GpuVisibility.h"
#endif
#include "Render/Renderer/Renderer.h"
#include <limits>

namespace lmx::render {
#ifndef LMX_PENDING_SCENE
namespace {
//======================================================================================================================
VisibilityCounters cpuCounters(const VisibilityResult& result, uint32_t commands) {
    return {.candidates = static_cast<uint32_t>(result.candidates.size()) - result.disabled,
            .visible = result.visible,
            .rejected = result.rejected,
            .disabled = result.disabled,
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
    const auto frame = m_gpu.frameNumber();
#ifndef LMX_PENDING_VISIBILITY
    // Retirement precedes draw-buffer growth, whose release list may destroy completed storage.
    if (m_gpuVisibility && frame >= 3)
        m_gpuVisibility->retireThrough(frame - 3);
#endif
    m_visibilityStatus = {};
#ifndef LMX_PENDING_VISIBILITY
    m_visibilityStatus.occlusionEnabled = view.occlusionEnabled;
    m_visibilityStatus.occlusionCheckEnabled = view.occlusionCheck;
    m_visibilityStatus.occlusionInvalidReason = m_occlusionReason;
    m_visibilityStatus.occlusionSourceFrame = m_occlusionSource.frameNumber;
    m_visibilityStatus.occlusionParams = m_occlusionParams;
    m_visibilityStatus.pyramidBytes =
        m_hzbStage && view.occlusionEnabled ? m_hzbStage->layout().bytes : 0;
    m_visibilityStatus.classifyMode = view.classifyMode;
    m_visibilityStatus.checkEnabled = view.classifyCheck;
#else
    m_visibilityStatus.classifyMode = ClassifyMode::Cpu;
#endif
    m_visibilityStatus.frameNumber = frame;
    m_visibilityStatus.sceneGeneration = view.temporal.sceneGeneration;
    const Stopwatch classifyTimer;
#ifndef LMX_PENDING_VISIBILITY
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
            if (row.flags & engine::kInstanceDisabled)
                ++m_visibilityStatus.sceneCounters.disabled;
            else
                ++m_visibilityStatus.sceneCounters.candidates;
            m_visibilityStatus.scene.candidates.push_back(
                {.instanceIdentity = item.instanceIdentity,
                 .instanceRow = item.instanceRow,
                 .worldBounds = {row.worldBoundsMin, row.worldBoundsMax}});
        }
        m_visibilityStatus.shadow.candidates = m_visibilityStatus.scene.candidates;
        m_visibilityStatus.shadowCounters = m_visibilityStatus.sceneCounters;
        if (!m_gpuVisibility) {
            auto stage = GpuVisibility::create(m_gpu);
            LMX_ASSERT(stage.has_value(), stage.error().message);
            m_gpuVisibility = std::move(*stage);
        }
    }
#else
    m_visibilityStatus.scene =
        classifyView(planes, view.items, view.tables, view.visibilityEnabled);
    m_visibilityStatus.shadow =
        classifyView(planes, view.items, view.tables, view.visibilityEnabled, true);
#endif
    const Stopwatch prepareTimer;
#ifndef LMX_PENDING_VISIBILITY
    m_visibilityStatus.classifyMs = view.classifyMode == ClassifyMode::Cpu
                                        ? classifyTimer.elapsedMillisecondsUntil(prepareTimer)
                                        : 0;
#else
    m_visibilityStatus.classifyMs = classifyTimer.elapsedMillisecondsUntil(prepareTimer);
#endif

#ifndef LMX_PENDING_VISIBILITY
    const auto prepared =
        m_drawSubmission.prepare(frame, view, m_visibilityStatus.scene, m_visibilityStatus.shadow);
#else
    SceneView cpuView = view;
    cpuView.classifyMode = ClassifyMode::Cpu;
    const auto prepared = m_drawSubmission.prepare(frame, cpuView, m_visibilityStatus.scene,
                                                   m_visibilityStatus.shadow);
#endif
    LMX_ASSERT(prepared.has_value(), prepared.error().message);
    m_visibilityStatus.submission = m_drawSubmission.stats();
    auto import = [&](rojoRHI::Buffer& buffer, const char* label, std::optional<BufferUse> use) {
        return use ? graph.importBuffer(buffer, label, *use) : graph.importBuffer(buffer, label);
    };
    auto rows =
        import(m_drawSubmission.scene().rows->rhi(), "lmx.draw.rows", m_drawSubmission.rowUse());
    auto arguments = import(m_drawSubmission.scene().arguments->rhi(), "lmx.draw.args",
                            m_drawSubmission.argumentUse());
#ifndef LMX_PENDING_VISIBILITY
    if (view.classifyMode == ClassifyMode::Gpu) {
        const auto output = m_gpuVisibility->declare(
            graph, commands, view,
            {.planes = planes,
             .submission = m_drawSubmission.prepared(),
             .instances = sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[3],
             .meshes = sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[2],
             .rows = rows,
             .arguments = arguments,
             .pyramid = m_previousPyramid,
             .occlusion = m_occlusionParams},
            m_visibilityStatus);
        rows = output.rows;
        arguments = output.arguments;
    } else {
        m_visibilityStatus.sceneCounters =
            cpuCounters(m_visibilityStatus.scene, m_visibilityStatus.submission.sceneCommands);
        m_visibilityStatus.shadowCounters =
            cpuCounters(m_visibilityStatus.shadow, m_visibilityStatus.submission.shadowCommands);
    }
#else
    m_visibilityStatus.sceneCounters =
        cpuCounters(m_visibilityStatus.scene, m_visibilityStatus.submission.sceneCommands);
    m_visibilityStatus.shadowCounters =
        cpuCounters(m_visibilityStatus.shadow, m_visibilityStatus.submission.shadowCommands);
#endif
    m_visibilityStatus.prepareMs = prepareTimer.elapsedMilliseconds();
#ifndef LMX_PENDING_VISIBILITY
    if (view.classifyMode == ClassifyMode::Gpu)
        m_gpuVisibility->recordDeclarationStatus(m_visibilityStatus);
    // CPU-only frames keep their historical imports; once a GPU writer has used this allocation
    // its terminal reads remain recorded across classifier switches.
    if (view.classifyMode == ClassifyMode::Gpu || m_drawSubmission.rowUse())
        m_drawSubmission.recordUses();
#endif
    return {rows, arguments};
}

#endif
} // namespace lmx::render
