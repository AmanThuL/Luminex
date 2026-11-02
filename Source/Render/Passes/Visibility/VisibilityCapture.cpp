//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityCapture.cpp
/// @brief Registers GPU visibility record layouts for labelled capture inspection.
//----------------------------------------------------------------------------------------------------------------------
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Passes/Visibility/VisibilityRoots.h"
#include "Render/Passes/Visibility/VisibilityTables.h"

namespace lmx::render {
//======================================================================================================================
void registerVisibilityLayoutsForCapture() {
    engine::registerUniformStruct(
        {.name = "CandidateRecord",

         .sizeBytes = sizeof(CandidateRecord),
         .fields = {{"instanceRow", offsetof(CandidateRecord, instanceRow), "uint"},
                    {"run", offsetof(CandidateRecord, run), "uint"}}});
    engine::registerUniformStruct(
        {.name = "RunRecord",

         .sizeBytes = sizeof(RunRecord),
         .fields = {{"firstCandidate", offsetof(RunRecord, firstCandidate), "uint"},
                    {"candidateCount", offsetof(RunRecord, candidateCount), "uint"},
                    {"firstSlot", offsetof(RunRecord, firstSlot), "uint"},
                    {"argumentIndex", offsetof(RunRecord, argumentIndex), "uint"}}});
    engine::registerUniformStruct(
        {.name = "ChunkRecord",

         .sizeBytes = sizeof(ChunkRecord),
         .fields = {{"run", offsetof(ChunkRecord, run), "uint"},
                    {"firstCandidate", offsetof(ChunkRecord, firstCandidate), "uint"},
                    {"candidateCount", offsetof(ChunkRecord, candidateCount), "uint"},
                    {"padding", offsetof(ChunkRecord, padding), "uint"}}});
    engine::registerUniformStruct(
        {.name = "VisibilityViewParams",

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
    engine::registerUniformStruct(
        {.name = "VisibilityParams",

         .sizeBytes = sizeof(VisibilityParams),
         .fields = {{"layout", offsetof(VisibilityParams, layout), "uint"},
                    {"rowCapacity", offsetof(VisibilityParams, rowCapacity), "uint"},
                    {"argumentCapacity", offsetof(VisibilityParams, argumentCapacity), "uint"},
                    {"stateCapacity", offsetof(VisibilityParams, stateCapacity), "uint"},
                    {"candidateCount", offsetof(VisibilityParams, candidateCount), "uint"},
                    {"viewCount", offsetof(VisibilityParams, viewCount), "uint"},
                    {"padding", offsetof(VisibilityParams, padding), "uint2"}}});
    engine::registerUniformStruct({.name = "OcclusionParams",

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
    engine::registerUniformStruct({.name = "VisibilityCounterWords",

                                   .sizeBytes = kVisibilityCounterWords * sizeof(uint32_t),
                                   .fields = {{"candidates", 0, "uint"},
                                              {"visible", 4, "uint"},
                                              {"rejected", 8, "uint"},
                                              {"cullingOff", 12, "uint"},
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
                                              {"disabled", 68, "uint"},
                                              {"padding", 72, "uint3"}}});
    engine::registerUniformStruct(
        {.name = "VisibilityClassifyRoot",
         .sizeBytes = sizeof(VisibilityClassifyRoot),
         .fields = {{"candidatesAddr", offsetof(VisibilityClassifyRoot, candidatesAddr), "Addr"},
                    {"chunksAddr", offsetof(VisibilityClassifyRoot, chunksAddr), "Addr"},
                    {"instancesAddr", offsetof(VisibilityClassifyRoot, instancesAddr), "Addr"},
                    {"viewsAddr", offsetof(VisibilityClassifyRoot, viewsAddr), "Addr"},
                    {"paramsAddr", offsetof(VisibilityClassifyRoot, paramsAddr), "Addr"},
                    {"statesAddr", offsetof(VisibilityClassifyRoot, statesAddr), "Addr"},
                    {"countersAddr", offsetof(VisibilityClassifyRoot, countersAddr), "Addr"},
                    {"countsAddr", offsetof(VisibilityClassifyRoot, countsAddr), "Addr"}}});
    engine::registerUniformStruct(
        {.name = "VisibilityOcclusionRoot",
         .sizeBytes = sizeof(VisibilityOcclusionRoot),
         .fields = {{"candidatesAddr", offsetof(VisibilityOcclusionRoot, candidatesAddr), "Addr"},
                    {"chunksAddr", offsetof(VisibilityOcclusionRoot, chunksAddr), "Addr"},
                    {"instancesAddr", offsetof(VisibilityOcclusionRoot, instancesAddr), "Addr"},
                    {"viewsAddr", offsetof(VisibilityOcclusionRoot, viewsAddr), "Addr"},
                    {"paramsAddr", offsetof(VisibilityOcclusionRoot, paramsAddr), "Addr"},
                    {"statesAddr", offsetof(VisibilityOcclusionRoot, statesAddr), "Addr"},
                    {"countersAddr", offsetof(VisibilityOcclusionRoot, countersAddr), "Addr"},
                    {"countsAddr", offsetof(VisibilityOcclusionRoot, countsAddr), "Addr"},
                    {"occlusionAddr", offsetof(VisibilityOcclusionRoot, occlusionAddr), "Addr"},
                    {"pyramid", offsetof(VisibilityOcclusionRoot, pyramid), "uint"},
                    {"padding", offsetof(VisibilityOcclusionRoot, padding), "uint"}}});
    engine::registerUniformStruct(
        {.name = "VisibilityScanRoot",
         .sizeBytes = sizeof(VisibilityScanRoot),
         .fields = {{"runsAddr", offsetof(VisibilityScanRoot, runsAddr), "Addr"},
                    {"chunksAddr", offsetof(VisibilityScanRoot, chunksAddr), "Addr"},
                    {"viewsAddr", offsetof(VisibilityScanRoot, viewsAddr), "Addr"},
                    {"countsAddr", offsetof(VisibilityScanRoot, countsAddr), "Addr"},
                    {"offsetsAddr", offsetof(VisibilityScanRoot, offsetsAddr), "Addr"},
                    {"padding0", offsetof(VisibilityScanRoot, padding0), "uint"},
                    {"padding1", offsetof(VisibilityScanRoot, padding1), "uint"}}});
    engine::registerUniformStruct(
        {.name = "VisibilityEmitRoot",
         .sizeBytes = sizeof(VisibilityEmitRoot),
         .fields = {{"candidatesAddr", offsetof(VisibilityEmitRoot, candidatesAddr), "Addr"},
                    {"runsAddr", offsetof(VisibilityEmitRoot, runsAddr), "Addr"},
                    {"chunksAddr", offsetof(VisibilityEmitRoot, chunksAddr), "Addr"},
                    {"instancesAddr", offsetof(VisibilityEmitRoot, instancesAddr), "Addr"},
                    {"meshesAddr", offsetof(VisibilityEmitRoot, meshesAddr), "Addr"},
                    {"viewsAddr", offsetof(VisibilityEmitRoot, viewsAddr), "Addr"},
                    {"paramsAddr", offsetof(VisibilityEmitRoot, paramsAddr), "Addr"},
                    {"statesAddr", offsetof(VisibilityEmitRoot, statesAddr), "Addr"},
                    {"countersAddr", offsetof(VisibilityEmitRoot, countersAddr), "Addr"},
                    {"offsetsAddr", offsetof(VisibilityEmitRoot, offsetsAddr), "Addr"},
                    {"rowsAddr", offsetof(VisibilityEmitRoot, rowsAddr), "Addr"},
                    {"argumentsAddr", offsetof(VisibilityEmitRoot, argumentsAddr), "Addr"}}});
}
} // namespace lmx::render
