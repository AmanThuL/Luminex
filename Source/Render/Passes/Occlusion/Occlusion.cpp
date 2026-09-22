//----------------------------------------------------------------------------------------------------------------------
/// @file Occlusion.cpp
/// @brief Mirrors the ordered shader box test without floating-point contraction.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/Occlusion.h"
#include "Core/Diagnostics/Assert.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace lmx::render {
//======================================================================================================================
OcclusionParams makeOcclusionParams(const glm::mat4& matrix, uint32_t width, uint32_t height,
                                    uint32_t levels, bool enabled, bool valid) {
    OcclusionParams result{.sourceWidth = width,
                           .sourceHeight = height,
                           .levelCount = levels,
                           .flags = (enabled ? 1u : 0u) | (valid ? 2u : 0u)};
    for (uint32_t r = 0; r < 4; ++r)
        for (uint32_t c = 0; c < 4; ++c)
            result.sourceRows[r][c] = matrix[c][r];
    return result;
}
//======================================================================================================================
OcclusionProjection projectOcclusionBounds(const Aabb& bounds, const OcclusionParams& params) {
    OcclusionProjection result;
    if (!(params.flags & 1u))
        return result;
    if (!(params.flags & 2u)) {
        result.outcome = OcclusionOutcome::HistoryInvalid;
        return result;
    }
    float minX = std::numeric_limits<float>::max(), minY = minX;
    float maxX = -minX, maxY = -minX;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{corner & 1u ? bounds.maximum.x : bounds.minimum.x,
                              corner & 2u ? bounds.maximum.y : bounds.minimum.y,
                              corner & 4u ? bounds.maximum.z : bounds.minimum.z};
        glm::vec4 clip;
        for (uint32_t r = 0; r < 4; ++r) {
            const auto row = params.sourceRows[r];
            const float x = row.x * point.x, y = row.y * point.y, z = row.z * point.z;
            const float xy = x + y, xyz = xy + z;
            clip[r] = xyz + row.w;
        }
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) ||
            !std::isfinite(clip.w) || clip.w <= params.nearGuard || clip.z > clip.w) {
            result.outcome = OcclusionOutcome::NearCrossing;
            return result;
        }
        const float nx = clip.x / clip.w, ny = clip.y / clip.w, nz = clip.z / clip.w;
        const float hx = nx * 0.5f, hy = ny * -0.5f;
        const float ux = hx + 0.5f, uy = hy + 0.5f;
        const float x = ux * float(params.sourceWidth), y = uy * float(params.sourceHeight);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(nz)) {
            result.outcome = OcclusionOutcome::NearCrossing;
            return result;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        result.zBox = std::max(result.zBox, nz);
    }
    const float left = std::floor(minX) - 1.0f, top = std::floor(minY) - 1.0f;
    const float right = std::ceil(maxX) + 1.0f, bottom = std::ceil(maxY) + 1.0f;
    if (left < 0 || top < 0 || right > float(params.sourceWidth) ||
        bottom > float(params.sourceHeight)) {
        result.outcome = OcclusionOutcome::OutsideSource;
        return result;
    }
    result.rectangle = {int32_t(left), int32_t(top), int32_t(right), int32_t(bottom)};
    for (uint32_t level = 0; level < params.levelCount; ++level) {
        const uint32_t shift = level + 1;
        if (((result.rectangle[2] - 1) >> shift) - (result.rectangle[0] >> shift) <= 1 &&
            ((result.rectangle[3] - 1) >> shift) - (result.rectangle[1] >> shift) <= 1) {
            result.level = level;
            result.outcome = OcclusionOutcome::Retained;
            return result;
        }
    }
    result.outcome = OcclusionOutcome::RectTooLarge;
    return result;
}
//======================================================================================================================
OcclusionProjection testOcclusionBounds(const Aabb& bounds, const OcclusionParams& params,
                                        std::span<const OcclusionLevel> levels) {
    auto result = projectOcclusionBounds(bounds, params);
    if (result.outcome != OcclusionOutcome::Retained)
        return result;
    LMX_ASSERT(result.level < levels.size(), "occlusion CPU oracle needs every selected mip");
    const auto& mip = levels[result.level];
    const uint32_t shift = result.level + 1;
    float farthest = 1.0f;
    for (int32_t y = result.rectangle[1] >> shift; y <= ((result.rectangle[3] - 1) >> shift); ++y)
        for (int32_t x = result.rectangle[0] >> shift; x <= ((result.rectangle[2] - 1) >> shift);
             ++x) {
            const auto index = uint64_t(y) * mip.width + uint32_t(x);
            LMX_ASSERT(index < mip.depth.size(), "occlusion CPU mip read must be in range");
            farthest = std::min(farthest, mip.depth[index]);
        }
    const float guarded = result.zBox + params.depthGuard;
    result.occluded = guarded < farthest;
    return result;
}
//======================================================================================================================
const char* occlusionOutcomeName(OcclusionOutcome outcome) {
    switch (outcome) {
    case OcclusionOutcome::NotTested:
        return "Not tested";
    case OcclusionOutcome::Retained:
        return "Retained";
    case OcclusionOutcome::HistoryInvalid:
        return "History invalid";
    case OcclusionOutcome::NearCrossing:
        return "Near crossing";
    case OcclusionOutcome::OutsideSource:
        return "Outside source";
    case OcclusionOutcome::RectTooLarge:
        return "Rectangle too large";
    }
    return "Unknown";
}
} // namespace lmx::render
