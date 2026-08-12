//----------------------------------------------------------------------------------------------------------------------
/// @file Types.h
/// @brief Declares the scalar vocabulary shared by every prototype interface header.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstdint>

namespace lmx::experimental::noapi {

/// Address of GPU-visible memory in the form the GPU dereferences.
///
/// Every buffer-shaped input in this interface is a `GpuAddress`. There is no buffer object: an
/// address plus a size is the whole abstraction, pointer arithmetic on it is defined, and shaders
/// receive addresses as 64-bit pointers rather than as bound resources.
using GpuAddress = uint64_t;

/// Address value that references no memory.
inline constexpr GpuAddress kNullAddress = 0;

/// Alignment applied when a caller does not request a wider one.
///
/// Sixteen bytes is the `float4` alignment every root block and vector load in this interface
/// assumes. Wider alignment is a per-call parameter, never a queried resource property.
inline constexpr uint64_t kDefaultAlignment = 16;

/// Bindless table slot that references no view.
inline constexpr uint32_t kInvalidSlot = ~uint32_t{0};

/// Selects every remaining mip level of a texture in a view range.
inline constexpr uint32_t kAllMipLevels = ~uint32_t{0};

/// Selects every remaining array layer of a texture in a view range.
inline constexpr uint32_t kAllArrayLayers = ~uint32_t{0};

/// Identifies a texel format for texture creation and view reinterpretation.
enum class Format : uint8_t {
    Undefined,        ///< No format; used to mean "attachment absent" in pipeline descriptors.
    RGBA8Unorm,       ///< Four 8-bit normalized channels, linear encoding.
    RGBA8UnormSrgb,   ///< Four 8-bit normalized channels; RGB decoded from sRGB on read.
    BGRA8Unorm,       ///< Four 8-bit normalized channels in BGRA order, linear encoding.
    RG16Float,        ///< Two 16-bit floating-point channels.
    RGBA16Float,      ///< Four 16-bit floating-point channels; the scene-linear working format.
    R32Uint,          ///< One 32-bit unsigned integer channel.
    R32Float,         ///< One 32-bit floating-point channel.
    RG11B10Float,     ///< Packed 11/11/10-bit floating-point color.
    RGB10A2Unorm,     ///< Packed 10/10/10/2-bit normalized color.
    D32Float,         ///< One 32-bit floating-point depth channel.
    BC1RgbaUnorm,     ///< Block-compressed RGBA, linear encoding.
    BC1RgbaUnormSrgb, ///< Block-compressed RGBA; RGB decoded from sRGB on read.
};

/// Identifies the dimensionality of a texture allocation.
enum class TextureKind : uint8_t {
    Texture2D,      ///< Single-layer two-dimensional texture.
    Texture2DArray, ///< Array of two-dimensional layers.
    TextureCube,    ///< Six-face cube map.
    Texture3D,      ///< Volume texture.
};

/// Declares how a texture may be used; violations of the declared set are fatal at use.
enum class TextureUsage : uint32_t {
    None = 0,                         ///< No usage; always invalid for a created texture.
    Sampled = 1u << 0,                ///< Readable through a bindless table slot.
    Storage = 1u << 1,                ///< Writable through a bindless table slot.
    ColorAttachment = 1u << 2,        ///< Usable as a render pass color attachment.
    DepthStencilAttachment = 1u << 3, ///< Usable as a render pass depth attachment.
    CopySource = 1u << 4,             ///< Usable as the source of a copy command.
    CopyDestination = 1u << 5,        ///< Usable as the destination of a copy command.
};

/// Combines two texture usage sets.
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Reports whether `set` declares `usage`.
constexpr bool hasUsage(TextureUsage set, TextureUsage usage) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(usage)) != 0;
}

/// Names a queue execution stage for barriers and split-barrier signals.
///
/// Stages, not resources, are the whole barrier vocabulary of this interface. A barrier states
/// which producer stage must complete before which consumer stage may start; the driver derives
/// the cache maintenance it needs from the stage pair plus the caller's `Hazard` flags.
enum class Stage : uint32_t {
    None = 0,                 ///< Empty stage set.
    Copy = 1u << 0,           ///< Copy and fill commands.
    Compute = 1u << 1,        ///< Compute dispatches.
    VertexShader = 1u << 2,   ///< Vertex shading, including index fetch.
    PixelShader = 1u << 3,    ///< Pixel shading.
    RasterColorOut = 1u << 4, ///< Color attachment writes leaving the rasterizer.
    RasterDepthOut = 1u << 5, ///< Depth attachment writes leaving the rasterizer.
    All = 0xFFFFFFFFu,        ///< Every stage.
};

