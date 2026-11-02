//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibility.cpp
/// @brief Allocates paced visibility tables and declares deterministic GPU work generation.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Visibility/GpuVisibility.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Occlusion/HzbHistoryBinding.h"
#include "Render/Passes/Visibility/VisibilityFrameData.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"
#include "Render/Passes/Visibility/VisibilityRoots.h"
#include "Render/Renderer/SceneView.h"

#include <algorithm>
#include <cstring>

namespace lmx::render {
namespace {
using S = rojoRHI::Stage;
using A = rojoRHI::Access;

// A partial use cannot eliminate any earlier contributor to the allocation.
//======================================================================================================================
void join(SyncPoint& prior, S stage, A access) {
    prior = {prior.stage | stage, prior.access | access, rojoRHI::Layout::Undefined};
}

#ifndef NDEBUG
//======================================================================================================================
void access(std::vector<rojoRHI::ResourceAccess>& accesses, const PassResources& resources,
            GraphBuffer handle, uint64_t bytes, A mask, uint64_t offset = 0) {
    if (bytes)
        accesses.push_back({.buffer = resources.buffer(handle),
                            .offset = offset,
                            .size = bytes,
                            .stage = S::ComputeShader,
                            .access = mask});
}
//======================================================================================================================
void counterAccess(std::vector<rojoRHI::ResourceAccess>& accesses, const PassResources& resources,
                   GraphBuffer handle, uint32_t candidates) {
    if (candidates)
        for (uint32_t view = 0; view < 2; ++view)
            access(accesses, resources, handle, 18 * sizeof(uint32_t), A::ShaderWrite,
                   uint64_t{view} * kVisibilityCounterWords * sizeof(uint32_t));
}
#endif
} // namespace

//======================================================================================================================
GpuVisibility::GpuVisibility(engine::GpuContext& gpu) : m_gpu(gpu) {}
//======================================================================================================================
GpuVisibility::~GpuVisibility() = default;

//======================================================================================================================
Result<std::unique_ptr<GpuVisibility>> GpuVisibility::create(engine::GpuContext& gpu) {
    std::unique_ptr<GpuVisibility> self(new GpuVisibility(gpu));
    constexpr std::array names{"VisibilityClassify", "VisibilityScan", "VisibilityEmit",
                               "VisibilityEmitSparse", "VisibilityClassifyOcclusion"};
    auto pipelines = createComputePipelines(gpu, names, "lmx.visibility.");
    if (!pipelines)
        return std::unexpected(pipelines.error());
    for (uint32_t index = 0; index < names.size(); ++index) {
        self->m_libraries[index] = std::move(pipelines->libraries[index]);
        self->m_pipelines[index] = std::move(pipelines->pipelines[index]);
    }
    const engine::InstanceRow emptyInstance;
    const engine::MeshRow emptyMesh;
    auto instance = engine::GpuBuffer::create(gpu.device(),
                                              {.size = sizeof(emptyInstance),
                                               .memory = rojoRHI::Memory::Upload,
                                               .label = "lmx.visibility.emptyInstances"},
                                              std::as_bytes(std::span{&emptyInstance, 1}));
    if (!instance)
        return std::unexpected(instance.error());
    self->m_emptyInstances = std::move(*instance);
    auto mesh = engine::GpuBuffer::create(gpu.device(),
                                          {.size = sizeof(emptyMesh),
                                           .memory = rojoRHI::Memory::Upload,
                                           .label = "lmx.visibility.emptyMeshes"},
                                          std::as_bytes(std::span{&emptyMesh, 1}));
    if (!mesh)
        return std::unexpected(mesh.error());
    self->m_emptyMeshes = std::move(*mesh);
    registerVisibilityLayoutsForCapture();
    return self;
}

//======================================================================================================================
Result<void> GpuVisibility::prepareSlot(VisibilitySlotBacking& slot,
                                        const VisibilityTables& tables) {
    const auto needed = std::max<uint32_t>(1, static_cast<uint32_t>(tables.candidates.size()));
    if (!slot.states || needed > slot.capacity) {
        uint32_t capacity = std::max(1u, slot.capacity);
        while (capacity < needed)
            capacity *= 2;
        VisibilitySlotBacking replacement;
        auto allocate = [&](engine::GpuBuffer& buffer, uint64_t bytes, const char* name,
                            bool writable) -> Result<void> {
            const std::string label =
                std::string(name) + "." + std::to_string(m_gpu.frameNumber() % 3);
            auto result = engine::GpuBuffer::create(
                m_gpu.device(),
                {.size = bytes,
                 .memory = writable ? rojoRHI::Memory::Gpu : rojoRHI::Memory::Upload,
                 .usage = writable ? rojoRHI::BufferUsage::ShaderWrite : rojoRHI::BufferUsage::None,
                 .label = label});
            if (!result)
                return std::unexpected(result.error());
            buffer = std::move(*result);
            return {};
        };
        auto result =
            allocate(replacement.candidates, uint64_t{capacity} * 8, "lmx.draw.candidates", false);
        if (!result)
            return result;
        result = allocate(replacement.runs, uint64_t{capacity} * 16, "lmx.draw.runs", false);
        if (!result)
            return result;
        result = allocate(replacement.chunks, uint64_t{capacity} * 16, "lmx.draw.chunks", false);
        if (!result)
            return result;
        result = allocate(replacement.views, 224, "lmx.draw.views", false);
        if (!result)
            return result;
        result = allocate(replacement.states, uint64_t{capacity} * 4, "lmx.draw.states", true);
        if (!result)
            return result;
        result = allocate(replacement.counters, 2 * kVisibilityCounterWords * sizeof(uint32_t),
                          "lmx.draw.counters", true);
        if (!result)
            return result;
        replacement.capacity = capacity;
        slot = std::move(replacement);
    }
    auto upload = [](engine::GpuBuffer& buffer, auto& previous, const auto& data) {
        const auto bytes = std::as_bytes(std::span(data));
        if (previous.size() == bytes.size() &&
            std::equal(previous.begin(), previous.end(), bytes.begin()))
            return;
        if (!bytes.empty())
            std::memcpy(buffer.cpuPointer(), bytes.data(), bytes.size());
        previous.assign(bytes.begin(), bytes.end());
    };
    upload(slot.candidates, slot.candidateBytes, tables.candidates);
    upload(slot.runs, slot.runBytes, tables.runs);
    upload(slot.chunks, slot.chunkBytes, tables.chunks);
    upload(slot.views, slot.viewBytes, tables.views);
    return {};
}

//======================================================================================================================
Result<GpuVisibilityOutputs>
GpuVisibility::declare(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                       const GpuVisibilityInputs& inputs, VisibilityStatus& status) {
    LMX_ASSERT(m_gpu.inFrame() && status.frameNumber == m_gpu.frameNumber(),
               "visibility declaration requires its actual open frame");
    const uint64_t frameNumber = m_gpu.frameNumber();
    auto tables = buildVisibilityTables(view, inputs.submission, inputs.planes);
    auto& cached = m_slots[frameNumber % m_slots.size()];
    const bool reusable = cached && cached.use_count() == 1;
    auto slot = reusable ? cached : std::make_shared<VisibilitySlotBacking>();
    if (!reusable && cached)
        slot->capacity = cached->capacity;
    const auto prepared = prepareSlot(*slot, tables);
    if (!prepared)
        return std::unexpected(prepared.error());
    const auto params = buildVisibilityParams(
        {.dense = view.submission == SubmissionMode::Batched,
         .rowBufferBytes = inputs.submission.scene.rows->desc().size,
         .argumentBufferBytes = inputs.submission.scene.arguments->desc().size,
         .stateCapacity = slot->capacity,
         .candidateCount = static_cast<uint32_t>(tables.candidates.size()),
         .capacityOverride = m_capacityOverride});
    status.submission.candidateBytes = tables.candidates.size() * sizeof(CandidateRecord);
    status.submission.runBytes = tables.runs.size() * sizeof(RunRecord);
    status.submission.chunkBytes = tables.chunks.size() * sizeof(ChunkRecord);
    status.submission.stateBytes = uint64_t{slot->capacity} * 4;
    status.submission.counterBytes = 2 * kVisibilityCounterWords * sizeof(uint32_t);
    const auto rowOwner = inputs.submission.scene.rows;
    const auto argumentOwner = inputs.submission.scene.arguments;
    auto tickets = std::make_shared<VisibilityReadbackTickets>();
    const auto callbackAnchor = std::make_shared<const uint8_t>(0);
    auto projection = std::make_shared<VisibilityDeclarationProjection>();
    projection->status = status;
    VisibilityPendingReadback pending{.status = status,
                                      .projection = projection,
                                      .tables = tables,
                                      .params = params,
                                      .geometry = inputs.submission.arguments,
                                      .tickets = tickets,
                                      .callbackAnchor = callbackAnchor,
                                      .backing = slot,
                                      .rowBuffer = rowOwner->rhi(),
                                      .argumentBuffer = argumentOwner->rhi(),
                                      .frameNumber = frameNumber};
    if (view.classifyCheck)
        for (uint32_t v = 0; v < 2; ++v)
            for (uint32_t i = 0; i < tables.views[v].candidateCount; ++i) {
                const auto row = tables.candidates[tables.views[v].firstCandidate + i].instanceRow;
                pending.expected.push_back(classifyInstance(inputs.planes,
                                                            view.tables.instanceRows[row], row,
                                                            view.visibilityEnabled, v == 1));
            }
    // All expected-fallible allocation has completed. Prior live declarations remain untouched.
    m_pending.reserve(m_pending.size() + 1);
    cached = slot;
    m_pending.push_back(std::move(pending));
    ++m_epoch;
    const auto check = [this, frameNumber, tickets, callbackAnchor] {
        (void)callbackAnchor;
        LMX_ASSERT(!tickets->discarded,
                   "visibility callback uses an explicitly discarded declaration");
        LMX_ASSERT(m_gpu.inFrame() && m_gpu.frameNumber() == frameNumber,
                   "visibility callback requires its actual declaration context and frame");
        tickets->entered = true;
        ++m_epoch;
    };
    const auto candidates = graph.importBuffer(slot->candidates, "lmx.draw.candidates");
    const auto runs = graph.importBuffer(slot->runs, "lmx.draw.runs");
    const auto chunks = graph.importBuffer(slot->chunks, "lmx.draw.chunks");
    const auto views = graph.importBuffer(slot->views, "lmx.draw.views");
    // A backing no recorded frame has used carries no prior use, as v1 imported a fresh slot.
    const auto states = slot->used
                            ? graph.importBuffer(slot->states, "lmx.draw.states",
                                                 BufferUse::StorageRead, slot->statesPhysical)
                            : graph.importBuffer(slot->states, "lmx.draw.states");
    const auto counters = slot->used
                              ? graph.importBuffer(slot->counters, "lmx.draw.counters",
                                                   BufferUse::StorageWrite, slot->countersPhysical)
                              : graph.importBuffer(slot->counters, "lmx.draw.counters");
    const auto chunkCount = static_cast<uint32_t>(tables.chunks.size());
    const auto runCount = static_cast<uint32_t>(tables.runs.size());
    const uint64_t chunkBytes = std::max<uint64_t>(4, uint64_t{chunkCount} * 4);
    const auto counts = graph.createBuffer(
        {.size = chunkBytes, .storageRead = true, .storageWrite = true}, "lmx.draw.chunkCounts");
    const auto offsets =
        params.layout
            ? graph.createBuffer({.size = chunkBytes, .storageRead = true, .storageWrite = true},
                                 "lmx.draw.chunkOffsets")
            : GraphBuffer{};
    CopyPassDesc reset;
    reset.bufferDestinations.push_back(counters);
    graph.addCopyPass(
        "lmx.pass.visibility.reset", std::move(reset),
        [=, &commands](const PassResources& resources) {
            check();
            LMX_ASSERT(resources.buffer(counters) == slot->counters.rhi(),
                       "visibility reset backing identity changed");
            commands.fillBuffer(slot->counters.rhi(), 0, slot->counters.desc().size, 0);
            // Exact full 168-byte allocation overwrite: all old contributors are covered.
            slot->countersPhysical = {S::Copy, A::CopyWrite, rojoRHI::Layout::Undefined};
        });
    auto statesInput = states;
    const auto guard = inputs.stateGuardReadback;
    if (guard) {
        CopyPassDesc initialize;
        initialize.bufferDestinations.push_back(states);
        graph.addCopyPass(
            "lmx.test.visibility.stateGuard", std::move(initialize),
            [=, &commands](const PassResources& resources) {
                check();
                LMX_ASSERT(resources.buffer(states) == slot->states.rhi(),
                           "visibility state canary backing identity changed");
                commands.fillBuffer(slot->states.rhi(), 0, slot->states.desc().size, 0xcdcdcdcdu);
                // Exact full allocation overwrite, including every undefined capacity-tail word.
                slot->statesPhysical = {S::Copy, A::CopyWrite, rojoRHI::Layout::Undefined};
            });
        statesInput = nextVersion(states);
    }
    const auto writtenStates = nextVersion(statesInput);
    auto instances = inputs.instances;
    auto meshes = inputs.meshes;
    [[maybe_unused]] uint64_t instanceBytes = sizeof(engine::InstanceRow),
                              meshBytes = sizeof(engine::MeshRow);
    if (!view.tables.instances) {
        instances = graph.importBuffer(m_emptyInstances, "lmx.visibility.emptyInstances");
        meshes = graph.importBuffer(m_emptyMeshes, "lmx.visibility.emptyMeshes");
    } else {
        instanceBytes = view.tables.instances->desc().size;
        meshBytes = view.tables.meshes->desc().size;
    }
    const auto history = inputs.history;
    LMX_ASSERT(!view.occlusionEnabled || history,
               "occlusion classification requires its history binding");
    if (history)
        history->bindDeclaration(inputs.occlusion, projection);
    const bool occlusionEnabled = view.occlusionEnabled;
    const bool cameraPotential = tables.views[0].candidateCount != 0;
    const uint64_t stateBytes = uint64_t{std::min(params.candidateCount, params.stateCapacity)} * 4;
    const uint64_t rowBytes = uint64_t{std::min(params.candidateCount, params.rowCapacity)} * 4;
    const uint64_t argumentBytes = uint64_t{std::min(runCount, params.argumentCapacity)} *
                                   sizeof(rojoRHI::DrawIndexedIndirectArgs);
    ComputePassDesc classify;
    classify.shaderBufferReads = {candidates, chunks, instances, views};
    classify.bufferWrites = {statesInput, nextVersion(counters), counts};
    ExecuteFn classifyExecute = [=, this, &commands](const PassResources& resources) {
        check();
        const auto paramsAddr = frameData(commands, params);
        engine::registerFrameRoot("VisibilityParams", paramsAddr);
        commands.setPipeline(m_pipelines[occlusionEnabled ? 4 : 0].get());
        const VisibilityClassifyRoot root{resources.address(candidates),
                                          resources.address(chunks),
                                          resources.address(instances),
                                          resources.address(views),
                                          paramsAddr,
                                          resources.address(statesInput),
                                          resources.address(nextVersion(counters)),
                                          resources.address(counts)};
        const auto effectiveOcclusion = history ? history->recordingParams() : OcclusionParams{};
        rojoRHI::Addr occlusionAddr{}, occlusionRootAddr{};
        rojoRHI::ViewIndex pyramidView{};
        VisibilityOcclusionRoot occlusionRoot{};
        if (occlusionEnabled) {
            occlusionAddr = frameData(commands, effectiveOcclusion);
            engine::registerFrameRoot("OcclusionParams", occlusionAddr);
            pyramidView = history->sampledView(resources);
            occlusionRoot = {root.candidatesAddr,
                             root.chunksAddr,
                             root.instancesAddr,
                             root.viewsAddr,
                             root.paramsAddr,
                             root.statesAddr,
                             root.countersAddr,
                             root.countsAddr,
                             occlusionAddr,
                             static_cast<uint32_t>(pyramidView),
                             0};
            occlusionRootAddr = commands.pushRoot(occlusionRoot);
            engine::registerFrameRoot("VisibilityOcclusionRoot", occlusionRootAddr);
        } else {
            engine::registerFrameRoot("VisibilityClassifyRoot", commands.pushRoot(root));
        }
#ifndef NDEBUG
        std::vector<rojoRHI::ResourceAccess> accesses;
        access(accesses, resources, candidates,
               uint64_t{params.candidateCount} * sizeof(CandidateRecord), A::ShaderRead);
        access(accesses, resources, chunks, uint64_t{chunkCount} * sizeof(ChunkRecord),
               A::ShaderRead);
        access(accesses, resources, instances, params.candidateCount ? instanceBytes : 0,
               A::ShaderRead);
        access(accesses, resources, views, sizeof(VisibilityViewParams) * 2, A::ShaderRead);
        access(accesses, resources, statesInput, stateBytes, A::ShaderWrite);
        access(accesses, resources, counts, uint64_t{chunkCount} * 4, A::ShaderWrite);
        counterAccess(accesses, resources, nextVersion(counters), params.candidateCount);
        if (occlusionEnabled && cameraPotential && (effectiveOcclusion.flags & 3u) == 3u)
            accesses.push_back({.texture = history->sampledTexture(resources).texture,
                                .stage = S::ComputeShader,
                                .access = A::ShaderRead});
        commands.declareAccess(accesses);
#endif
        if (occlusionEnabled && m_recordingObserver) {
#ifndef NDEBUG
            const bool accessDeclared = true;
            const std::span<const rojoRHI::ResourceAccess> observedAccesses = accesses;
#else
            const bool accessDeclared = false;
            const std::span<const rojoRHI::ResourceAccess> observedAccesses;
#endif
            m_recordingObserver(m_recordingObserverContext, frameNumber,
                                std::as_bytes(std::span{&effectiveOcclusion, 1}), occlusionAddr,
                                std::as_bytes(std::span{&occlusionRoot, 1}), occlusionRootAddr,
                                pyramidView, accessDeclared, observedAccesses);
        }
        commands.dispatch(std::max(1u, chunkCount), 1, 1);
        if (stateBytes) {
            if (stateBytes == slot->states.desc().size) {
                // Every candidate below capacity writes one word; this dispatch covers the
                // whole allocation.
                slot->statesPhysical = {S::ComputeShader, A::ShaderWrite,
                                        rojoRHI::Layout::Undefined};
            } else {
                join(slot->statesPhysical, S::ComputeShader, A::ShaderWrite);
            }
        }
        if (params.candidateCount)
            join(slot->countersPhysical, S::ComputeShader, A::ShaderWrite);
        LMX_ASSERT(resources.buffer(statesInput) == slot->states.rhi(),
                   "visibility classify backing identity changed");
        tickets->states =
            recordReadback(m_gpu, commands, slot->states, slot->statesPhysical, {0, stateBytes});
        if (guard)
            *guard = recordReadback(m_gpu, commands, slot->states, slot->statesPhysical,
                                    wholeBuffer(slot->states));
    };
    if (occlusionEnabled)
        history->appendRead(graph, "lmx.pass.visibility.classify", std::move(classify),
                            std::move(classifyExecute), tables.views[0].candidateCount != 0);
    else
        graph.addComputePass("lmx.pass.visibility.classify", std::move(classify),
                             std::move(classifyExecute));
    if (params.layout) {
        ComputePassDesc scan;
        scan.shaderBufferReads = {runs, chunks, views};
        scan.bufferReads = {nextVersion(counts)};
        scan.bufferWrites = {offsets};
        graph.addComputePass(
            "lmx.pass.visibility.scan", std::move(scan),
            [=, this, &commands](const PassResources& resources) {
                check();
                commands.setPipeline(m_pipelines[1].get());
                engine::registerFrameRoot(
                    "VisibilityScanRoot",
                    commands.pushRoot(VisibilityScanRoot{
                        resources.address(runs), resources.address(chunks),
                        resources.address(views), resources.address(nextVersion(counts)),
                        resources.address(offsets)}));
#ifndef NDEBUG
                std::vector<rojoRHI::ResourceAccess> accesses;
                access(accesses, resources, runs, uint64_t{runCount} * sizeof(RunRecord),
                       A::ShaderRead);
                access(accesses, resources, chunks, uint64_t{chunkCount} * sizeof(ChunkRecord),
                       A::ShaderRead);
                access(accesses, resources, views, sizeof(VisibilityViewParams) * 2, A::ShaderRead);
                access(accesses, resources, nextVersion(counts), uint64_t{chunkCount} * 4,
                       A::ShaderRead);
                access(accesses, resources, offsets, uint64_t{chunkCount} * 4, A::ShaderWrite);
                commands.declareAccess(accesses);
#endif
                commands.dispatch(std::max(1u, runCount), 1, 1);
            });
    }
    const auto rows = inputs.rows, arguments = inputs.arguments;
    const auto rowPrior = inputs.rowPhysical;
    const auto argumentPrior = inputs.argumentPhysical;
    const bool rowWriteCapable =
        params.candidateCount && params.rowCapacity &&
        (!params.layout || (params.stateCapacity && params.argumentCapacity));
    const bool argumentWriteCapable = runCount && params.argumentCapacity;
    ComputePassDesc emit;
    emit.shaderBufferReads = {candidates, runs, chunks, instances, meshes, views};
    emit.bufferReads = {writtenStates};
    if (params.layout)
        emit.bufferReads.push_back(nextVersion(offsets));
    emit.bufferWrites = {rows, arguments, nextVersion(nextVersion(counters))};
    graph.addComputePass(
        "lmx.pass.visibility.emit", std::move(emit),
        [=, this, &commands](const PassResources& resources) {
            check();
            LMX_ASSERT(resources.buffer(rows) == rowOwner->rhi() &&
                           resources.buffer(arguments) == argumentOwner->rhi(),
                       "visibility output backing identity changed");
            const auto paramsAddr = frameData(commands, params);
            engine::registerFrameRoot("VisibilityParams", paramsAddr);
            commands.setPipeline(m_pipelines[params.layout ? 2 : 3].get());
            engine::registerFrameRoot(
                "VisibilityEmitRoot",
                commands.pushRoot(VisibilityEmitRoot{
                    resources.address(candidates), resources.address(runs),
                    resources.address(chunks), resources.address(instances),
                    resources.address(meshes), resources.address(views), paramsAddr,
                    resources.address(writtenStates),
                    resources.address(nextVersion(nextVersion(counters))),
                    params.layout ? resources.address(nextVersion(offsets)) : rojoRHI::Addr{},
                    resources.address(rows), resources.address(arguments)}));
#ifndef NDEBUG
            std::vector<rojoRHI::ResourceAccess> accesses;
            access(accesses, resources, candidates,
                   uint64_t{params.candidateCount} * sizeof(CandidateRecord), A::ShaderRead);
            access(accesses, resources, runs, uint64_t{runCount} * sizeof(RunRecord),
                   A::ShaderRead);
            access(accesses, resources, chunks, uint64_t{chunkCount} * sizeof(ChunkRecord),
                   A::ShaderRead);
            access(accesses, resources, instances, params.candidateCount ? instanceBytes : 0,
                   A::ShaderRead);
            access(accesses, resources, meshes, params.candidateCount ? meshBytes : 0,
                   A::ShaderRead);
            access(accesses, resources, views, sizeof(VisibilityViewParams) * 2, A::ShaderRead);
            access(accesses, resources, writtenStates, stateBytes, A::ShaderRead);
            if (params.layout)
                access(accesses, resources, nextVersion(offsets), uint64_t{chunkCount} * 4,
                       A::ShaderRead);
            access(accesses, resources, rows, rowWriteCapable ? rowBytes : 0, A::ShaderWrite);
            access(accesses, resources, arguments, argumentWriteCapable ? argumentBytes : 0,
                   A::ShaderWrite);
            counterAccess(accesses, resources, nextVersion(nextVersion(counters)),
                          params.candidateCount);
            commands.declareAccess(accesses);
#endif
            commands.dispatch(std::max(1u, chunkCount), 1, 1);
            if (stateBytes)
                join(slot->statesPhysical, S::ComputeShader, A::ShaderRead);
            if (params.candidateCount)
                join(slot->countersPhysical, S::ComputeShader, A::ShaderWrite);
            tickets->counters = recordReadback(m_gpu, commands, slot->counters,
                                               slot->countersPhysical, wholeBuffer(slot->counters));
            // Unsupplied provenance does not certify externally reused output owners.
            auto rowsPhysical =
                rowPrior.value_or(SyncPoint{S::None, A::None, rojoRHI::Layout::Undefined});
            auto argumentsPhysical =
                argumentPrior.value_or(SyncPoint{S::None, A::None, rojoRHI::Layout::Undefined});
            if (rowWriteCapable)
                join(rowsPhysical, S::ComputeShader, A::ShaderWrite);
            if (argumentWriteCapable)
                join(argumentsPhysical, S::ComputeShader, A::ShaderWrite);
            tickets->rows = recordReadback(m_gpu, commands, *rowOwner, rowsPhysical, {0, rowBytes});
            tickets->arguments = recordReadback(m_gpu, commands, *argumentOwner, argumentsPhysical,
                                                {0, argumentBytes});
            slot->used = true;
        });
    graph.readbackBuffer(nextVersion(nextVersion(nextVersion(counters))));
    graph.readbackBuffer(writtenStates);
    graph.exportBuffer(nextVersion(rows));
    graph.exportBuffer(nextVersion(arguments));
    return GpuVisibilityOutputs{nextVersion(rows), nextVersion(arguments), tickets, projection};
}

//======================================================================================================================
void GpuVisibility::recordDeclarationStatus(const GpuVisibilityOutputs& declaration,
                                            const VisibilityStatus& status) {
    const auto token = declaration.readbackIntent.lock();
    LMX_ASSERT(token, "visibility status requires a live exact declaration token");
    const auto found = std::ranges::find_if(
        m_pending, [&](const auto& entry) { return entry.tickets.get() == token.get(); });
    LMX_ASSERT(found != m_pending.end() && !found->tickets->discarded,
               "visibility status requires this owner's live declaration");
    LMX_ASSERT(status.frameNumber == found->frameNumber &&
                   status.sceneGeneration == found->status.sceneGeneration &&
                   status.occlusionAssociation == found->status.occlusionAssociation,
               "visibility status must preserve its declaration frame and scene identity");
    auto replacement = status;
    replacement.isRetired = false;
    found->status = std::move(replacement);
    if (found->projection)
        found->projection->status = found->status;
    ++m_epoch;
}

//======================================================================================================================
void GpuVisibility::discardUnrecorded(const GpuVisibilityOutputs& declaration) {
    const auto token = declaration.readbackIntent.lock();
    LMX_ASSERT(token, "visibility discard requires a live exact declaration token");
    const auto found = std::ranges::find_if(
        m_pending, [&](const auto& entry) { return entry.tickets.get() == token.get(); });
    LMX_ASSERT(found != m_pending.end(), "visibility discard requires this owner's declaration");
    const auto tickets = found->tickets;
    const auto backing = found->backing;
    LMX_ASSERT(!tickets->discarded && !tickets->entered && !tickets->states && !tickets->counters &&
                   !tickets->rows && !tickets->arguments,
               "visibility discard requires wholly unrecorded intent");
    tickets->discarded = true;
    m_pending.erase(found);
    ++m_epoch;
}
} // namespace lmx::render
