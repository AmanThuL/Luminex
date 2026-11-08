//----------------------------------------------------------------------------------------------------------------------
/// @file ShadowRoots.h
/// @brief Declares address-first shadow roots and their pointed light transform.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Natural-layout light transform copied once for a shadow pass.
struct ShadowPassUniforms {
    glm::mat4 lightViewProj; ///< World-to-light clip transform with reversed depth.
};
static_assert(sizeof(ShadowPassUniforms) == 64);
static_assert(offsetof(ShadowPassUniforms, lightViewProj) == 0);

/// Shared scene addresses and material sampling index for one shadow pass.
struct alignas(16) ShadowPassRoot {
    rojoRHI::Addr vertices{};    ///< Packed scene vertex pool.
    rojoRHI::Addr visibleRows{}; ///< Paced visible instance-row indices.
    rojoRHI::Addr instances{};   ///< Scene instance-row table.
    rojoRHI::Addr materials{};   ///< Scene material-row table; read only by masked draws.
    rojoRHI::Addr uniforms{};    ///< Natural-layout ShadowPassUniforms payload.
    uint32_t linearSampler{};    ///< Material sampler index; zero is valid.
    uint32_t padding{};          ///< Explicit zero padding.
};
static_assert(alignof(ShadowPassRoot) == 16);
static_assert(sizeof(ShadowPassRoot) == 48);
static_assert(offsetof(ShadowPassRoot, vertices) == 0);
static_assert(offsetof(ShadowPassRoot, visibleRows) == 8);
static_assert(offsetof(ShadowPassRoot, instances) == 16);
static_assert(offsetof(ShadowPassRoot, materials) == 24);
static_assert(offsetof(ShadowPassRoot, uniforms) == 32);
static_assert(offsetof(ShadowPassRoot, linearSampler) == 40);
static_assert(offsetof(ShadowPassRoot, padding) == 44);

/// Draw-row identity: pushed once per pass for indirect and batched draws, which read drawId, and
/// once per run for direct draws, which read firstEntry. Masked draws read their diffuse view from
/// MaterialRow.
struct alignas(16) ShadowDrawRoot {
    rojoRHI::Addr pass{};  ///< Address of this pass's ShadowPassRoot.
    uint32_t firstEntry{}; ///< Direct visible-row offset, unused for indirect draws.
    uint32_t indirect{};   ///< One selects drawId, zero selects firstEntry; never add them.
};
static_assert(alignof(ShadowDrawRoot) == 16);
static_assert(sizeof(ShadowDrawRoot) == 16);
static_assert(offsetof(ShadowDrawRoot, pass) == 0);
static_assert(offsetof(ShadowDrawRoot, firstEntry) == 8);
static_assert(offsetof(ShadowDrawRoot, indirect) == 12);

} // namespace lmx::render
