//----------------------------------------------------------------------------------------------------------------------
/// @file DrawSubmission.cpp
/// @brief Builds deterministic command runs and uploads only retired submission slots.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Scene/DrawSubmission.h"
#include "Core/Diagnostics/Assert.h"
#include "Render/Renderer/SceneView.h"
#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <tuple>

namespace lmx::render {

//======================================================================================================================
PreparedSubmission buildDrawSubmission(const SceneView& view, const VisibilityResult& scene,
                                       const VisibilityResult& shadow, SubmissionMode mode) {
    PreparedSubmission result;
    auto key = [&](uint32_t index) {
        const auto& item = view.items[index];
        const auto& row = view.tables.instanceRows[item.instanceRow];
        return std::tuple{item.alphaMode, item.doubleSided, row.materialRow, row.meshRow};
    };
    auto append = [&](const VisibilityResult& visibility, DrawList& list) {
        list.mode = mode;
        list.firstEntry = static_cast<uint32_t>(result.rows.size());
        list.entryCount = static_cast<uint32_t>(visibility.visibleItems.size());
        auto indices = visibility.visibleItems;
        if (mode == SubmissionMode::Batched)
            std::stable_sort(indices.begin(), indices.end(),
                             [&](auto a, auto b) { return key(a) < key(b); });
        for (uint32_t cursor = 0; cursor < indices.size();) {
            const uint32_t itemIndex = indices[cursor];
            uint32_t end = cursor + 1;
            if (mode == SubmissionMode::Batched)
                while (end < indices.size() && key(indices[end]) == key(itemIndex))
                    ++end;
            const uint32_t firstEntry = static_cast<uint32_t>(result.rows.size());
            for (uint32_t i = cursor; i < end; ++i)
                result.rows.push_back(view.items[indices[i]].instanceRow);
            const auto& mesh = view.items[itemIndex].mesh;
            list.runs.push_back({itemIndex, firstEntry, end - cursor,
                                 static_cast<uint32_t>(result.arguments.size())});
            result.arguments.push_back({.drawId = firstEntry,
                                        .indexCount = mesh.indexCount,
                                        .instanceCount = end - cursor,
                                        .firstIndex = mesh.firstIndex,
                                        .baseVertex = 0,
                                        .firstInstance = 0});
            cursor = end;
        }
    };
    append(scene, result.scene);
    append(shadow, result.shadow);
    return result;
}

//======================================================================================================================
DrawSubmission::DrawSubmission(rojoRHI::Device& device) : m_device(device) {}

//======================================================================================================================
lmx::Result<void> DrawSubmission::prepare(rojoRHI::CommandList& commands, uint64_t frameNumber,
                                          const SceneView& view, const VisibilityResult& scene,
                                          const VisibilityResult& shadow) {
    LMX_ASSERT(frameNumber > m_lastFrame,
               "submission preparation requires a newly paced device frame");
    std::erase_if(m_retiring,
                  [frameNumber](const auto& entry) { return frameNumber >= entry.releaseFrame; });
    VisibilityResult all;
    if (view.classifyMode == ClassifyMode::Gpu) {
        LMX_ASSERT(view.submission != SubmissionMode::Direct,
                   "GPU classification requires indirect submission");
        for (uint32_t i = 0; i < view.items.size(); ++i)
            all.visibleItems.push_back(i);
    }
    auto prepared = view.classifyMode == ClassifyMode::Gpu
                        ? buildDrawSubmission(view, all, all, view.submission)
                        : buildDrawSubmission(view, scene, shadow, view.submission);
    const uint64_t required = std::max<uint64_t>(
        1, std::max<uint64_t>(prepared.rows.size(), uint64_t{view.tables.instanceCapacity} * 2));
    if (required > m_capacity) {
        uint64_t capacity = std::max(1u, m_capacity);
        // No uint32 capacity can grow past this first unsupported power of two. Bounding the
        // loop also keeps both byte products representable even for an oversized CPU input.
        while (capacity < required && capacity <= std::numeric_limits<uint32_t>::max()) {
            capacity *= 2;
        }
        const uint64_t rowBytes = capacity * sizeof(uint32_t);
        const uint64_t argumentBytes = capacity * sizeof(rojoRHI::DrawIndexedIndirectArgs);
        const auto& limits = m_device.capabilities().limits;
        // maxAddrOffset is exclusive: the final addressed byte must be strictly below it.
        if (capacity > std::numeric_limits<uint32_t>::max() || rowBytes > limits.maxBufferSize ||
            argumentBytes > limits.maxBufferSize || rowBytes > limits.maxAddrOffset ||
            argumentBytes > limits.maxAddrOffset) {
            return std::unexpected(lmx::Error{
                ErrorCode::InvalidArgument,
                std::format(
                    "draw submission requested {} entries; capacity {} requires rowBytes={} "
                    "argumentBytes={}; "
                    "maxBufferSize={} maxAddrOffset={} (exclusive), uint32 capacity required",
                    required, capacity, rowBytes, argumentBytes, limits.maxBufferSize,
                    limits.maxAddrOffset)});
        }
        std::array<Slot, 3> replacements;
        for (uint32_t i = 0; i < replacements.size(); ++i) {
            const auto suffix = std::to_string(i);
            const std::string rowsLabel = "lmx.draw.rows." + suffix;
            auto rows =
                engine::GpuBuffer::create(m_device, {.size = rowBytes,
                                                     .memory = rojoRHI::Memory::Gpu,
                                                     .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                     .label = rowsLabel});
            if (!rows) {
                return std::unexpected(rows.error());
            }
            const std::string argsLabel = "lmx.draw.args." + suffix;
            auto args =
                engine::GpuBuffer::create(m_device, {.size = argumentBytes,
                                                     .memory = rojoRHI::Memory::Gpu,
                                                     .usage = rojoRHI::BufferUsage::ShaderWrite |
                                                              rojoRHI::BufferUsage::IndirectArgs,
                                                     .label = argsLabel});
            if (!args) {
                return std::unexpected(args.error());
            }
            const std::string rowUploadLabel = "lmx.draw.rows.upload." + suffix;
            auto rowUpload = engine::GpuBuffer::create(
                m_device,
                {.size = rowBytes, .memory = rojoRHI::Memory::Upload, .label = rowUploadLabel});
            if (!rowUpload) {
                return std::unexpected(rowUpload.error());
            }
            const std::string argumentUploadLabel = "lmx.draw.args.upload." + suffix;
            auto argumentUpload =
                engine::GpuBuffer::create(m_device, {.size = argumentBytes,
                                                     .memory = rojoRHI::Memory::Upload,
                                                     .label = argumentUploadLabel});
            if (!argumentUpload) {
                return std::unexpected(argumentUpload.error());
            }
            replacements[i] = {.rows = std::move(*rows),
                               .arguments = std::move(*args),
                               .rowUpload = std::move(*rowUpload),
                               .argumentUpload = std::move(*argumentUpload),
                               .rowUse = std::nullopt,
                               .argumentUse = std::nullopt};
        }
        LMX_ASSERT(m_lastFrame <= std::numeric_limits<uint64_t>::max() - m_slots.size(),
                   "frame counter exhausted");
        m_retiring.reserve(m_retiring.size() + m_slots.size());
        for (auto& slot : m_slots)
            if (slot.rows)
                m_retiring.push_back({m_lastFrame + 3, std::move(slot)});
        m_slots = std::move(replacements);
        m_capacity = static_cast<uint32_t>(capacity);
        ++m_stats.growthEvents;
    }
    auto& slot = m_slots[frameNumber % m_slots.size()];
    if (view.classifyMode == ClassifyMode::Cpu) {
        std::array<rojoRHI::BufferBarrier, 2> barriers;
        uint32_t barrierCount = 0;
        if (!prepared.rows.empty()) {
            const uint64_t bytes = prepared.rows.size() * sizeof(uint32_t);
            std::memcpy(slot.rowUpload.cpuPointer(), prepared.rows.data(), bytes);
            commands.copyBuffer(slot.rowUpload.rhi(), 0, slot.rows.rhi(), 0, bytes);
            barriers[barrierCount++] = {.buffer = slot.rows.rhi(),
                                        .srcStage = rojoRHI::Stage::Copy,
                                        .srcAccess = rojoRHI::Access::CopyWrite,
                                        .dstStage = rojoRHI::Stage::VertexShader,
                                        .dstAccess = rojoRHI::Access::ShaderRead};
        }
        if (!prepared.arguments.empty()) {
            const uint64_t bytes =
                prepared.arguments.size() * sizeof(rojoRHI::DrawIndexedIndirectArgs);
            std::memcpy(slot.argumentUpload.cpuPointer(), prepared.arguments.data(), bytes);
            commands.copyBuffer(slot.argumentUpload.rhi(), 0, slot.arguments.rhi(), 0, bytes);
            barriers[barrierCount++] = {.buffer = slot.arguments.rhi(),
                                        .srcStage = rojoRHI::Stage::Copy,
                                        .srcAccess = rojoRHI::Access::CopyWrite,
                                        .dstStage = rojoRHI::Stage::IndirectArgs,
                                        .dstAccess = rojoRHI::Access::IndirectArgs};
        }
        if (barrierCount)
            commands.barrier({}, std::span{barriers.data(), barrierCount}, {});
    }
    prepared.scene.rows = prepared.shadow.rows = &slot.rows;
    prepared.scene.arguments = prepared.shadow.arguments = &slot.arguments;
    m_stats.sceneCommands = static_cast<uint32_t>(prepared.scene.runs.size());
    m_stats.shadowCommands = static_cast<uint32_t>(prepared.shadow.runs.size());
    m_stats.instancedRuns = 0;
    for (const auto* list : {&prepared.scene, &prepared.shadow})
        for (const auto& run : list->runs)
            m_stats.instancedRuns += run.instanceCount > 1;
    m_stats.listBytes = prepared.rows.size() * sizeof(uint32_t);
    m_stats.argumentBytes = prepared.arguments.size() * sizeof(rojoRHI::DrawIndexedIndirectArgs);
    m_stats.allocatedListBytes = uint64_t{m_capacity} * sizeof(uint32_t) * m_slots.size();
    m_stats.allocatedArgumentBytes =
        uint64_t{m_capacity} * sizeof(rojoRHI::DrawIndexedIndirectArgs) * m_slots.size();
    m_stats.allocatedUploadBytes = m_stats.allocatedListBytes + m_stats.allocatedArgumentBytes;
    std::erase_if(m_retiring,
                  [frameNumber](const auto& entry) { return frameNumber >= entry.releaseFrame; });
    m_stats.pendingReleaseBuffers = static_cast<uint32_t>(m_retiring.size() * 2);
    m_stats.pendingReleaseUploadBuffers = static_cast<uint32_t>(m_retiring.size() * 2);
    m_prepared = std::move(prepared);
    m_lastFrame = frameNumber;
    return {};
}
//======================================================================================================================
void DrawSubmission::recordUses() {
    auto& slot = m_slots[m_lastFrame % 3];
    // Both stages declare these reads even when their command list is empty.
    slot.rowUse = BufferUse::ShaderRead;
    slot.argumentUse = BufferUse::IndirectArgument;
}
} // namespace lmx::render