/// Combines two stage sets.
constexpr Stage operator|(Stage a, Stage b) {
    return static_cast<Stage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Reports whether `set` contains `stage`.
constexpr bool hasStage(Stage set, Stage stage) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(stage)) != 0;
}

/// Names the non-automatic cache maintenance a barrier must additionally perform.
///
/// Ordinary producer-to-consumer barriers need none of these. Each flag corresponds to a cache
/// that hardware does not flush on every barrier, so the caller declares the case rather than
/// listing the resources that would let a driver infer it.
enum class Hazard : uint32_t {
    /// No special cache maintenance beyond the stage dependency.
    None = 0,
    /// The producer wrote bindless table slots; invalidate descriptor caches.
    Descriptors = 1u << 0,
    /// The producer wrote indirect arguments; stall command-processor prefetch.
    DrawArguments = 1u << 1,
    /// The producer wrote depth through a non-raster stage; invalidate depth caches.
    DepthStencil = 1u << 2,
};

/// Combines two hazard sets.
constexpr Hazard operator|(Hazard a, Hazard b) {
    return static_cast<Hazard>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Reports whether `set` contains `hazard`.
constexpr bool hasHazard(Hazard set, Hazard hazard) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(hazard)) != 0;
}

/// Identifies the width of index buffer elements.
enum class IndexKind : uint8_t {
    Uint16, ///< 16-bit indices.
    Uint32, ///< 32-bit indices.
};

/// Names a comparison used by depth-stencil state and by split-barrier waits.
enum class CompareOp : uint8_t {
    Never,        ///< Comparison never passes.
    Less,         ///< Passes when the tested value is smaller.
    Equal,        ///< Passes when the values are equal.
    LessEqual,    ///< Passes when the tested value is smaller or equal.
    Greater,      ///< Passes when the tested value is larger.
    NotEqual,     ///< Passes when the values differ.
    GreaterEqual, ///< Passes when the tested value is larger or equal.
    Always,       ///< Comparison always passes.
};

/// Names how a split-barrier signal updates its counter in GPU memory.
enum class SignalOp : uint8_t {
    Set,       ///< Store the value unconditionally.
    AtomicMax, ///< Store the maximum of the stored and supplied values; gives timeline semantics.
    AtomicOr,  ///< Bitwise-or the value in; gives multi-producer bitmask semantics.
};

/// Identifies the primitive assembly rule of a graphics pipeline.
enum class Topology : uint8_t {
    TriangleList,  ///< Independent triangles.
    TriangleStrip, ///< Connected triangle strip.
};

/// Names which triangle facing the rasterizer discards.
enum class CullMode : uint8_t {
    None,  ///< Keep both facings.
    Front, ///< Discard front-facing triangles.
    Back,  ///< Discard back-facing triangles.
};

/// Names which winding order the rasterizer treats as front-facing.
enum class Winding : uint8_t {
    Clockwise,        ///< Clockwise triangles face the viewer.
    CounterClockwise, ///< Counter-clockwise triangles face the viewer.
};

/// Holds a three-dimensional size in texels.
struct Extent3D {
    uint32_t width = 1;  ///< Size along x, in texels.
    uint32_t height = 1; ///< Size along y, in texels.
    uint32_t depth = 1;  ///< Size along z, in texels or array layers for volume textures.
};

/// Holds a three-dimensional texel offset.
struct Origin3D {
    uint32_t x = 0; ///< Offset along x, in texels.
    uint32_t y = 0; ///< Offset along y, in texels.
    uint32_t z = 0; ///< Offset along z, in texels.
};

/// Holds a byte size paired with the alignment its address must satisfy.
struct SizeAlign {
    uint64_t size = 0;                      ///< Required byte size.
    uint64_t alignment = kDefaultAlignment; ///< Required address alignment, a power of two.
};

/// Describes the rasterizer's output rectangle and depth range.
struct Viewport {
    float x = 0.0f;        ///< Left edge in pixels.
    float y = 0.0f;        ///< Top edge in pixels.
    float width = 0.0f;    ///< Width in pixels.
    float height = 0.0f;   ///< Height in pixels.
    float minDepth = 0.0f; ///< Near depth value written to the depth attachment.
    float maxDepth = 1.0f; ///< Far depth value written to the depth attachment.
};

/// Describes the rasterizer's scissor rectangle in pixels.
struct ScissorRect {
    uint32_t x = 0;      ///< Left edge in pixels.
    uint32_t y = 0;      ///< Top edge in pixels.
    uint32_t width = 0;  ///< Width in pixels.
    uint32_t height = 0; ///< Height in pixels.
};

} // namespace lmx::experimental::noapi
