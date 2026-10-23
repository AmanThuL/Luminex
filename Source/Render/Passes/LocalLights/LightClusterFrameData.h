//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterFrameData.h
/// @brief Declares light clustering and debug frame data with pure builders.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Lights/LocalLight.h"
#include "Render/Passes/LocalLights/LightClusters.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Mirrors Shaders/Passes/LocalLights/LightCluster.slang's LightClusterParams. Matrices travel as
/// their four glm columns because a Slang float4x4 in a cbuffer leaves the column/row convention to
/// codegen, while `column[c][r]` names exactly what `m[c][r]` names here; the slice table rides in
/// float4s because cbuffer packing pads a scalar array element to sixteen bytes.
struct LightClusterKernelParams {
    std::array<glm::vec4, 4> view{};              ///< World-to-view columns, metres.
    std::array<glm::vec4, 4> inverseProjection{}; ///< Inverse jittered projection columns.
    uint32_t rowCount = 0;                        ///< Addressable light rows, free slots included.
    uint32_t activeWidth = 0;                     ///< Active render rectangle width, pixels.
    uint32_t activeHeight = 0;                    ///< Active render rectangle height, pixels.
    uint32_t perClusterCap = 0;                   ///< Entries one froxel may keep.
    uint32_t globalCapacity = 0;                  ///< Entries the index list holds.
    uint32_t froxelCount = 0;                     ///< Always kClusterCount.
    uint32_t padding0 = 0;                        ///< Explicit ABI padding.
    uint32_t padding1 = 0;                        ///< Explicit ABI padding.
    std::array<glm::vec4, 7> sliceDepth{}; ///< Slice boundary depths, four to a float4, metres.
};
static_assert(sizeof(LightClusterKernelParams) == 272,
              "must match LightCluster.slang's LightClusterParams");

/// Builds the kernel parameters shared by the count, scan and fill passes: copies the matrices into
/// columns, the capacities as given, the froxel count as kClusterCount and the boundary depths four
/// to a float4 (boundary b lands in element b / 4, lane b % 4).
LightClusterKernelParams buildLightClusterKernelParams(const LightClusterParams& clusters);

/// Scalar storage matches LightDebugView.slang's constant block, including the scalar depth array.
struct LightDebugParams {
    glm::mat4 inverseViewProjection; ///< Inverse jittered raster transform.
    uint32_t mode;                   ///< engine::LightDebugView value.
    uint32_t rowCount;               ///< Addressable light rows.
    uint32_t activeWidth;            ///< Active source width, pixels.
    uint32_t activeHeight;           ///< Active source height, pixels.
    uint32_t outputWidth;            ///< Full display width, pixels.
    uint32_t outputHeight;           ///< Full display height, pixels.
    std::array<float, kClusterSliceBoundaryCount> sliceDepth; ///< Slice boundary depths, metres.
};
static_assert(offsetof(LightDebugParams, sliceDepth) == 88);
static_assert(sizeof(LightDebugParams) == 188);

/// Plain values the light debug parameters are built from.
struct LightDebugParamsInputs {
    /// Diagnostic view; written as its enumerator value.
    engine::LightDebugView mode = engine::LightDebugView::Off;
    LightClusterParams clusters; ///< Supplies the row count, active extent and slice table.
    glm::mat4 inverseViewProjection{1.0f}; ///< Inverse of the frame's jittered raster transform.
    uint32_t outputWidth = 0;              ///< Full display width, pixels.
    uint32_t outputHeight = 0;             ///< Full display height, pixels.
};

/// Builds the light debug parameters; every field is copied, the mode as its underlying value.
LightDebugParams buildLightDebugParams(const LightDebugParamsInputs& inputs);

} // namespace lmx::render
