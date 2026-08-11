//----------------------------------------------------------------------------------------------------------------------
/// @file StressShaders.h
/// @brief Runtime-MSL oracles for the prototype side of M5.1 Stage 4's stress cases (H01-H24,
///        S-BIND, I1-I4). Reuses Tests/TestShaders.h's kernels wherever they already cover a case
///        (lmxFillBufferKernel, lmxWriteImageKernel, lmxTriangleVs/lmxSolidFs/lmxBindlessFs) and
///        adds only what those do not: a data-dependent identity copy through a bindless storage
///        texture, a texture-to-buffer relay, per-draw indirect-argument writes, and S-BIND's
///        textured quad. Every texture here is addressed through the one bindless table
///        (`buffer(1)`), never bound directly -- the model has no other texture-binding mechanism
///        (NoApi/CommandBuffer.h).
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::noapi::bench {

/// Shader-visible ABI matching Tests/TestShaders.h's own convention: root address at buffer(0),
/// bindless table address at buffer(1), pixel/second root at buffer(2).
inline constexpr std::string_view kStressShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

// A read_write slot serves both the copy kernel's source (read only) and destination (write only)
// textures from the one bindless table array: MSL has no way to bind two differently-qualified
// views of buffer(1) to one kernel, so both textures this file writes through gain
// `access::read_write` locally (their host-side Storage usage already grants it) and every access
// here calls only .read or only .write, never .sample -- filtering is never needed off this path.
struct RwSlot {
    texture2d<float, access::read_write> image;
};
struct TextureSlot {
    texture2d<float> texture;
};
struct SamplerSlot {
    sampler state;
};

struct CopyImageRoot {
    uint dstSlot;
    uint srcSlot;
    uint extent;
    uint pad;
};

// The hazard matrix's producer/consumer content is always defined on the CPU
// (Workload/StressCases.h's hazardExpectedTexel) and uploaded once into the slot `srcSlot` names;
// every "producer" role that needs to move that content into the case's target texture through a
// real compute pass does so with this one kernel, so the case's pass/barrier structure -- not a
// bespoke per-case formula -- is what a correct or incorrect ordering can be distinguished by.
kernel void lmxCopyImageKernel(constant CopyImageRoot& root [[buffer(0)]],
                               device const RwSlot* slots [[buffer(1)]],
                               uint2 tid [[thread_position_in_grid]]) {
    if (tid.x >= root.extent || tid.y >= root.extent) {
        return;
    }
    slots[root.dstSlot].image.write(slots[root.srcSlot].image.read(tid), tid);
}

struct ReadImageRoot {
    device uint* out;
    uint srcSlot;
    uint extent;
    uint pad;
};

// Relays a bindless texture's RGBA8-packed content into a linear buffer -- the compute-side
// "reader" half of every hazard case whose consumer (or WAR/WAW producer) role is a compute read.
kernel void lmxReadImageToBufferKernel(constant ReadImageRoot& root [[buffer(0)]],
                                       device const RwSlot* slots [[buffer(1)]],
                                       uint2 tid [[thread_position_in_grid]]) {
    if (tid.x >= root.extent || tid.y >= root.extent) {
        return;
    }
    float4 texel = slots[root.srcSlot].image.read(tid);
    uint4 texelBytes = uint4(round(saturate(texel) * 255.0));
    uint packed = texelBytes.r | (texelBytes.g << 8) | (texelBytes.b << 16) | (texelBytes.a << 24);
    root.out[tid.y * root.extent + tid.x] = packed;
}

struct QuadRoot {
    float2 offset;
    float2 halfExtent;
};
struct QuadPixelRoot {
    device const TextureSlot* textures;
    device const SamplerSlot* samplers;
    uint textureSlot;
    uint samplerSlot;
    float4 tint;
};

struct QuadVaryings {
    float4 position [[position]];
    float2 uv;
};

// vid in [0,6): two-triangle unit quad, matching Shaders/StressQuad.slang's vertexQuad exactly.
vertex QuadVaryings lmxQuadVs(constant QuadRoot& root [[buffer(0)]], uint vid [[vertex_id]]) {
    constexpr float2 corners[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    constexpr uint indices[6] = {0, 1, 2, 0, 2, 3};
    float2 corner = corners[indices[vid]];
    QuadVaryings out;
    out.position = float4(root.offset + corner * root.halfExtent, 0.0, 1.0);
    out.uv = corner * 0.5 + 0.5;
    return out;
}

fragment float4 lmxQuadFs(QuadVaryings in [[stage_in]],
                          constant QuadPixelRoot& root [[buffer(2)]],
                          device const TextureSlot* textures [[buffer(1)]]) {
    return textures[root.textureSlot].texture.sample(root.samplers[root.samplerSlot].state, in.uv) *
           root.tint;
}

struct IndirectArgsWriteRoot {
    device uint* args;
    uint argsIndex;   // Element index (not byte offset) of the first written word.
    uint valueA;
    uint valueB;
    uint valueC;
    uint valueD;
    uint valueE;
    uint wordCount;   // Number of words in {A..E} actually written (3 for dispatch, 5 for indexed draw).
};

// Writes a small fixed-size argument block one word at a time, mirroring
// Shaders/IndirectSmoke.slang's computeWriteDispatchArgs/computeWriteDrawIndexedArgs so both
// encoders realise I2/I4's "compute-written" arguments the same way.
kernel void lmxWriteIndirectArgsKernel(constant IndirectArgsWriteRoot& root [[buffer(0)]],
                                       uint3 tid [[thread_position_in_grid]]) {
    if (tid.x != 0 || tid.y != 0 || tid.z != 0) {
        return;
    }
    uint values[5] = {root.valueA, root.valueB, root.valueC, root.valueD, root.valueE};
    for (uint i = 0; i < root.wordCount; ++i) {
        root.args[root.argsIndex + i] = values[i];
    }
}
)";

/// Root block `lmxCopyImageKernel` reads.
struct alignas(16) CopyImageRoot {
    uint32_t dstSlot = 0;
    uint32_t srcSlot = 0;
    uint32_t extent = 0;
    uint32_t pad = 0;
};

/// Root block `lmxReadImageToBufferKernel` reads.
struct alignas(8) ReadImageRoot {
    uint64_t out = 0;
    uint32_t srcSlot = 0;
    uint32_t extent = 0;
};

/// Root block `lmxQuadVs` reads.
struct alignas(16) QuadRoot {
    float offset[2] = {0.0f, 0.0f};
    float halfExtent[2] = {0.0f, 0.0f};
};

/// Root block `lmxQuadFs` reads. MSL gives `float4 tint` its natural 16-byte alignment, which pads
/// the preceding two pointers (16 bytes) and two uints (8 bytes) -- 24 bytes -- up to 32; the
/// explicit `pad` below matches that so both sides agree on `tint`'s byte offset (a plain
/// `float tint[4]` member here, with no such padding, previously landed at byte 24 in this struct
/// while the shader read it from byte 32, corrupting every sampled colour's blue and alpha channels
/// with whatever bytes followed in the root allocator).
struct alignas(16) QuadPixelRoot {
    uint64_t textures = 0; ///< Address of the bindless texture-slot array.
    uint64_t samplers = 0; ///< Address of the bindless sampler-slot array.
    uint32_t textureSlot = 0;
    uint32_t samplerSlot = 0;
    uint32_t pad[2] = {0, 0};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

/// Root block `lmxWriteIndirectArgsKernel` reads.
struct alignas(8) IndirectArgsWriteRoot {
    uint64_t args = 0;
    uint32_t argsIndex = 0;
    uint32_t valueA = 0;
    uint32_t valueB = 0;
    uint32_t valueC = 0;
    uint32_t valueD = 0;
    uint32_t valueE = 0;
    uint32_t wordCount = 0;
};

/// Returns the shader source as the byte span pipeline creation takes.
inline std::span<const std::byte> stressShaderSource() {
    return {reinterpret_cast<const std::byte*>(kStressShaderSource.data()),
            kStressShaderSource.size()};
}

} // namespace lmx::noapi::bench
