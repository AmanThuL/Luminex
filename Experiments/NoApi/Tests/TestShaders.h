//----------------------------------------------------------------------------------------------------------------------
/// @file TestShaders.h
/// @brief Holds the hand-written MSL oracles the prototype's smoke tests compile at runtime.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::experimental::noapi::test {

/// Shader-visible ABI the prototype pins: the vertex and compute root address at `buffer(0)`, the
/// bindless table's address at `buffer(1)`, and the pixel root address at `buffer(2)`.
///
/// A bindless slot is one `MTLResourceID`, so the shader views the table as an array of one-member
/// wrapper structs — Apple's Metal compiler rejects a bare pointer-to-texture as a buffer argument
/// but accepts the same pointer once the texture is a struct member.
inline constexpr std::string_view kTestShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    float4 position;
    float2 uv;
    float2 pad;
};

struct VertexRoot {
    float4 tint;
    device const Vertex* vertices;
};

struct SolidPixelRoot {
    float4 scale;
};

struct TextureSlot {
    texture2d<float> texture;
};

struct SamplerSlot {
    sampler state;
};

struct ImageSlot {
    texture2d<float, access::write> image;
};

struct BindlessPixelRoot {
    device const SamplerSlot* samplers;
    uint textureSlot;
    uint samplerSlot;
};

struct FillRoot {
    device uint* out;
    uint count;
    uint base;
};

struct ImageRoot {
    uint slot;
    uint width;
    uint height;
    uint pad;
    float4 color;
};

struct Varyings {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

vertex Varyings lmxTriangleVs(constant VertexRoot& root [[buffer(0)]], uint vertexId [[vertex_id]]) {
    Varyings out;
    out.position = root.vertices[vertexId].position;
    out.color = root.tint;
    out.uv = root.vertices[vertexId].uv;
    return out;
}

fragment float4 lmxSolidFs(Varyings in [[stage_in]], constant SolidPixelRoot& root [[buffer(2)]]) {
    return in.color * root.scale;
}

fragment float4 lmxBindlessFs(Varyings in [[stage_in]],
                              constant BindlessPixelRoot& root [[buffer(2)]],
                              device const TextureSlot* textures [[buffer(1)]]) {
    return textures[root.textureSlot].texture.sample(root.samplers[root.samplerSlot].state, in.uv);
}

kernel void lmxFillBufferKernel(constant FillRoot& root [[buffer(0)]],
                                uint2 threadId [[thread_position_in_grid]],
                                uint2 gridSize [[threads_per_grid]]) {
    const uint index = threadId.y * gridSize.x + threadId.x;
    if (index >= root.count) {
        return;
    }
    root.out[index] = root.base + index;
}

kernel void lmxWriteImageKernel(constant ImageRoot& root [[buffer(0)]],
                                device const ImageSlot* images [[buffer(1)]],
                                uint2 threadId [[thread_position_in_grid]]) {
    if (threadId.x >= root.width || threadId.y >= root.height) {
        return;
    }
    images[root.slot].image.write(root.color, threadId);
}
)";

/// One vertex of the shared triangle; the declaration is the whole CPU-to-GPU layout contract.
struct Vertex {
    float position[4]; ///< Clip-space position.
    float uv[2];       ///< Texture coordinates.
    float pad[2];      ///< Padding matching the shader's declaration.
};

/// Root block the vertex stage reads.
struct alignas(16) VertexRoot {
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Color the triangle is tinted with.
    uint64_t vertices = 0;                    ///< Address of the vertex array.
};

/// Root block `lmxSolidFs` reads.
struct alignas(16) SolidPixelRoot {
    float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Factor applied to the interpolated color.
};

/// Root block `lmxBindlessFs` reads.
struct alignas(8) BindlessPixelRoot {
    uint64_t samplers = 0;    ///< Address of the bindless table, viewed as sampler slots.
    uint32_t textureSlot = 0; ///< Slot holding the texture to sample.
    uint32_t samplerSlot = 0; ///< Slot holding the sampler to sample it with.
};

/// Root block `lmxFillBufferKernel` reads.
struct alignas(8) FillRoot {
    uint64_t out = 0;   ///< Address the kernel writes through.
    uint32_t count = 0; ///< Elements to write.
    uint32_t base = 0;  ///< Value written to element zero.
};

/// Root block `lmxWriteImageKernel` reads.
struct alignas(16) ImageRoot {
    uint32_t slot = 0;                         ///< Bindless slot holding the storage image.
    uint32_t width = 0;                        ///< Image width in texels.
    uint32_t height = 0;                       ///< Image height in texels.
    uint32_t pad = 0;                          ///< Padding matching the shader's declaration.
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Color written to every texel.
};

/// Returns the shader source as the byte span pipeline creation takes.
inline std::span<const std::byte> shaderSource() {
    return {reinterpret_cast<const std::byte*>(kTestShaderSource.data()), kTestShaderSource.size()};
}

} // namespace lmx::experimental::noapi::test
