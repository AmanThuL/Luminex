//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibility.cpp
/// @brief Allocates paced visibility tables and declares deterministic GPU work generation.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Core/Diagnostics/Assert.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/PacedSlots.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Visibility/VisibilityFrameData.h"
#include "Render/Renderer/SceneView.h"
#include <algorithm>
#include <cstring>

namespace lmx::render {
//======================================================================================================================
lmx::Result<std::unique_ptr<GpuVisibility>> GpuVisibility::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<GpuVisibility> self(new GpuVisibility(gpu));
    constexpr std::array names{"VisibilityClassify", "VisibilityScan", "VisibilityEmit",
                               "VisibilityEmitSparse", "VisibilityClassifyOcclusion"};
    auto pipelines =
        createComputePipelines(gpu, names, "lmx.visibility.", [](uint32_t) { return 256u; });
    if (!pipelines) {
        return std::unexpected(pipelines.error());
    }
    for (uint32_t index = 0; index < names.size(); ++index) {
        self->m_libraries[index] = std::move(pipelines->libraries[index]);
        self->m_pipelines[index] = std::move(pipelines->pipelines[index]);
    }
    const engine::InstanceRow emptyInstance;
    const engine::MeshRow emptyMesh;
    auto instance = engine::GpuBuffer::create(
        device, {.size = sizeof(emptyInstance), .label = "lmx.visibility.emptyInstances"},
        &emptyInstance);
    if (!instance) {
        return std::unexpected(instance.error());
    }
    self->m_emptyInstances = std::move(*instance);
    auto mesh = engine::GpuBuffer::create(
        device, {.size = sizeof(emptyMesh), .label = "lmx.visibility.emptyMeshes"}, &emptyMesh);
    if (!mesh) {
        return std::unexpected(mesh.error());
    }
    self->m_emptyMeshes = std::move(*mesh);
    return self;
}

