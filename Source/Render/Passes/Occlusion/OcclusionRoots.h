//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionRoots.h
/// @brief Declares occlusion address roots and their canonical frame-owned values.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Occlusion/HzbFrameData.h"
#include "Render/Passes/Occlusion/Occlusion.h"

#include <rojoRHI/RHI.h>

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lmx::render {

/// Borrows reduction constants and source and destination views.
struct alignas(16) HzbReduceRoot {
    rojoRHI::Addr paramsAddr; ///< Borrowed params data.
    uint32_t source = 0;      ///< Sampled source mip view.
    uint32_t destination = 0; ///< Storage destination mip view.
};

static_assert(alignof(HzbReduceRoot) == 16);
static_assert(sizeof(HzbReduceRoot) == 16);
static_assert(offsetof(HzbReduceRoot, paramsAddr) == 0);
static_assert(offsetof(HzbReduceRoot, source) == 8);
static_assert(offsetof(HzbReduceRoot, destination) == 12);

/// Borrows publication storage and the completed pyramid view.
struct alignas(16) HzbPublishRoot {
    rojoRHI::Addr publicationAddr; ///< Borrowed publication data.
    uint32_t pyramid = 0;          ///< Sampled pyramid view.
    uint32_t levelCount = 0;       ///< Published pyramid mip count.
};

static_assert(alignof(HzbPublishRoot) == 16);
static_assert(sizeof(HzbPublishRoot) == 16);
static_assert(offsetof(HzbPublishRoot, publicationAddr) == 0);
static_assert(offsetof(HzbPublishRoot, pyramid) == 8);
static_assert(offsetof(HzbPublishRoot, levelCount) == 12);

/// Borrows debug-view constants and the pyramid view.
struct alignas(16) HzbDebugRoot {
    rojoRHI::Addr paramsAddr; ///< Borrowed params data.
    uint32_t pyramid = 0;     ///< Sampled pyramid view.
    uint32_t padding = 0;     ///< Explicit zero padding.
};

static_assert(alignof(HzbDebugRoot) == 16);
static_assert(sizeof(HzbDebugRoot) == 16);
static_assert(offsetof(HzbDebugRoot, paramsAddr) == 0);
static_assert(offsetof(HzbDebugRoot, pyramid) == 8);
static_assert(offsetof(HzbDebugRoot, padding) == 12);

/// Borrows scene tables, reference constants and alpha-mask sampling data.
struct alignas(16) OcclusionReferenceRoot {
    rojoRHI::Addr verticesAddr;  ///< Borrowed vertices data.
    rojoRHI::Addr instancesAddr; ///< Borrowed instances data.
    rojoRHI::Addr materialsAddr; ///< Borrowed materials data.
    rojoRHI::Addr paramsAddr;    ///< Borrowed params data.
    uint32_t diffuse = 0;        ///< Sampled material diffuse view.
    uint32_t sampler = 0;        ///< Material sampler.
    uint32_t padding0 = 0;       ///< Explicit zero padding.
    uint32_t padding1 = 0;       ///< Explicit zero padding.
};

static_assert(alignof(OcclusionReferenceRoot) == 16);
static_assert(sizeof(OcclusionReferenceRoot) == 48);
static_assert(offsetof(OcclusionReferenceRoot, verticesAddr) == 0);
static_assert(offsetof(OcclusionReferenceRoot, instancesAddr) == 8);
static_assert(offsetof(OcclusionReferenceRoot, materialsAddr) == 16);
static_assert(offsetof(OcclusionReferenceRoot, paramsAddr) == 24);
static_assert(offsetof(OcclusionReferenceRoot, diffuse) == 32);
static_assert(offsetof(OcclusionReferenceRoot, sampler) == 36);
static_assert(offsetof(OcclusionReferenceRoot, padding0) == 40);
static_assert(offsetof(OcclusionReferenceRoot, padding1) == 44);

/// The debug view payload, preserving the builder's eight-word order.
struct HzbDebugViewParams {
    uint32_t level;        ///< Clamped pyramid mip.
    uint32_t activeWidth;  ///< Active source width, pixels.
    uint32_t activeHeight; ///< Active source height, pixels.
    uint32_t padding;      ///< Explicit zero padding.
    uint32_t outputWidth;  ///< Output width, pixels.
    uint32_t outputHeight; ///< Output height, pixels.
    uint32_t padding2;     ///< Explicit zero padding.
    uint32_t padding3;     ///< Explicit zero padding.
};
static_assert(sizeof(HzbDebugViewParams) == 32);
static_assert(std::is_trivially_copyable_v<HzbDebugViewParams>);
static_assert(offsetof(HzbDebugViewParams, level) == 0);
static_assert(offsetof(HzbDebugViewParams, activeWidth) == 4);
static_assert(offsetof(HzbDebugViewParams, activeHeight) == 8);
static_assert(offsetof(HzbDebugViewParams, padding) == 12);
static_assert(offsetof(HzbDebugViewParams, outputWidth) == 16);
static_assert(offsetof(HzbDebugViewParams, outputHeight) == 20);
static_assert(offsetof(HzbDebugViewParams, padding2) == 24);
static_assert(offsetof(HzbDebugViewParams, padding3) == 28);

/// Frame-owned transform and stable instance row for the independent reference draw.
struct ReferenceParams {
    glm::mat4 viewProjection; ///< Source view-projection transform.
    uint32_t instanceRow;     ///< Stable scene instance row.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
    uint32_t padding1 = 0;    ///< Explicit zero padding.
    uint32_t padding2 = 0;    ///< Explicit zero padding.
};
static_assert(sizeof(ReferenceParams) == 80);
static_assert(offsetof(ReferenceParams, instanceRow) == 64);

} // namespace lmx::render
