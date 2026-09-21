//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibility.cpp
/// @brief Allocates paced visibility tables and declares deterministic GPU work generation.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/GpuVisibility.h"
#include "Core/Diagnostics/Assert.h"
#include "Render/Renderer/SceneView.h"
#include <algorithm>
#include <cstring>

namespace lmx::render {
//======================================================================================================================
rojoRHI::Result<std::unique_ptr<GpuVisibility>> GpuVisibility::create(rojoRHI::Device& device) {
    std::unique_ptr<GpuVisibility> self(new GpuVisibility(device));
    constexpr std::array names{"VisibilityClassify", "VisibilityScan", "VisibilityEmit",
                               "VisibilityEmitSparse", "VisibilityClassifyOcclusion"};
    for (uint32_t i = 0; i < names.size(); ++i) {
        auto library = device.loadShaderLibrary("Shaders/" + std::string(names[i]));
        if (!library)
            return std::unexpected(library.error());
        self->m_libraries[i] = std::move(*library);
        auto pipeline =
            device.createComputePipeline({.library = self->m_libraries[i].get(),
                                          .computeEntry = "computeMain",
                                          .threadsPerThreadgroup = {256, 1, 1},
                                          .label = "lmx.visibility." + std::string(names[i])});
        if (!pipeline)
            return std::unexpected(pipeline.error());
        self->m_pipelines[i] = std::move(*pipeline);
    }
    const engine::InstanceRow emptyInstance;
    const engine::MeshRow emptyMesh;
    auto instance = device.createBuffer(
        {.size = sizeof(emptyInstance), .label = "lmx.visibility.emptyInstances"}, &emptyInstance);
    if (!instance)
        return std::unexpected(instance.error());
    self->m_emptyInstances = std::move(*instance);
    auto mesh = device.createBuffer(
        {.size = sizeof(emptyMesh), .label = "lmx.visibility.emptyMeshes"}, &emptyMesh);
    if (!mesh)
        return std::unexpected(mesh.error());
    self->m_emptyMeshes = std::move(*mesh);
    return self;
}

//======================================================================================================================
rojoRHI::Result<void> GpuVisibility::prepareSlot(Slot& slot, const VisibilityTables& tables) {
    const auto needed = std::max<uint32_t>(1, static_cast<uint32_t>(tables.candidates.size()));
    if (needed > slot.capacity) {
        uint32_t capacity = std::max(1u, slot.capacity);
        while (capacity < needed)
            capacity *= 2;
        Slot replacement;
        auto allocate = [&](std::unique_ptr<rojoRHI::Buffer>& buffer, uint64_t bytes,
                            const char* name, bool writable) -> rojoRHI::Result<void> {
            auto result = m_device.createBuffer(
                {.size = bytes,
                 .storageRead = true,
                 .storageWrite = writable,
                 .cpuReadback = writable,
                 .cpuWrite = !writable,
                 .label = std::string(name) + "." + std::to_string(m_device.frameNumber() % 3)},
                nullptr);
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
        result = allocate(replacement.counters, 160, "lmx.draw.counters", true);
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
    upload(*slot.candidates, slot.candidateBytes, tables.candidates);
    upload(*slot.runs, slot.runBytes, tables.runs);
    upload(*slot.chunks, slot.chunkBytes, tables.chunks);
    upload(*slot.views, slot.viewBytes, tables.views);
    return {};
}

//======================================================================================================================
GpuVisibilityOutputs GpuVisibility::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                            const SceneView& view, const FrustumPlanes& planes,
                                            const PreparedSubmission& submission,
                                            GraphBuffer instances, GraphBuffer meshes,
                                            GraphBuffer rows, GraphBuffer arguments,
                                            VisibilityStatus& status, GraphTexture pyramid,
                                            OcclusionParams occlusion) {
    auto tables = buildVisibilityTables(view, submission, planes);
    auto& slot = m_slots[m_device.frameNumber() % 3];
    const auto prepared = prepareSlot(slot, tables);
    LMX_ASSERT(prepared.has_value(), prepared.error().message);
    VisibilityParams params{.layout = view.submission == SubmissionMode::Batched ? 1u : 0u,
                            .rowCapacity = static_cast<uint32_t>(submission.scene.rows->size() / 4),
                            .argumentCapacity =
                                static_cast<uint32_t>(submission.scene.arguments->size() / 20),
                            .stateCapacity = slot.capacity,
                            .candidateCount = static_cast<uint32_t>(tables.candidates.size())};
    if (m_capacityOverride) {
        params.rowCapacity = std::min(params.rowCapacity, (*m_capacityOverride)[0]);
        params.argumentCapacity = std::min(params.argumentCapacity, (*m_capacityOverride)[1]);
        params.stateCapacity = std::min(params.stateCapacity, (*m_capacityOverride)[2]);
    }
    status.submission.candidateBytes = tables.candidates.size() * sizeof(CandidateRecord);
    status.submission.runBytes = tables.runs.size() * sizeof(RunRecord);
    status.submission.chunkBytes = tables.chunks.size() * sizeof(ChunkRecord);
    status.submission.stateBytes = uint64_t{slot.capacity} * 4;
    status.submission.counterBytes = 160;
    Pending pending{.status = status,
                    .tables = tables,
                    .params = params,
                    .geometry = submission.arguments,
                    .states = slot.states.get(),
                    .counters = slot.counters.get(),
                    .rows = submission.scene.rows,
                    .arguments = submission.scene.arguments};
    if (view.classifyCheck)
        for (uint32_t v = 0; v < 2; ++v)
            for (uint32_t i = 0; i < tables.views[v].candidateCount; ++i) {
                const auto row = tables.candidates[tables.views[v].firstCandidate + i].instanceRow;
                pending.expected.push_back(classifyInstance(planes, view.tables.instanceRows[row],
                                                            row, view.visibilityEnabled, v == 1));
            }
    m_pending.push_back(std::move(pending));
    auto import = [&](rojoRHI::Buffer& buffer, const char* name, rojoRHI::BufferUse use) {
        return slot.used ? graph.importBuffer(buffer, name, use) : graph.importBuffer(buffer, name);
    };
    const auto candidates = graph.importBuffer(*slot.candidates, "lmx.draw.candidates");
    const auto runs = graph.importBuffer(*slot.runs, "lmx.draw.runs");
    const auto chunks = graph.importBuffer(*slot.chunks, "lmx.draw.chunks");
    const auto views = graph.importBuffer(*slot.views, "lmx.draw.views");
    const auto states = import(*slot.states, "lmx.draw.states", rojoRHI::BufferUse::StorageRead);
    const auto counters =
        import(*slot.counters, "lmx.draw.counters", rojoRHI::BufferUse::StorageWrite);
    const auto chunkBytes = std::max<uint64_t>(4, tables.chunks.size() * 4);
    const auto counts = graph.createBuffer(
        {.size = chunkBytes, .storageRead = true, .storageWrite = true}, "lmx.draw.chunkCounts");
    const auto offsets =
        params.layout
            ? graph.createBuffer({.size = chunkBytes, .storageRead = true, .storageWrite = true},
                                 "lmx.draw.chunkOffsets")
            : GraphBuffer{};
    CopyPassDesc reset;
    reset.bufferDestinations.push_back(counters);
    graph.addCopyPass("lmx.pass.visibility.reset", std::move(reset),
                      [&commands, counters](const PassResources& resources) {
                          commands.fillBuffer(**resources.buffer(counters), 0, 160, 0);
                      });
    auto bindRead = [&commands](const PassResources& resources, uint32_t index,
                                GraphBuffer handle) {
        commands.bindBuffer(index, **resources.buffer(handle));
    };
    auto bindStorage = [&commands](const PassResources& resources, uint32_t index,
                                   GraphBuffer handle, rojoRHI::StorageAccess access) {
        commands.bindStorageBuffer(index, **resources.buffer(handle), access);
    };
    if (!view.tables.instances) {
        instances = graph.importBuffer(*m_emptyInstances, "lmx.visibility.emptyInstances");
        meshes = graph.importBuffer(*m_emptyMeshes, "lmx.visibility.emptyMeshes");
    }
    ComputePassDesc classify;
    classify.shaderBufferReads = {candidates, chunks, instances, views};
    if (view.occlusionEnabled)
        classify.shaderTextureReads.push_back(pyramid);
    classify.bufferWrites = {states, nextVersion(counters), counts};
    const auto chunkCount = static_cast<uint32_t>(tables.chunks.size());
    graph.addComputePass(
        "lmx.pass.visibility.classify", std::move(classify),
        [=, this, &commands](const PassResources& resources) {
            commands.bindComputePipeline(*m_pipelines[view.occlusionEnabled ? 4 : 0]);
            if (view.occlusionEnabled) {
                commands.bindFrameData(13, occlusion);
                commands.bindTexture(0, **resources.texture(pyramid));
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
        });
    graph.readbackBuffer(nextVersion(nextVersion(nextVersion(counters))));
    graph.readbackBuffer(nextVersion(states));
    graph.exportBuffer(nextVersion(rows));
    graph.exportBuffer(nextVersion(arguments));
    slot.used = true;
    return {nextVersion(rows), nextVersion(arguments)};
}
} // namespace lmx::render