//======================================================================================================================
lmx::Result<void> GpuVisibility::prepareSlot(Slot& slot, const VisibilityTables& tables) {
    const auto needed = std::max<uint32_t>(1, static_cast<uint32_t>(tables.candidates.size()));
    if (needed > slot.capacity) {
        uint32_t capacity = std::max(1u, slot.capacity);
        while (capacity < needed)
            capacity *= 2;
        Slot replacement;
        auto allocate = [&](engine::GpuBuffer& buffer, uint64_t bytes, const char* name,
                            bool writable) -> lmx::Result<void> {
            const std::string label =
                std::string(name) + "." + std::to_string(m_gpu.frameNumber() % 3);
            auto result = engine::GpuBuffer::create(m_gpu.device(), {.size = bytes,
                                                                     .storageRead = true,
                                                                     .storageWrite = writable,
                                                                     .cpuReadback = writable,
                                                                     .cpuWrite = !writable,
                                                                     .label = label});
            if (!result) {
                return std::unexpected(result.error());
            }
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
        // Only the paced current slot grows: every replaced resource has already retired.
        slot = std::move(replacement);
    }
    auto upload = [](rojoRHI::Buffer& buffer, auto& previous, const auto& data) {
        const auto bytes = std::as_bytes(std::span(data));
        if (previous.size() == bytes.size() &&
            std::equal(previous.begin(), previous.end(), bytes.begin()))
            return;
        if (!bytes.empty())
            buffer.write(0, bytes.data(), bytes.size());
        previous.assign(bytes.begin(), bytes.end());
    };
    upload(slot.candidates.rhi(), slot.candidateBytes, tables.candidates);
    upload(slot.runs.rhi(), slot.runBytes, tables.runs);
    upload(slot.chunks.rhi(), slot.chunkBytes, tables.chunks);
    upload(slot.views.rhi(), slot.viewBytes, tables.views);
    return {};
}

//======================================================================================================================
GpuVisibilityOutputs GpuVisibility::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                            const SceneView& view,
                                            const GpuVisibilityInputs& inputs,
                                            VisibilityStatus& status) {
    const auto& planes = inputs.planes;
    const auto& submission = inputs.submission;
    auto instances = inputs.instances;
    auto meshes = inputs.meshes;
    const auto rows = inputs.rows;
    const auto arguments = inputs.arguments;
    const auto pyramid = inputs.pyramid;
    auto occlusion = inputs.occlusion;
    auto tables = buildVisibilityTables(view, submission, planes);
    auto& slot = m_slots[m_gpu.frameNumber() % 3];
    const auto prepared = prepareSlot(slot, tables);
    LMX_ASSERT(prepared.has_value(), prepared.error().message);
    const VisibilityParams params =
        buildVisibilityParams({.dense = view.submission == SubmissionMode::Batched,
                               .rowBufferBytes = submission.scene.rows->desc().size,
                               .argumentBufferBytes = submission.scene.arguments->desc().size,
                               .stateCapacity = slot.capacity,
                               .candidateCount = static_cast<uint32_t>(tables.candidates.size()),
                               .capacityOverride = m_capacityOverride});
    status.submission.candidateBytes = tables.candidates.size() * sizeof(CandidateRecord);
    status.submission.runBytes = tables.runs.size() * sizeof(RunRecord);
    status.submission.chunkBytes = tables.chunks.size() * sizeof(ChunkRecord);
    status.submission.stateBytes = uint64_t{slot.capacity} * 4;
    status.submission.counterBytes = 2 * kVisibilityCounterWords * sizeof(uint32_t);
    Pending pending{
        .status = status, .tables = tables, .params = params, .geometry = submission.arguments};
    const auto tickets = pending.tickets;
    if (view.classifyCheck)
        for (uint32_t v = 0; v < 2; ++v)
            for (uint32_t i = 0; i < tables.views[v].candidateCount; ++i) {
                const auto row = tables.candidates[tables.views[v].firstCandidate + i].instanceRow;
                pending.expected.push_back(classifyInstance(planes, view.tables.instanceRows[row],
                                                            row, view.visibilityEnabled, v == 1));
            }
    m_pending.push_back(std::move(pending));
    const auto candidates = graph.importBuffer(slot.candidates.rhi(), "lmx.draw.candidates");
    const auto runs = graph.importBuffer(slot.runs.rhi(), "lmx.draw.runs");
    const auto chunks = graph.importBuffer(slot.chunks.rhi(), "lmx.draw.chunks");
    const auto views = graph.importBuffer(slot.views.rhi(), "lmx.draw.views");
    const auto states =
        importPaced(graph, slot.states.rhi(), "lmx.draw.states", BufferUse::StorageRead, slot.used);
    const auto counters = importPaced(graph, slot.counters.rhi(), "lmx.draw.counters",
                                      BufferUse::StorageWrite, slot.used);
    const auto chunkBytes = std::max<uint64_t>(4, tables.chunks.size() * 4);
    const auto counts = graph.createBuffer(
        {.size = chunkBytes, .storageRead = true, .storageWrite = true}, "lmx.draw.chunkCounts");
    const auto offsets =
        params.layout
            ? graph.createBuffer({.size = chunkBytes, .storageRead = true, .storageWrite = true},
                                 "lmx.draw.chunkOffsets")
            : GraphBuffer{};
    declareZeroFill(graph, commands, "lmx.pass.visibility.reset", counters,
                    2 * kVisibilityCounterWords * sizeof(uint32_t));
    auto bindRead = [&commands](const PassResources& resources, uint32_t index,
                                GraphBuffer handle) {
        commands.bindBuffer(index, lmx::render::buffer(resources, handle));
    };
    auto bindStorage = [&commands](const PassResources& resources, uint32_t index,
                                   GraphBuffer handle, rojoRHI::StorageAccess access) {
        commands.bindStorageBuffer(index, lmx::render::buffer(resources, handle), access);
    };
    if (!view.tables.instances) {
        instances = graph.importBuffer(m_emptyInstances.rhi(), "lmx.visibility.emptyInstances");
        meshes = graph.importBuffer(m_emptyMeshes.rhi(), "lmx.visibility.emptyMeshes");
    }
    ComputePassDesc classify;
    classify.shaderBufferReads = {candidates, chunks, instances, views};
    if (view.occlusionEnabled)
        classify.shaderTextureReads.push_back(pyramid);
    classify.bufferWrites = {states, nextVersion(counters), counts};
    const auto chunkCount = static_cast<uint32_t>(tables.chunks.size());
    // The regions cover what retirement can read: each count is final at declaration.
    const uint64_t stateBytes = uint64_t{std::min(params.candidateCount, params.stateCapacity)} * 4;
    const uint64_t rowBytes = uint64_t{std::min(params.candidateCount, params.rowCapacity)} * 4;
    const uint64_t argumentBytes =
        uint64_t{std::min<uint32_t>(static_cast<uint32_t>(tables.runs.size()),
                                    params.argumentCapacity)} *
        sizeof(rojoRHI::DrawIndexedIndirectArgs);
    const engine::GpuBuffer* const stateOwner = &slot.states;
    const engine::GpuBuffer* const counterOwner = &slot.counters;
    const engine::GpuBuffer* const rowOwner = submission.scene.rows;
    const engine::GpuBuffer* const argumentOwner = submission.scene.arguments;
    graph.addComputePass(
        "lmx.pass.visibility.classify", std::move(classify),
        [=, this, &commands](const PassResources& resources) {
            commands.bindComputePipeline(*m_pipelines[view.occlusionEnabled ? 4 : 0]);
            if (view.occlusionEnabled) {
                commands.bindFrameData(13, occlusion);
                commands.bindTexture(0, lmx::render::texture(resources, pyramid));
            }
            bindRead(resources, 0, candidates);
            bindRead(resources, 2, chunks);
            bindRead(resources, 3, instances);
            bindRead(resources, 5, views);
            commands.bindFrameData(6, params);
            bindStorage(resources, 7, states, rojoRHI::StorageAccess::Write);
            bindStorage(resources, 8, nextVersion(counters), rojoRHI::StorageAccess::ReadWrite);
            bindStorage(resources, 9, counts, rojoRHI::StorageAccess::Write);
            commands.dispatch(std::max(1u, chunkCount), 1, 1);
            tickets->states = recordReadback(m_gpu, commands, *stateOwner, BufferUse::StorageWrite,
                                             {0, stateBytes});
        });
    if (params.layout) {
        ComputePassDesc scan;
        scan.shaderBufferReads = {runs, chunks, views};
        scan.bufferReads = {nextVersion(counts)};
        scan.bufferWrites = {offsets};
        const auto runCount = static_cast<uint32_t>(tables.runs.size());
        graph.addComputePass("lmx.pass.visibility.scan", std::move(scan),
                             [=, this, &commands](const PassResources& resources) {
                                 commands.bindComputePipeline(*m_pipelines[1]);
                                 bindRead(resources, 1, runs);
                                 bindRead(resources, 2, chunks);
                                 bindRead(resources, 5, views);
                                 bindStorage(resources, 9, nextVersion(counts),
                                             rojoRHI::StorageAccess::Read);
                                 bindStorage(resources, 10, offsets, rojoRHI::StorageAccess::Write);
                                 commands.dispatch(std::max(1u, runCount), 1, 1);
                             });
    }
    ComputePassDesc emit;
    emit.shaderBufferReads = {candidates, runs, chunks, instances, meshes, views};
    emit.bufferReads = {nextVersion(states)};
    if (params.layout)
        emit.bufferReads.push_back(nextVersion(offsets));
    emit.bufferWrites = {rows, arguments, nextVersion(nextVersion(counters))};
    graph.addComputePass(
        "lmx.pass.visibility.emit", std::move(emit),
        [=, this, &commands](const PassResources& resources) {
            commands.bindComputePipeline(*m_pipelines[params.layout ? 2 : 3]);
            bindRead(resources, 0, candidates);
            bindRead(resources, 1, runs);
            bindRead(resources, 2, chunks);
            bindRead(resources, 3, instances);
            bindRead(resources, 4, meshes);
            bindRead(resources, 5, views);
            commands.bindFrameData(6, params);
            bindStorage(resources, 7, nextVersion(states), rojoRHI::StorageAccess::Read);
            bindStorage(resources, 8, nextVersion(nextVersion(counters)),
                        rojoRHI::StorageAccess::ReadWrite);
            if (params.layout)
                bindStorage(resources, 10, nextVersion(offsets), rojoRHI::StorageAccess::Read);
            bindStorage(resources, 11, rows, rojoRHI::StorageAccess::Write);
            bindStorage(resources, 12, arguments, rojoRHI::StorageAccess::Write);
            commands.dispatch(std::max(1u, chunkCount), 1, 1);
            tickets->counters = recordReadback(m_gpu, commands, *counterOwner,
                                               BufferUse::StorageWrite, wholeBuffer(*counterOwner));
            tickets->rows =
                recordReadback(m_gpu, commands, *rowOwner, BufferUse::StorageWrite, {0, rowBytes});
            tickets->arguments = recordReadback(m_gpu, commands, *argumentOwner,
                                                BufferUse::StorageWrite, {0, argumentBytes});
        });
    graph.readbackBuffer(nextVersion(nextVersion(nextVersion(counters))));
    graph.readbackBuffer(nextVersion(states));
    graph.exportBuffer(nextVersion(rows));
    graph.exportBuffer(nextVersion(arguments));
    slot.used = true;
    return {nextVersion(rows), nextVersion(arguments)};
}
} // namespace lmx::render
