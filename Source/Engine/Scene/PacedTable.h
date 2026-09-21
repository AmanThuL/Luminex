//----------------------------------------------------------------------------------------------------------------------
/// @file PacedTable.h
/// @brief Declares the triple-paced GPU row table and its growth and upload helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Containers/DirtySet.h"
#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Engine/Scene/SceneTableStats.h"
#include <rojoRHI/RHI.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::engine {

/// Paced copies of every table, one per frame in flight.
inline constexpr uint32_t kSlots = 3;
/// Smallest row capacity a table grows to.
inline constexpr uint32_t kMinimumCapacity = 4;

/// A replaced GPU buffer kept alive until the frame that last read it has retired.
struct RetiringBuffer {
    std::unique_ptr<rojoRHI::Buffer> buffer;
    uint64_t releaseAtFrame = 0;
};

/// One row kind's paced GPU buffers, its CPU shadow rows and their per-slot dirty marks.
template <typename Row>
struct PacedTable {
    std::array<std::unique_ptr<rojoRHI::Buffer>, kSlots> buffers;
    std::vector<Row> shadow;
    DirtySet dirty{kSlots};
    uint32_t capacity = 0;
};

/// Grows `table` to hold `count` rows, doubling from at least `kMinimumCapacity`. A grown
/// table's previous buffers join `retiring` until `lastFrame + kSlots` and count one growth
/// event; every shadow row becomes dirty in every slot. Returns buffer-creation failure.
template <typename Row>
rojoRHI::Result<void> reserveTable(rojoRHI::Device& device, PacedTable<Row>& table, uint32_t count,
                                   std::string_view label, uint64_t lastFrame,
                                   std::vector<RetiringBuffer>& retiring, uint64_t& growthEvents) {
    if (count <= table.capacity && table.capacity != 0) {
        return {};
    }
    uint32_t capacity = std::max(table.capacity, kMinimumCapacity);
    while (capacity < count) {
        LMX_ASSERT(capacity <= std::numeric_limits<uint32_t>::max() / 2,
                   "scene capacity exhausted");
        capacity *= 2;
    }
    std::array<std::unique_ptr<rojoRHI::Buffer>, kSlots> buffers;
    for (uint32_t slot = 0; slot < kSlots; ++slot) {
        auto buffer = device.createBuffer({.size = uint64_t{capacity} * sizeof(Row),
                                           .storageRead = true,
                                           .cpuReadback = true,
                                           .cpuWrite = true,
                                           .label = std::format("{}.{}", label, slot)},
                                          nullptr);
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        buffers[slot] = std::move(*buffer);
    }
    if (table.capacity != 0) {
        LMX_ASSERT(lastFrame <= std::numeric_limits<uint64_t>::max() - kSlots,
                   "frame counter exhausted");
        for (auto& buffer : table.buffers) {
            retiring.push_back({std::move(buffer), lastFrame + kSlots});
        }
        ++growthEvents;
        LMX_LOG_INFO("{} grew from {} to {} rows; old buffers retire at frame {}", label,
                     table.capacity, capacity, lastFrame + kSlots);
    }
    table.buffers = std::move(buffers);
    table.capacity = capacity;
    table.shadow.resize(count);
    table.dirty.assign(count);
    return {};
}

/// Stores `row` at `index`, marking it dirty in every slot only when its bytes changed.
template <typename Row>
void updateRow(PacedTable<Row>& table, uint32_t index, const Row& row) {
    if (index >= table.shadow.size()) {
        table.shadow.resize(index + 1);
        table.dirty.resize(index + 1);
    }
    if (std::memcmp(&table.shadow[index], &row, sizeof(Row)) != 0) {
        table.shadow[index] = row;
        table.dirty.markAll(index);
    }
}

/// Uploads `slot`'s dirty rows into that slot's buffer, clears them and counts the writes.
template <typename Row>
void writeRows(PacedTable<Row>& table, uint32_t slot, SceneTableStats& stats) {
    for (uint32_t i = 0; i < table.shadow.size(); ++i) {
        if (table.dirty.test(i, slot)) {
            table.buffers[slot]->write(uint64_t{i} * sizeof(Row), &table.shadow[i], sizeof(Row));
            table.dirty.clear(i, slot);
            ++stats.rowsWritten;
            stats.bytesWritten += sizeof(Row);
        }
    }
}

} // namespace lmx::engine
