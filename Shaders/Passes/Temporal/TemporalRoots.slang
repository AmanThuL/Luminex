// Address roots and frame-owned parameter records of the temporal kernels. Each root carries this
// dispatch's parameter address, the persistent exposure pair where the kernel corrects history,
// and the exact sampled, storage and sampler indices it reads and writes. The parameter records
// are mirrored by Source/Render/Passes/Temporal/TemporalFrameData.h, the roots by TemporalRoots.h.
import RojoRHI;

// Shaders/Passes/Temporal/TemporalReproject.slang.
struct TemporalReprojectRoot
{
    rojoRHI::Addr paramsAddr;
    uint history;
    uint sceneColor;
    uint motion;
    uint diagnostic;
    uint sampler;
    uint padding0;
};

// Shaders/Passes/Temporal/TemporalResolve.slang and TemporalUpscale.slang: one root for both, the
// way they shared one slot map.
struct TemporalResolveRoot
{
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr exposureAddr;
    uint sceneColor;
    uint depth;
    uint previousDepth;
    uint motion;
    uint reactive;
    uint history;
    uint output;
    uint rejection;
    uint reprojected;
    uint sampler;
    uint padding0;
    uint padding1;
};

// Shaders/Passes/Temporal/SpatialUpscale.slang.
struct SpatialUpscaleRoot
{
    rojoRHI::Addr paramsAddr;
    uint sceneColor;
    uint output;
    uint sampler;
    uint padding0;
    uint padding1;
    uint padding2;
};

// Shaders/Passes/Temporal/TemporalDebugView.slang.
struct TemporalDebugViewRoot
{
    rojoRHI::Addr paramsAddr;
    uint motion;
    uint diagnostic;
    uint rejection;
    uint reprojected;
    uint resolved;
    uint padding0;
};

// Shaders/Passes/Temporal/VendorTemporalHistory.slang.
struct VendorTemporalHistoryRoot
{
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr exposureAddr;
    uint depth;
    uint previousDepth;
    uint motion;
    uint history;
    uint reprojected;
    uint sampler;
    uint padding0;
    uint padding1;
};

// Shaders/Passes/Temporal/VendorTemporalPack.slang.
struct VendorTemporalPackRoot
{
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr exposureAddr;
    uint motion;
    uint reactive;
    uint packedMotion;
    uint packedReactive;
    uint exposureTexel;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct TemporalResolveParams
{
    uint width;  // Render extent, shared by every input and both outputs.
    uint height;
    uint historyValid;      // 0 on a reset frame: steps 1-6 are skipped and no history is read.
    uint writeDiagnostics;  // Bit 0 writes rejection; bit 1 writes reprojected history.
    // Unjittered, so a reconstructed world position does not carry the frame's sub-pixel offset.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    // The previous frame's positive near-plane distance. Both sides of the disocclusion comparison
    // are depths in the *previous* frame's clip space, so one near plane inverts both of them.
    float previousNearZ;
    // Three floats rather than a float3: a record is laid out naturally, where a float3 takes
    // 16-byte alignment on some targets and would move the tail off the CPU mirror's offsets.
    float pad[3];
};

struct TemporalUpscaleParams
{
    // TemporalResolveParams, field for field and in its order, so the two kernels' blocks stay
    // comparable and the CPU mirror grows by appending only.
    uint width;  // Active render extent, which every render-extent input is read within.
    uint height;
    uint historyValid;      // 0 on a reset frame: no history is read.
    uint writeDiagnostics;  // Bit 0 writes rejection; bit 1 writes reprojected history.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float previousNearZ;
    float pad[3];
    // Appended by the upscaling kernel.
    uint outputWidth;  // Dispatch bound: one invocation per output pixel.
    uint outputHeight;
    uint allocatedWidth;  // Allocation of every render-extent input, which UVs are taken over.
    uint allocatedHeight;
    uint previousRenderWidth;  // The extent the previous depth slot was rendered at.
    uint previousRenderHeight;
    // Where the jittered raster placed the scene, in render texels of texture space: the CPU-side
    // jitterTexelOffset(jitter) of Source/Render/Passes/Temporal/Temporal.h.
    float2 jitterOffset;
};

struct SpatialUpscaleParams
{
    uint renderWidth;  // Active rectangle of the scene colour, anchored at the origin.
    uint renderHeight;
    uint outputWidth;  // Dispatch bound: one invocation per output pixel.
    uint outputHeight;
    uint allocatedWidth;  // Allocation of the scene colour, which every UV is taken over.
    uint allocatedHeight;
    // Where the jittered raster placed the scene, in render texels of texture space.
    float2 jitterOffset;
};

struct TemporalReprojectParams
{
    // The output extent: the dispatch bound, and what the history and the diagnostic are.
    uint width;
    uint height;
    uint renderWidth;  // Active rectangle of the scene colour and the motion target.
    uint renderHeight;
    uint allocatedWidth;  // Allocation of those two, which every UV over them is taken over.
    uint allocatedHeight;
    // Where the jittered raster placed the scene, in render texels of texture space.
    float2 jitterOffset;
};

struct TemporalDebugViewParams
{
    uint view; // One of TemporalDebugView.slang's kView* selectors.
    // The motion target is a render-extent signal while every other input and the display target
    // are at the output extent, so the view needs both to read motion at the right texel.
    uint renderWidth;
    uint renderHeight;
    uint outputWidth;
    uint outputHeight;
};

struct VendorPackParams
{
    uint width;
    uint height;
};
