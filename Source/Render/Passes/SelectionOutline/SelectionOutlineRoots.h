//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutlineRoots.h
/// @brief Declares address and view roots for selection coverage and presentation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Borrows scene tables, one coverage payload and the draw's material texture/sampler.
struct alignas(16) SelectionMaskRoot {
    rojoRHI::Addr verticesAddr;  ///< VertexStorage array shared with ordinary scene draws.
    rojoRHI::Addr instancesAddr; ///< Canonical InstanceRow array.
    rojoRHI::Addr materialsAddr; ///< Canonical MaterialRow array.
    rojoRHI::Addr paramsAddr;    ///< MaskParams of this draw.
    uint32_t diffuse = 0;        ///< Sampled material texture, including the white fallback.
    uint32_t sampler = 0;        ///< Material filtering sampler.
    uint32_t padding0 = 0;       ///< Explicit zero padding.
    uint32_t padding1 = 0;       ///< Explicit zero padding.
};
static_assert(sizeof(SelectionMaskRoot) == 48);
static_assert(offsetof(SelectionMaskRoot, verticesAddr) == 0);
static_assert(offsetof(SelectionMaskRoot, instancesAddr) == 8);
static_assert(offsetof(SelectionMaskRoot, materialsAddr) == 16);
static_assert(offsetof(SelectionMaskRoot, paramsAddr) == 24);
static_assert(offsetof(SelectionMaskRoot, diffuse) == 32);
static_assert(offsetof(SelectionMaskRoot, sampler) == 36);
static_assert(offsetof(SelectionMaskRoot, padding0) == 40);
static_assert(offsetof(SelectionMaskRoot, padding1) == 44);

/// Borrows the completed display and the three images defining visible silhouette edges.
struct alignas(16) SelectionOutlineRoot {
    rojoRHI::Addr paramsAddr;   ///< OutlineParams holding the target-pixel radius.
    uint32_t display = 0;       ///< Completed SDR sampled view.
    uint32_t mask = 0;          ///< Selected geometry coverage view.
    uint32_t selectedDepth = 0; ///< Unoccluded selected geometry depth view.
    uint32_t sceneDepth = 0;    ///< Full scene visibility depth view.
    uint32_t padding0 = 0;      ///< Explicit zero padding.
    uint32_t padding1 = 0;      ///< Explicit zero padding.
};
static_assert(sizeof(SelectionOutlineRoot) == 32);
static_assert(offsetof(SelectionOutlineRoot, paramsAddr) == 0);
static_assert(offsetof(SelectionOutlineRoot, display) == 8);
static_assert(offsetof(SelectionOutlineRoot, mask) == 12);
static_assert(offsetof(SelectionOutlineRoot, selectedDepth) == 16);
static_assert(offsetof(SelectionOutlineRoot, sceneDepth) == 20);
static_assert(offsetof(SelectionOutlineRoot, padding0) == 24);
static_assert(offsetof(SelectionOutlineRoot, padding1) == 28);

/// Refreshes the UI target from the ordinary display without an outline contribution.
struct alignas(16) SelectionCopyRoot {
    uint32_t display = 0;  ///< Completed SDR sampled view.
    uint32_t padding0 = 0; ///< Explicit zero padding.
    uint32_t padding1 = 0; ///< Explicit zero padding.
    uint32_t padding2 = 0; ///< Explicit zero padding.
};
static_assert(sizeof(SelectionCopyRoot) == 16);
static_assert(offsetof(SelectionCopyRoot, display) == 0);
static_assert(offsetof(SelectionCopyRoot, padding0) == 4);
static_assert(offsetof(SelectionCopyRoot, padding1) == 8);
static_assert(offsetof(SelectionCopyRoot, padding2) == 12);

} // namespace lmx::render
