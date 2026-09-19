//----------------------------------------------------------------------------------------------------------------------
/// @file DrawSubmission.cpp
/// @brief Builds deterministic command runs and uploads only retired submission slots.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/DrawSubmission.h"
#include "Core/Assert.h"
#include "Render/SceneView.h"
#include <algorithm>
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
            result.arguments.push_back({.indexCount = mesh.indexCount,
                                        .instanceCount = end - cursor,
                                        .firstIndex = mesh.firstIndex,
                                        .baseVertex = 0,
                                        .firstInstance = firstEntry});
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
rojoRHI::Result<void> DrawSubmission::prepare(uint64_t frameNumber, const SceneView& view,
                                          const VisibilityResult& scene,
                                          const VisibilityResult& shadow) {
    LMX_ASSERT(frameNumber == m_device.frameNumber() && frameNumber > m_lastFrame,
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
    LMX_ASSERT(required <= std::numeric_limits<uint32_t>::max(), "submission capacity exhausted");
    if (required > m_capacity) {
        uint32_t capacity = std::max(1u, m_capacity);
        while (capacity < required) {
            LMX_ASSERT(capacity <= std::numeric_limits<uint32_t>::max() / 2,
                       "submission growth exhausted");
            capacity *= 2;
        }
        std::array<Slot, 3> replacements;
        for (uint32_t i = 0; i < replacements.size(); ++i) {
            const auto suffix = std::to_string(i);
            auto rows = m_device.createBuffer({.size = uint64_t{capacity} * sizeof(uint32_t),
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = true,
                                               .cpuWrite = true,
                                               .label = "lmx.draw.rows." + suffix},
                                              nullptr);
            if (!rows)
                return std::unexpected(rows.error());
            auto args = m_device.createBuffer(
                {.size = uint64_t{capacity} * sizeof(rojoRHI::DrawIndexedIndirectArgs),
                 .storageWrite = true,
                 .cpuReadback = true,
                 .cpuWrite = true,
                 .label = "lmx.draw.args." + suffix},
                nullptr);
            if (!args)
                return std::unexpected(args.error());
            replacements[i] = {.rows = std::move(*rows),
                               .arguments = std::move(*args),
                               .rowUse = std::nullopt,
                               .argumentUse = std::nullopt};
        }
        for (auto& slot : m_slots)
            if (slot.rows)
                m_retiring.push_back({m_lastFrame + 3, std::move(slot)});
        m_slots = std::move(replacements);
        m_capacity = capacity;
        ++m_stats.growthEvents;
    }
    auto& slot = m_slots[frameNumber % m_slots.size()];
    if (view.classifyMode == ClassifyMode::Cpu && !prepared.rows.empty())
        slot.rows->write(0, prepared.rows.data(), prepared.rows.size() * sizeof(uint32_t));
    if (view.classifyMode == ClassifyMode::Cpu && !prepared.arguments.empty())
        slot.arguments->write(0, prepared.arguments.data(),
                              prepared.arguments.size() * sizeof(rojoRHI::DrawIndexedIndirectArgs));
    prepared.scene.rows = prepared.shadow.rows = slot.rows.get();
    prepared.scene.arguments = prepared.shadow.arguments = slot.arguments.get();
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
    std::erase_if(m_retiring,
                  [frameNumber](const auto& entry) { return frameNumber >= entry.releaseFrame; });
    m_stats.pendingReleaseBuffers = static_cast<uint32_t>(m_retiring.size() * 2);
    m_prepared = std::move(prepared);
    m_lastFrame = frameNumber;
    return {};
}
//======================================================================================================================
void DrawSubmission::recordUses() {
    auto& slot = m_slots[m_lastFrame % 3];
    // Both stages declare these reads even when their command list is empty.
    slot.rowUse = rojoRHI::BufferUse::ShaderRead;
    slot.argumentUse = rojoRHI::BufferUse::IndirectArgument;
}
} // namespace lmx::render
