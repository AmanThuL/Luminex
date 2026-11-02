//----------------------------------------------------------------------------------------------------------------------
/// @file RendererVisibility.cpp
/// @brief Coordinates CPU declarations and separately retired GPU visibility diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Diagnostics/Assert.h"
#include "Core/Util/Stopwatch.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/RendererInternal.h"
#include <limits>

namespace lmx::render {
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
lmx::Result<RendererVisibilityFrame>
Renderer::prepareVisibility(RenderGraph& graph, rojoRHI::CommandList& commands,
                            const SceneView& view, const FrustumPlanes& planes,
                            std::span<const GraphBuffer> sceneBuffers,
                            const RendererOcclusionFrame& occlusion) {
    const auto frame = m_gpu.frameNumber();
    // Retirement precedes draw-buffer growth, whose release list may destroy completed storage,
    // and the visibility declaration, whose slot reuse needs the frame three back released. One
    // transaction prepares production and reference before either publishes; its error is sticky.
    if (m_gpuVisibility && frame >= 3) {
        auto retired = VisibilityRetirementAccess::retire(*m_gpuVisibility,
                                                          m_occlusionReference.get(), frame - 3);
        if (!retired)
            return std::unexpected(retired.error());
    }
    RendererVisibilityFrame result;
    VisibilityStatus& status = result.status;
    status.occlusionAssociation = occlusion.association;
    status.occlusionEnabled = view.occlusionEnabled;
    status.occlusionCheckEnabled = view.occlusionCheck;
    status.occlusionInvalidReason = occlusion.reason;
    status.occlusionSourceFrame = m_occlusionSource.frameNumber;
    status.occlusionParams = occlusion.params;
    status.pyramidBytes = m_hzbStage && view.occlusionEnabled ? m_hzbStage->layout().bytes : 0;
    status.classifyMode = view.classifyMode;
    status.checkEnabled = view.classifyCheck;
    status.frameNumber = frame;
    status.sceneGeneration = view.temporal.sceneGeneration;
    const Stopwatch classifyTimer;
    if (view.classifyMode == ClassifyMode::Cpu) {
        status.scene = classifyView(planes, view.items, view.tables, view.visibilityEnabled);
        status.shadow = classifyView(planes, view.items, view.tables, view.visibilityEnabled, true);
    } else {
        // Preserve declaration-time row identity and bounds without executing the CPU box oracle.
        for (const auto& item : view.items) {
            LMX_ASSERT(item.instanceRow < view.tables.instanceRows.size(),
                       "visibility requires canonical instance rows");
            const auto& row = view.tables.instanceRows[item.instanceRow];
            if (row.flags & engine::kInstanceDisabled)
                ++status.sceneCounters.disabled;
            else
                ++status.sceneCounters.candidates;
            status.scene.candidates.push_back(
                {.instanceIdentity = item.instanceIdentity,
                 .instanceRow = item.instanceRow,
                 .worldBounds = {row.worldBoundsMin, row.worldBoundsMax}});
        }
        status.shadow.candidates = status.scene.candidates;
        status.shadowCounters = status.sceneCounters;
        if (!m_gpuVisibility) {
            auto stage = GpuVisibility::create(m_gpu);
            if (!stage)
                return std::unexpected(stage.error());
            m_gpuVisibility = std::move(*stage);
        }
    }
    const Stopwatch prepareTimer;
    status.classifyMs = view.classifyMode == ClassifyMode::Cpu
                            ? classifyTimer.elapsedMillisecondsUntil(prepareTimer)
                            : 0;

    // CPU frames copy their prepared prefixes on `commands` before any graph pass records.
    const auto prepared =
        m_drawSubmission.prepare(commands, frame, view, status.scene, status.shadow);
    if (!prepared)
        return std::unexpected(prepared.error());
    status.submission = m_drawSubmission.stats();
    auto import = [&](const engine::GpuBuffer& buffer, const char* label,
                      std::optional<BufferUse> use) {
        return use ? graph.importBuffer(buffer, label, *use) : graph.importBuffer(buffer, label);
    };
    auto rows = import(*m_drawSubmission.scene().rows, "lmx.draw.rows", m_drawSubmission.rowUse());
    auto arguments = import(*m_drawSubmission.scene().arguments, "lmx.draw.args",
                            m_drawSubmission.argumentUse());
    if (view.classifyMode == ClassifyMode::Gpu) {
        auto output = m_gpuVisibility->declare(
            graph, commands, view,
            {.planes = planes,
             .submission = m_drawSubmission.prepared(),
             .instances = sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[3],
             .meshes = sceneBuffers.empty() ? GraphBuffer{} : sceneBuffers[2],
             .rows = rows,
             .arguments = arguments,
             .history = occlusion.history,
             .occlusion = occlusion.params},
            status);
        if (!output)
            return std::unexpected(output.error());
        rows = output->rows;
        arguments = output->arguments;
        result.declaration = std::move(*output);
    } else {
        status.sceneCounters = cpuCounters(status.scene, status.submission.sceneCommands);
        status.shadowCounters = cpuCounters(status.shadow, status.submission.shadowCommands);
    }
    status.prepareMs = prepareTimer.elapsedMilliseconds();
    if (result.declaration)
        m_gpuVisibility->recordDeclarationStatus(*result.declaration, status);
    // CPU-only frames keep their historical imports; once a GPU writer has used this allocation
    // its terminal reads remain recorded across classifier switches.
    if (view.classifyMode == ClassifyMode::Gpu || m_drawSubmission.rowUse())
        m_drawSubmission.recordUses();
    result.buffers = {rows, arguments};
    return result;
}

//======================================================================================================================
std::vector<VisibilityStatus> Renderer::takeRetiredVisibility() {
    // Every record reached m_retired through a joined retirement, so taking cannot fail.
    return m_gpuVisibility ? m_gpuVisibility->takeRetired() : std::vector<VisibilityStatus>{};
}

//======================================================================================================================
lmx::Result<void> Renderer::drainVisibilityAfterIdle() {
    if (m_gpuVisibility)
        return VisibilityRetirementAccess::retire(*m_gpuVisibility, m_occlusionReference.get(),
                                                  std::numeric_limits<uint64_t>::max());
    LMX_ASSERT(!m_occlusionReference, "an occlusion reference requires its production owner");
    return {};
}

} // namespace lmx::render
