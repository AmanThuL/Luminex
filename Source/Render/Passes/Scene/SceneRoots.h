//----------------------------------------------------------------------------------------------------------------------
/// @file SceneRoots.h
/// @brief Declares flat address-first scene, draw and sky shader roots.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Shared tables, payload addresses and sampled views for one scene pass.
struct alignas(16) ScenePassRoot {
    rojoRHI::Addr vertices{};         ///< Vertex-pulling table.
    rojoRHI::Addr visibleRows{};      ///< Paced visible instance-row indices.
    rojoRHI::Addr instances{};        ///< Instance-row table.
    rojoRHI::Addr materials{};        ///< Material-row table.
    rojoRHI::Addr passUniforms{};     ///< Pass constants uploaded once.
    rojoRHI::Addr exposure{};         ///< Automatic pre-exposure payload, when selected.
    rojoRHI::Addr lights{};           ///< Local-light rows, zero when disabled.
    rojoRHI::Addr lightGrid{};        ///< Cluster grid, zero when disabled.
    rojoRHI::Addr lightIndices{};     ///< Cluster light indices, zero when disabled.
    rojoRHI::Addr localLightParams{}; ///< Local-light constants, zero when disabled.
    uint32_t shadow{};                ///< Sampled shadow depth view.
    uint32_t irradiance{};            ///< Diffuse IBL cube view.
    uint32_t prefilteredEnv{};        ///< Specular IBL cube view.
    uint32_t dfg{};                   ///< Split-sum DFG view.
    uint32_t linearSampler{};         ///< Material sampler index.
    uint32_t shadowSampler{};         ///< Shadow comparison sampler index.
    uint32_t iblSampler{};            ///< IBL sampler index.
    uint32_t padding{};               ///< Explicit zero padding.
};

static_assert(alignof(ScenePassRoot) == 16);
static_assert(sizeof(ScenePassRoot) == 112);
static_assert(offsetof(ScenePassRoot, vertices) == 0);
static_assert(offsetof(ScenePassRoot, visibleRows) == 8);
static_assert(offsetof(ScenePassRoot, instances) == 16);
static_assert(offsetof(ScenePassRoot, materials) == 24);
static_assert(offsetof(ScenePassRoot, passUniforms) == 32);
static_assert(offsetof(ScenePassRoot, exposure) == 40);
static_assert(offsetof(ScenePassRoot, lights) == 48);
static_assert(offsetof(ScenePassRoot, lightGrid) == 56);
static_assert(offsetof(ScenePassRoot, lightIndices) == 64);
static_assert(offsetof(ScenePassRoot, localLightParams) == 72);
static_assert(offsetof(ScenePassRoot, shadow) == 80);
static_assert(offsetof(ScenePassRoot, irradiance) == 84);
static_assert(offsetof(ScenePassRoot, prefilteredEnv) == 88);
static_assert(offsetof(ScenePassRoot, dfg) == 92);
static_assert(offsetof(ScenePassRoot, linearSampler) == 96);
static_assert(offsetof(ScenePassRoot, shadowSampler) == 100);
static_assert(offsetof(ScenePassRoot, iblSampler) == 104);
static_assert(offsetof(ScenePassRoot, padding) == 108);

/// Per-run material views and explicit direct or indirect row identity.
struct alignas(16) SceneDrawRoot {
    rojoRHI::Addr pass{};  ///< Address of the shared ScenePassRoot.
    uint32_t firstEntry{}; ///< Direct visible-row offset; unused for indirect draws.
    uint32_t baseColor{};  ///< Base-color view including fallback.
    uint32_t normal{};     ///< Normal-map view including fallback.
    uint32_t metalRough{}; ///< Metallic/roughness view including fallback.
    uint32_t occlusion{};  ///< Occlusion view including fallback.
    uint32_t emissive{};   ///< Emissive view including fallback.
    uint32_t indirect{};   ///< One selects drawId, zero selects firstEntry; never add them.
    uint32_t padding0{};   ///< Explicit zero padding.
    uint32_t padding1{};   ///< Explicit zero padding.
    uint32_t padding2{};   ///< Explicit zero padding.
};

static_assert(alignof(SceneDrawRoot) == 16);
static_assert(sizeof(SceneDrawRoot) == 48);
static_assert(offsetof(SceneDrawRoot, pass) == 0);
static_assert(offsetof(SceneDrawRoot, firstEntry) == 8);
static_assert(offsetof(SceneDrawRoot, baseColor) == 12);
static_assert(offsetof(SceneDrawRoot, normal) == 16);
static_assert(offsetof(SceneDrawRoot, metalRough) == 20);
static_assert(offsetof(SceneDrawRoot, occlusion) == 24);
static_assert(offsetof(SceneDrawRoot, emissive) == 28);
static_assert(offsetof(SceneDrawRoot, indirect) == 32);
static_assert(offsetof(SceneDrawRoot, padding0) == 36);
static_assert(offsetof(SceneDrawRoot, padding1) == 40);
static_assert(offsetof(SceneDrawRoot, padding2) == 44);

/// Sky geometry, constants and environment sampling indices.
struct alignas(16) SkyRoot {
    rojoRHI::Addr vertices{}; ///< Sky vertex-pulling table.
    rojoRHI::Addr uniforms{}; ///< Sky constants uploaded once.
    rojoRHI::Addr exposure{}; ///< Automatic pre-exposure payload, when selected.
    uint32_t sky{};           ///< Sampled environment cube view.
    uint32_t sampler{};       ///< Environment sampler index.
};

static_assert(alignof(SkyRoot) == 16);
static_assert(sizeof(SkyRoot) == 32);
static_assert(offsetof(SkyRoot, vertices) == 0);
static_assert(offsetof(SkyRoot, uniforms) == 8);
static_assert(offsetof(SkyRoot, exposure) == 16);
static_assert(offsetof(SkyRoot, sky) == 24);
static_assert(offsetof(SkyRoot, sampler) == 28);

} // namespace lmx::render
