//----------------------------------------------------------------------------------------------------------------------
/// @file ProdValues.h
/// @brief Records the resolved production values every *prod*-marked entry in the M5.1
///        representative graph mirrors (spec section 6), before measurement begins.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Workload/Types.h"

#include <cstdint>

/// Spec section 6 freezes a handful of resource properties by reference to a production
/// declaration rather than by a literal value ("entries marked *prod* resolve to the production
/// declaration they mirror and are recorded in the manifest before measurement"). This header is
/// that resolution: one named constant per *prod* reference, each commented with the exact
/// production file it was read from. `RepresentativeGraph.cpp` builds R5 and the material/IBL
/// resource descriptors from these constants rather than restating the numbers, so a future drift
/// between this file and the cited source is a one-place fix.
namespace lmx::noapi::workload::prod {

// Exposure buffer (R5): one float, read-write storage, persistent across frames. Resolved from
// the `device.createBuffer` call that allocates `Renderer::m_exposureBuffer`
// (Source/Render/Renderer.cpp).
inline constexpr uint64_t kExposureBufferSize = sizeof(float);
inline constexpr bool kExposureBufferStorageRead = true;
inline constexpr bool kExposureBufferStorageWrite = true;

// Material texture formats (t0/t1/t4/t5/t6): base colour and emissive are authored sRGB colour;
// normal, metallic-roughness, and occlusion are linear sampled data. Resolved from the
// `ensureUploaded`/`device.createTexture` calls in `loadGltfBackedScene`
// (Source/Engine/Scene.cpp).
inline constexpr Format kBaseColorFormat = Format::RGBA8Unorm_sRGB;         ///< t0
inline constexpr Format kNormalFormat = Format::RGBA8Unorm;                 ///< t1
inline constexpr Format kMetallicRoughnessFormat = Format::RGBA8Unorm;      ///< t4
inline constexpr Format kOcclusionFormat = Format::RGBA8Unorm;              ///< t5
inline constexpr Format kEmissiveFormat = Format::RGBA8Unorm_sRGB;          ///< t6

// Shared IBL set dimensions and formats (t7/t8/t9). Resolved from the `kIrradianceFaceSize`,
// `kSpecularBaseFaceSize`, `kSpecularMipCount`, `kDfgLutSize` constants and the `IblTextures`
// upload formats documented in Source/Engine/Ibl.h.
inline constexpr uint32_t kIrradianceFaceSize = 16;             ///< t7: cube face extent.
inline constexpr Format kIrradianceFormat = Format::RGBA16Float; ///< t7
inline constexpr uint32_t kPrefilteredBaseFaceSize = 64;        ///< t8: base mip cube face extent.
inline constexpr uint32_t kPrefilteredMipCount = 5;              ///< t8: roughness levels.
inline constexpr Format kPrefilteredFormat = Format::RGBA16Float; ///< t8
inline constexpr uint32_t kDfgLutSize = 64;                     ///< t9: square extent.
inline constexpr Format kDfgLutFormat = Format::RG16Float;      ///< t9

} // namespace lmx::noapi::workload::prod
