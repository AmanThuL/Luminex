//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityTables.h
/// @brief Declares the fixed GPU visibility ABI and canonical candidate tables.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Render/Passes/Scene/DrawSubmission.h"
#include <cstddef>

namespace lmx::render {
/// Counter storage per view, including three padding words.
inline constexpr uint32_t kVisibilityCounterWords = 20;
/// One threadgroup's maximum candidate count.
inline constexpr uint32_t kVisibilityChunkSize = 256;
/// Stable instance row and absolute run index.
struct CandidateRecord {
    uint32_t instanceRow = 0; ///< Stable scene table row.
    uint32_t run = 0;         ///< Absolute run index.
};
/// A canonical compatible command and its reserved output range.
struct RunRecord {
    uint32_t firstCandidate = 0; ///< Absolute first candidate.
    uint32_t candidateCount = 0; ///< Candidates reserved by this run.
    uint32_t firstSlot = 0;      ///< Absolute first output row.
    uint32_t argumentIndex = 0;  ///< Absolute output argument.
};
/// Threadgroup input range; dense chunks never cross run boundaries.
struct ChunkRecord {
    uint32_t run = 0;            ///< Absolute run index; unused by sparse chunks.
    uint32_t firstCandidate = 0; ///< Absolute first candidate.
    uint32_t candidateCount = 0; ///< At most kVisibilityChunkSize.
    uint32_t padding = 0;        ///< Explicit ABI padding.
};
/// Per-view planes, flags and absolute table ranges.
struct VisibilityViewParams {
    std::array<glm::vec4, 5> planes{}; ///< Guarded inward clipping planes.
    uint32_t flags = 0;          ///< Bit zero enabled, bit one unculled, bit two valid planes.
    uint32_t firstCandidate = 0; ///< Absolute first candidate.
    uint32_t candidateCount = 0; ///< Candidate range length.
    uint32_t firstRun = 0;       ///< Absolute first run.
    uint32_t runCount = 0;       ///< Run range length.
    uint32_t firstChunk = 0;     ///< Absolute first chunk.
    uint32_t chunkCount = 0;     ///< Chunk range length.
    uint32_t padding = 0;        ///< Explicit ABI padding.
};
/// Explicit physical write capacities and dispatch layout.
struct VisibilityParams {
    uint32_t layout = 0;               ///< Zero sparse, one dense.
    uint32_t rowCapacity = 0;          ///< Writable row count.
    uint32_t argumentCapacity = 0;     ///< Writable argument count.
    uint32_t stateCapacity = 0;        ///< Writable state count.
    uint32_t candidateCount = 0;       ///< Combined candidates.
    uint32_t viewCount = 2;            ///< Scene then unculled shadow.
    std::array<uint32_t, 2> padding{}; ///< Explicit ABI padding.
};
/// Combined scene-then-shadow tables and fixed CPU command bindings.
struct VisibilityTables {
    std::vector<CandidateRecord> candidates;     ///< Canonical candidate order.
    std::vector<RunRecord> runs;                 ///< Canonical compatible runs.
    std::vector<ChunkRecord> chunks;             ///< Threadgroup ranges.
    std::array<VisibilityViewParams, 2> views{}; ///< Scene and shadow ranges.
};
/// Builds canonical tables from an unculled fixed submission, without classification.
VisibilityTables buildVisibilityTables(const SceneView& view, const PreparedSubmission& submission,
                                       const FrustumPlanes& planes);
/// Registers the fixed visibility ABI for labelled capture inspection.
void registerVisibilityLayoutsForCapture();
static_assert(offsetof(CandidateRecord, instanceRow) == 0);
static_assert(offsetof(RunRecord, firstCandidate) == 0);
static_assert(offsetof(RunRecord, candidateCount) == 4);
static_assert(offsetof(RunRecord, firstSlot) == 8);
static_assert(offsetof(ChunkRecord, run) == 0);
static_assert(offsetof(ChunkRecord, firstCandidate) == 4);
static_assert(offsetof(ChunkRecord, candidateCount) == 8);
static_assert(offsetof(VisibilityViewParams, planes) == 0);
static_assert(offsetof(VisibilityViewParams, padding) == 108);
static_assert(offsetof(VisibilityParams, layout) == 0);
static_assert(offsetof(VisibilityParams, rowCapacity) == 4);
static_assert(offsetof(VisibilityParams, argumentCapacity) == 8);
static_assert(offsetof(VisibilityParams, stateCapacity) == 12);
static_assert(offsetof(VisibilityParams, viewCount) == 20);
static_assert(offsetof(VisibilityParams, padding) == 24);
static_assert(sizeof(CandidateRecord) == 8 && offsetof(CandidateRecord, run) == 4);
static_assert(sizeof(RunRecord) == 16 && offsetof(RunRecord, argumentIndex) == 12);
static_assert(sizeof(ChunkRecord) == 16 && offsetof(ChunkRecord, padding) == 12);
static_assert(sizeof(VisibilityViewParams) == 112 && offsetof(VisibilityViewParams, flags) == 80);
static_assert(offsetof(VisibilityViewParams, firstCandidate) == 84);
static_assert(offsetof(VisibilityViewParams, candidateCount) == 88);
static_assert(offsetof(VisibilityViewParams, firstRun) == 92);
static_assert(offsetof(VisibilityViewParams, runCount) == 96);
static_assert(offsetof(VisibilityViewParams, firstChunk) == 100);
static_assert(offsetof(VisibilityViewParams, chunkCount) == 104);
static_assert(sizeof(VisibilityParams) == 32 && offsetof(VisibilityParams, candidateCount) == 16);
} // namespace lmx::render
