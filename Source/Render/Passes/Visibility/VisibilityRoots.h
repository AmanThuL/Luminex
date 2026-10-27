//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityRoots.h
/// @brief Declares address roots and imports the canonical visibility value types.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Visibility/VisibilityTables.h"

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Borrows classification inputs and writable state, counters and chunk counts.
struct alignas(16) VisibilityClassifyRoot {
    rojoRHI::Addr candidatesAddr; ///< Borrowed candidates data.
    rojoRHI::Addr chunksAddr;     ///< Borrowed chunks data.
    rojoRHI::Addr instancesAddr;  ///< Borrowed instances data.
    rojoRHI::Addr viewsAddr;      ///< Borrowed views data.
    rojoRHI::Addr paramsAddr;     ///< Borrowed params data.
    rojoRHI::Addr statesAddr;     ///< Borrowed states data.
    rojoRHI::Addr countersAddr;   ///< Borrowed counters data.
    rojoRHI::Addr countsAddr;     ///< Borrowed counts data.
};

static_assert(alignof(VisibilityClassifyRoot) == 16);
static_assert(sizeof(VisibilityClassifyRoot) == 64);
static_assert(offsetof(VisibilityClassifyRoot, candidatesAddr) == 0);
static_assert(offsetof(VisibilityClassifyRoot, chunksAddr) == 8);
static_assert(offsetof(VisibilityClassifyRoot, instancesAddr) == 16);
static_assert(offsetof(VisibilityClassifyRoot, viewsAddr) == 24);
static_assert(offsetof(VisibilityClassifyRoot, paramsAddr) == 32);
static_assert(offsetof(VisibilityClassifyRoot, statesAddr) == 40);
static_assert(offsetof(VisibilityClassifyRoot, countersAddr) == 48);
static_assert(offsetof(VisibilityClassifyRoot, countsAddr) == 56);

/// Borrows classification data and the previous depth pyramid.
struct alignas(16) VisibilityOcclusionRoot {
    rojoRHI::Addr candidatesAddr; ///< Borrowed candidates data.
    rojoRHI::Addr chunksAddr;     ///< Borrowed chunks data.
    rojoRHI::Addr instancesAddr;  ///< Borrowed instances data.
    rojoRHI::Addr viewsAddr;      ///< Borrowed views data.
    rojoRHI::Addr paramsAddr;     ///< Borrowed params data.
    rojoRHI::Addr statesAddr;     ///< Borrowed states data.
    rojoRHI::Addr countersAddr;   ///< Borrowed counters data.
    rojoRHI::Addr countsAddr;     ///< Borrowed counts data.
    rojoRHI::Addr occlusionAddr;  ///< Borrowed occlusion data.
    uint32_t pyramid = 0;         ///< Sampled depth pyramid view.
    uint32_t padding = 0;         ///< Explicit zero padding.
};

static_assert(alignof(VisibilityOcclusionRoot) == 16);
static_assert(sizeof(VisibilityOcclusionRoot) == 80);
static_assert(offsetof(VisibilityOcclusionRoot, candidatesAddr) == 0);
static_assert(offsetof(VisibilityOcclusionRoot, chunksAddr) == 8);
static_assert(offsetof(VisibilityOcclusionRoot, instancesAddr) == 16);
static_assert(offsetof(VisibilityOcclusionRoot, viewsAddr) == 24);
static_assert(offsetof(VisibilityOcclusionRoot, paramsAddr) == 32);
static_assert(offsetof(VisibilityOcclusionRoot, statesAddr) == 40);
static_assert(offsetof(VisibilityOcclusionRoot, countersAddr) == 48);
static_assert(offsetof(VisibilityOcclusionRoot, countsAddr) == 56);
static_assert(offsetof(VisibilityOcclusionRoot, occlusionAddr) == 64);
static_assert(offsetof(VisibilityOcclusionRoot, pyramid) == 72);
static_assert(offsetof(VisibilityOcclusionRoot, padding) == 76);

/// Borrows canonical ranges and per-chunk scan counts and offsets.
struct alignas(16) VisibilityScanRoot {
    rojoRHI::Addr runsAddr;    ///< Borrowed runs data.
    rojoRHI::Addr chunksAddr;  ///< Borrowed chunks data.
    rojoRHI::Addr viewsAddr;   ///< Borrowed views data.
    rojoRHI::Addr countsAddr;  ///< Borrowed counts data.
    rojoRHI::Addr offsetsAddr; ///< Borrowed offsets data.
    uint32_t padding0 = 0;     ///< Explicit zero padding.
    uint32_t padding1 = 0;     ///< Explicit zero padding.
};

static_assert(alignof(VisibilityScanRoot) == 16);
static_assert(sizeof(VisibilityScanRoot) == 48);
static_assert(offsetof(VisibilityScanRoot, runsAddr) == 0);
static_assert(offsetof(VisibilityScanRoot, chunksAddr) == 8);
static_assert(offsetof(VisibilityScanRoot, viewsAddr) == 16);
static_assert(offsetof(VisibilityScanRoot, countsAddr) == 24);
static_assert(offsetof(VisibilityScanRoot, offsetsAddr) == 32);
static_assert(offsetof(VisibilityScanRoot, padding0) == 40);
static_assert(offsetof(VisibilityScanRoot, padding1) == 44);

/// Borrows canonical tables and writable row and indexed-argument storage.
struct alignas(16) VisibilityEmitRoot {
    rojoRHI::Addr candidatesAddr; ///< Borrowed candidates data.
    rojoRHI::Addr runsAddr;       ///< Borrowed runs data.
    rojoRHI::Addr chunksAddr;     ///< Borrowed chunks data.
    rojoRHI::Addr instancesAddr;  ///< Borrowed instances data.
    rojoRHI::Addr meshesAddr;     ///< Borrowed meshes data.
    rojoRHI::Addr viewsAddr;      ///< Borrowed views data.
    rojoRHI::Addr paramsAddr;     ///< Borrowed params data.
    rojoRHI::Addr statesAddr;     ///< Borrowed states data.
    rojoRHI::Addr countersAddr;   ///< Borrowed counters data.
    rojoRHI::Addr offsetsAddr;    ///< Borrowed offsets data.
    rojoRHI::Addr rowsAddr;       ///< Borrowed rows data.
    rojoRHI::Addr argumentsAddr;  ///< Borrowed arguments data.
};

static_assert(alignof(VisibilityEmitRoot) == 16);
static_assert(sizeof(VisibilityEmitRoot) == 96);
static_assert(offsetof(VisibilityEmitRoot, candidatesAddr) == 0);
static_assert(offsetof(VisibilityEmitRoot, runsAddr) == 8);
static_assert(offsetof(VisibilityEmitRoot, chunksAddr) == 16);
static_assert(offsetof(VisibilityEmitRoot, instancesAddr) == 24);
static_assert(offsetof(VisibilityEmitRoot, meshesAddr) == 32);
static_assert(offsetof(VisibilityEmitRoot, viewsAddr) == 40);
static_assert(offsetof(VisibilityEmitRoot, paramsAddr) == 48);
static_assert(offsetof(VisibilityEmitRoot, statesAddr) == 56);
static_assert(offsetof(VisibilityEmitRoot, countersAddr) == 64);
static_assert(offsetof(VisibilityEmitRoot, offsetsAddr) == 72);
static_assert(offsetof(VisibilityEmitRoot, rowsAddr) == 80);
static_assert(offsetof(VisibilityEmitRoot, argumentsAddr) == 88);

} // namespace lmx::render
