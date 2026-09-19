//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityCapture.cpp
/// @brief Registers GPU visibility record layouts for labelled capture inspection.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/VisibilityTables.h"
#include <rojoRHI/CaptureSchema.h>

namespace lmx::render {
//======================================================================================================================
void registerVisibilityLayoutsForCapture() {
    auto& schema = rojoRHI::debug::CaptureSchema::instance();
    schema.registerUniformStruct(
        {.name = "CandidateRecord",
         .slot = 0,
         .sizeBytes = sizeof(CandidateRecord),
         .fields = {{"instanceRow", offsetof(CandidateRecord, instanceRow), "uint"},
                    {"run", offsetof(CandidateRecord, run), "uint"}}});
    schema.registerUniformStruct(
        {.name = "RunRecord",
         .slot = 1,
         .sizeBytes = sizeof(RunRecord),
         .fields = {{"firstCandidate", offsetof(RunRecord, firstCandidate), "uint"},
                    {"candidateCount", offsetof(RunRecord, candidateCount), "uint"},
                    {"firstSlot", offsetof(RunRecord, firstSlot), "uint"},
                    {"argumentIndex", offsetof(RunRecord, argumentIndex), "uint"}}});
    schema.registerUniformStruct(
        {.name = "ChunkRecord",
         .slot = 2,
         .sizeBytes = sizeof(ChunkRecord),
         .fields = {{"run", offsetof(ChunkRecord, run), "uint"},
                    {"firstCandidate", offsetof(ChunkRecord, firstCandidate), "uint"},
                    {"candidateCount", offsetof(ChunkRecord, candidateCount), "uint"},
                    {"padding", offsetof(ChunkRecord, padding), "uint"}}});
    schema.registerUniformStruct(
        {.name = "VisibilityViewParams",
         .slot = 5,
         .sizeBytes = sizeof(VisibilityViewParams),
         .fields = {{"planes", offsetof(VisibilityViewParams, planes), "float4[5]"},
                    {"flags", offsetof(VisibilityViewParams, flags), "uint"},
                    {"firstCandidate", offsetof(VisibilityViewParams, firstCandidate), "uint"},
                    {"candidateCount", offsetof(VisibilityViewParams, candidateCount), "uint"},
                    {"firstRun", offsetof(VisibilityViewParams, firstRun), "uint"},
                    {"runCount", offsetof(VisibilityViewParams, runCount), "uint"},
                    {"firstChunk", offsetof(VisibilityViewParams, firstChunk), "uint"},
                    {"chunkCount", offsetof(VisibilityViewParams, chunkCount), "uint"},
                    {"padding", offsetof(VisibilityViewParams, padding), "uint"}}});
    schema.registerUniformStruct(
        {.name = "VisibilityParams",
         .slot = 6,
         .sizeBytes = sizeof(VisibilityParams),
         .fields = {{"layout", offsetof(VisibilityParams, layout), "uint"},
                    {"rowCapacity", offsetof(VisibilityParams, rowCapacity), "uint"},
                    {"argumentCapacity", offsetof(VisibilityParams, argumentCapacity), "uint"},
                    {"stateCapacity", offsetof(VisibilityParams, stateCapacity), "uint"},
                    {"candidateCount", offsetof(VisibilityParams, candidateCount), "uint"},
                    {"viewCount", offsetof(VisibilityParams, viewCount), "uint"},
                    {"padding", offsetof(VisibilityParams, padding), "uint2"}}});
    schema.registerUniformStruct({.name = "OcclusionParams",
                                  .slot = 13,
                                  .sizeBytes = sizeof(OcclusionParams),
                                  .fields = {{"sourceRows", 0, "float4[4]"},
                                             {"sourceWidth", 64, "uint"},
                                             {"sourceHeight", 68, "uint"},
                                             {"levelCount", 72, "uint"},
                                             {"flags", 76, "uint"},
                                             {"nearGuard", 80, "float"},
                                             {"depthGuard", 84, "float"},
                                             {"padding", 88, "uint2"}}});
    // GPU counters use four bypass words; the decoded status has an extra None array entry.
    schema.registerUniformStruct({.name = "VisibilityCounterWords",
                                  .slot = 8,
                                  .sizeBytes = 80,
                                  .fields = {{"candidates", 0, "uint"},
                                             {"visible", 4, "uint"},
                                             {"rejected", 8, "uint"},
                                             {"disabled", 12, "uint"},
                                             {"unculled", 16, "uint"},
                                             {"unreliable", 20, "uint"},
                                             {"nonfinite", 24, "uint"},
                                             {"emittedRows", 28, "uint"},
                                             {"emittedCommands", 32, "uint"},
                                             {"overflowedRows", 36, "uint"},
                                             {"overflowedCommands", 40, "uint"},
                                             {"occluded", 44, "uint"},
                                             {"tested", 48, "uint"},
                                             {"historyInvalid", 52, "uint"},
                                             {"nearCrossing", 56, "uint"},
                                             {"outsideSource", 60, "uint"},
                                             {"rectTooLarge", 64, "uint"},
                                             {"padding", 68, "uint3"}}});
}
} // namespace lmx::render
