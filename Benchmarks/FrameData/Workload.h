//----------------------------------------------------------------------------------------------------------------------
/// @file Workload.h
/// @brief Declares the five frozen FrameDataBench workloads.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string_view>

namespace lmx::bench {

/// Distinguishes the two frame-data delivery shapes the frozen workload set exercises (spec section
/// 11): Dynamic re-delivers a changing per-draw block through deliverPerDrawData every draw of
/// every frame; Static binds an already-created, unwritten buffer and writes zero per-frame
/// frame-data bytes.
enum class WorkloadKind {
    Dynamic, ///< Re-delivers a changing per-draw block of `blockSize` bytes every draw.
    Static,  ///< Binds a buffer created once before timing; no per-frame data delivery.
};

/// One frozen workload's shape: how many disjoint quads it draws per frame, its delivery kind, and
/// -- for a Dynamic workload -- the exact size of the per-draw block re-delivered every draw.
struct WorkloadSpec {
    std::string_view name;                     ///< Exact case name, e.g. "F-FIT-512".
    uint32_t drawCount = 0;                    ///< Disjoint quads drawn per frame.
    WorkloadKind kind = WorkloadKind::Dynamic; ///< Delivery shape.
    uint64_t blockSize = 0; ///< Dynamic per-draw block size in bytes; 0 for Static.
};

/// The five frozen cases from docs/specs/2026-08-12-m5.2-rhi-frame-data-design.md section 11, in
/// the spec table's order. Frozen: do not add, remove, reorder, or resize without updating that
/// spec and the milestone plan it decomposes.
inline constexpr WorkloadSpec kWorkloads[] = {
    {.name = "F-FIT-512", .drawCount = 512, .kind = WorkloadKind::Dynamic, .blockSize = 64},
    {.name = "F-DYNAMIC-1024", .drawCount = 1024, .kind = WorkloadKind::Dynamic, .blockSize = 304},
    {.name = "F-DYNAMIC-4096", .drawCount = 4096, .kind = WorkloadKind::Dynamic, .blockSize = 64},
    {.name = "F-STATIC-1024", .drawCount = 1024, .kind = WorkloadKind::Static, .blockSize = 0},
    {.name = "F-STATIC-4096", .drawCount = 4096, .kind = WorkloadKind::Static, .blockSize = 0},
};

/// Finds a frozen workload by exact name, or returns nullptr when `name` matches none of
/// kWorkloads.
const WorkloadSpec* findWorkload(std::string_view name);

} // namespace lmx::bench
